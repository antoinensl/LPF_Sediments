#include "LatentEngine.h"
#include "Analysis.h"

#include <chrono>

namespace
{
    constexpr float silenceThreshold = 0.001f;   // -60 dBFS RMS: such frames stay off the map

    // Thread-safe: the import and audition threads both read files.
    juce::AudioFormatManager& formatManager()
    {
        struct Holder { juce::AudioFormatManager fm; Holder() { fm.registerBasicFormats(); } };
        static Holder holder;
        return holder.fm;
    }

    // Resample a whole signal with Lagrange interpolation (good enough for analysis).
    std::vector<float> resample (const std::vector<float>& in, double fromRate, double toRate)
    {
        if (std::abs (fromRate - toRate) < 0.5)
            return in;
        const double ratio = fromRate / toRate;
        const auto outLen = (int) std::floor ((double) in.size() / ratio);
        std::vector<float> padded (in);
        padded.resize (in.size() + 16, 0.0f);
        std::vector<float> out ((size_t) std::max (0, outLen));
        juce::LagrangeInterpolator interp;
        interp.process (ratio, padded.data(), out.data(), outLen);
        return out;
    }
}

LatentEngine::LatentEngine() {}

LatentEngine::~LatentEngine()
{
    stopWorker();
    importPool.removeAllJobs (true, 20000);
    auditionPool.removeAllJobs (true, 5000);
    stopTraining();
}

//==============================================================================
std::shared_ptr<const LearnedTerrain> LatentEngine::getLearnedTerrain() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return learned;
}

void LatentEngine::stopTraining()
{
    std::lock_guard<std::mutex> lock (trainerMutex);
    ++trainGeneration;
    if (trainer.joinable())
        trainer.join();
    trainProgress = -1.0f;
}

void LatentEngine::startTraining (std::shared_ptr<const Corpus> c)
{
    std::lock_guard<std::mutex> lock (trainerMutex);
    const int gen = ++trainGeneration;      // makes any running training abort
    if (trainer.joinable())
        trainer.join();

    if (c == nullptr || c->size() < 4)
    {
        {
            std::lock_guard<std::mutex> snap (snapMutex);
            learned = nullptr;
        }
        ++learnedVersion;
        trainProgress = -1.0f;
        return;
    }

    trainProgress = 0.0f;
    trainer = std::thread ([this, c, gen]
    {
        juce::Thread::setCurrentThreadName ("Sediments terrain training");
        std::shared_ptr<LearnedTerrain> t;
        try
        {
            t = LearnedTerrain::train (c, [this, gen] (float p)
            {
                if (trainGeneration.load() != gen)
                    return false;
                trainProgress = p;
                return true;
            });
        }
        catch (const std::exception& e)
        {
            setStatus (juce::String ("Terrain training failed: ") + e.what());
        }

        if (trainGeneration.load() != gen)
            return;     // superseded by a newer corpus
        {
            std::lock_guard<std::mutex> snap (snapMutex);
            learned = t;
        }
        ++learnedVersion;
        trainProgress = -1.0f;
    });
}

//==============================================================================
void LatentEngine::prepare (double hostSampleRate, int maxBlockSize)
{
    stopWorker();
    hostRate = hostSampleRate;
    maxBlock = maxBlockSize;
    fifo.reset();
    fifoBuffer.clear();
    noteFifo.reset();
    uiNoteFifo.reset();
    consumedTotal = 0;
    consumedStamp = 0;
    startWorker();
}

void LatentEngine::release()
{
    stopWorker();
}

void LatentEngine::render (juce::AudioBuffer<float>& buffer, int numSamples, float gain) noexcept
{
    consumedTotal += numSamples;

    // Original sound preview, mixed on top.
    {
        const juce::SpinLock::ScopedTryLockType lock (auditionLock);
        if (lock.isLocked() && audition != nullptr && ! audition->finished.load())
        {
            auto& clip = *audition;
            const bool ambisonic = outputChoice.load() >= 9 && outputChoice.load() < Spatial::firstSurroundChoice;
            const int n = juce::jmin (numSamples, clip.buffer.getNumSamples() - clip.pos);
            for (int i = 0; i < n; ++i)
            {
                if (clip.stopping.load())
                {
                    clip.stopGain -= 1.0f / 480.0f;
                    if (clip.stopGain <= 0.0f) { clip.finished = true; break; }
                }
                const float g = gain * clip.stopGain;
                const float l = clip.buffer.getSample (0, clip.pos + i), r = clip.buffer.getSample (1, clip.pos + i);
                if (ambisonic || buffer.getNumChannels() == 1)
                    buffer.getWritePointer (0)[i] += g * 0.5f * (l + r);
                else
                {
                    buffer.getWritePointer (0)[i] += g * l;
                    buffer.getWritePointer (1)[i] += g * r;
                }
            }
            clip.pos += n;
            if (clip.pos >= clip.buffer.getNumSamples())
                clip.finished = true;
        }
    }

    const int toRead = juce::jmin (numSamples, fifo.getNumReady());
    if (toRead <= 0)
        return;

    int s1, n1, s2, n2;
    fifo.prepareToRead (toRead, s1, n1, s2, n2);
    const int chans = juce::jmin (buffer.getNumChannels(), Spatial::maxChannels);
    // "Sound plays": fade in quickly (20 ms) when allowed, out gently (200 ms) when not.
    const float target = gateOpen.load() ? 1.0f : 0.0f;
    const float seconds = target > gateGain ? 0.02f : 0.2f;
    const float step = (float) (numSamples / (juce::jmax (1.0, hostRateForGate.load()) * seconds));
    const float g0 = gateGain;
    gateGain = target > gateGain ? juce::jmin (target, gateGain + step) : juce::jmax (target, gateGain - step);
    const float g1 = gateGain;
    const float gm = g0 + (g1 - g0) * (float) n1 / (float) juce::jmax (1, n1 + n2);
    if (g0 > 0.0f || g1 > 0.0f)
        for (int ch = 0; ch < chans; ++ch)
        {
            if (n1 > 0) buffer.addFromWithRamp (ch, 0, fifoBuffer.getReadPointer (ch, s1), n1, gain * g0, gain * gm);
            if (n2 > 0) buffer.addFromWithRamp (ch, n1, fifoBuffer.getReadPointer (ch, s2), n2, gain * gm, gain * g1);
        }
    fifo.finishedRead (n1 + n2);
}

void LatentEngine::setSoundDirections (const std::map<juce::String, float>& directions)
{
    {
        std::lock_guard<std::mutex> lock (snapMutex);
        soundDirections = directions;
    }
    ++soundDirVersion;
}

void LatentEngine::publishGrain (const Spatial::Place& p, int source) noexcept
{
    // at most ~30 marks a second: very short grains would only make a blur on screen
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastGrainMark.load() < 33)
        return;
    lastGrainMark = now;
    auto& m = grainMarks[(grainMarkNext++ & 0x7fffffff) % numGrainMarks];
    m.az = p.az; m.el = p.el; m.dist = p.dist; m.source = source;
    m.when = juce::Time::getMillisecondCounter();
}

void LatentEngine::publishModelPlace (const Spatial::Place& p) noexcept
{
    modelAz = p.az; modelEl = p.el; modelDist = p.dist;
    modelPlaceActive = true;
}

//==============================================================================
void LatentEngine::startWorker()
{
    if (running.exchange (true))
        return;
    worker = std::thread ([this] { workerLoop(); });
}

void LatentEngine::stopWorker()
{
    if (! running.exchange (false))
        return;
    if (worker.joinable())
        worker.join();
}

void LatentEngine::pushNote (int note, float velocity, bool isOn) noexcept
{
    int s1, n1, s2, n2;
    noteFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        noteEvents[s1] = { note, velocity, isOn };
    noteFifo.finishedWrite (n1);
}

void LatentEngine::pushNoteFromUI (int note, float velocity, bool isOn) noexcept
{
    int s1, n1, s2, n2;
    uiNoteFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        uiNoteEvents[s1] = { note, velocity, isOn };
    uiNoteFifo.finishedWrite (n1);
}

//==============================================================================
void LatentEngine::auditionAsync (const juce::String& path, double startSec, double durationSec)
{
    const int request = ++auditionRequest;
    const double rate = hostRate;
    auditionPool.addJob ([this, path, startSec, durationSec, request, rate]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (juce::File (path)));
        if (reader == nullptr || auditionRequest.load() != request)
            return;
        const double srcRate = reader->sampleRate;
        const auto start = (juce::int64) juce::jmax (0.0, startSec * srcRate);
        const double maxDur = durationSec > 0.0 ? durationSec : 60.0;
        const int len = (int) juce::jlimit<juce::int64> (0, (juce::int64) (maxDur * srcRate), reader->lengthInSamples - start);
        if (len <= 0)
            return;
        juce::AudioBuffer<float> src (2, len);
        reader->read (&src, 0, len, start, true, true);
        if (reader->numChannels == 1)
            src.copyFrom (1, 0, src, 0, 0, len);

        auto clip = std::make_unique<AuditionClip>();
        clip->path = path;
        const double ratio = srcRate / rate;
        const int outLen = (int) std::floor (len / ratio) - 4;
        clip->buffer.setSize (2, juce::jmax (1, outLen));
        for (int c = 0; c < 2; ++c)
        {
            juce::LagrangeInterpolator li;
            std::vector<float> padded (src.getReadPointer (c), src.getReadPointer (c) + len);
            padded.resize ((size_t) len + 16, 0.0f);
            li.process (ratio, padded.data(), clip->buffer.getWritePointer (c), clip->buffer.getNumSamples());
        }
        // gentle edges
        const int fade = juce::jmin (clip->buffer.getNumSamples() / 2, (int) (rate * 0.015));
        clip->buffer.applyGainRamp (0, fade, 0.0f, 1.0f);
        clip->buffer.applyGainRamp (clip->buffer.getNumSamples() - fade, fade, 1.0f, 0.0f);

        if (auditionRequest.load() != request)
            return;
        std::unique_ptr<AuditionClip> old;
        {
            const juce::SpinLock::ScopedLockType lock (auditionLock);
            old = std::move (audition);
            audition = std::move (clip);
        }
    });
}

void LatentEngine::stopAudition()
{
    ++auditionRequest;
    const juce::SpinLock::ScopedLockType lock (auditionLock);
    if (audition != nullptr)
        audition->stopping = true;
}

juce::String LatentEngine::getAuditionPath() const
{
    const juce::SpinLock::ScopedLockType lock (auditionLock);
    if (audition == nullptr || audition->finished.load() || audition->stopping.load())
        return {};
    return audition->path;
}

void LatentEngine::setTransport (bool playing, double ppq, double bpm) noexcept
{
    consumedStamp = consumedTotal.load();
    ppqStamp = ppq;
    bpmStamp = bpm > 1.0 ? bpm : 120.0;
    hostPlaying = playing;
}

void LatentEngine::setPath (std::vector<PathPoint> points)
{
    std::shared_ptr<const std::vector<PathPoint>> p;
    if (points.size() >= 2 && points.back().t > 0.0f)
        p = std::make_shared<const std::vector<PathPoint>> (std::move (points));
    {
        std::lock_guard<std::mutex> lock (snapMutex);
        path = p;
    }
    ++pathVersion;
}

std::shared_ptr<const std::vector<LatentEngine::PathPoint>> LatentEngine::getPath() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return path;
}

namespace
{
    LatentEngine::PathPoint samplePath (const std::vector<LatentEngine::PathPoint>& p, float t)
    {
        if (t <= p.front().t) return p.front();
        if (t >= p.back().t)  return p.back();
        auto it = std::upper_bound (p.begin(), p.end(), t, [] (float v, const LatentEngine::PathPoint& q) { return v < q.t; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const float u = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0.0f;
        return { t, a.x + u * (b.x - a.x), a.y + u * (b.y - a.y) };
    }

    // Random pick among the k frames nearest to (x, y), preferring frames that
    // have a "next moment" (so Flow can keep moving).
    int seekNear (const Corpus& c, float x, float y, juce::Random& rnd, int k = 6)
    {
        const int n = c.size();
        std::vector<std::pair<float, int>> best;
        best.reserve ((size_t) k + 1);
        for (int i = 0; i < n; ++i)
        {
            if (c.nextFrame (i) < 0 && n > 1)
                continue;
            const float dx = c.xy[(size_t) i * 2] - x, dy = c.xy[(size_t) i * 2 + 1] - y;
            const float d = dx * dx + dy * dy;
            if ((int) best.size() < k || d < best.back().first)
            {
                best.insert (std::upper_bound (best.begin(), best.end(), std::make_pair (d, i)), { d, i });
                if ((int) best.size() > k) best.pop_back();
            }
        }
        if (best.empty())
            return 0;
        return best[(size_t) rnd.nextInt ((int) best.size())].second;
    }
}

void LatentEngine::workerLoop()
{
    juce::Thread::setCurrentThreadName ("Sediments synthesis");

    std::shared_ptr<RaveModel> currentModel;
    std::vector<float> target, z, learnedZ;
    std::vector<float> staging[2];
    juce::LagrangeInterpolator interp[2];
    std::vector<float> resampled;

    // MIDI / envelope state
    std::vector<int> heldNotes;
    int lastNote = -1;
    float noteGain = 1.0f, sprayDx = 0.0f, sprayDy = 0.0f;
    juce::ADSR env;
    env.setSampleRate (hostRate);
    juce::Random rnd;

    // Path and Flow state
    double pathClock = 0.0;            // in path lengths (free) or beats (synced, transport stopped)
    juce::int64 producedTotal = 0;     // host samples generated since prepare()
    const Corpus* flowCorpus = nullptr;
    int head = -1;
    double headFrac = 0.0;

    // Originals (concatenative) state
    GrainPlayer grainPlayer;
    std::vector<float> origBuf, mixBuf;
    Spatial::Placer placer;
    const Corpus* placerCorpus = nullptr;
    int placerDirVersion = -1;
    float rotationDeg = 0.0f;
    float prevGl[Spatial::maxChannels] = {}, prevGr[Spatial::maxChannels] = {};
    bool modelGainsValid = false;
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    Biquad lfeFilter[2];
    int lfeFilterChannel = -2;
    double lfeFilterRate = 0.0;
    int modelChannels = 1;
    float gx = -1.0f, gy = -1.0f;      // glided position for grain picking

    while (running.load())
    {
        std::shared_ptr<RaveModel> dec;
        std::shared_ptr<const Corpus> corp;
        std::shared_ptr<const LearnedTerrain> lt;
        std::shared_ptr<const std::vector<PathPoint>> pth;
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            dec = decoder;
            corp = corpus;
            lt = learned;
            pth = path;
        }

        const bool modelOk = dec != nullptr && corp != nullptr && corp->size() > 0 && corp->modelLatents
                             && corp->dim == dec->getLatentSize();
        const bool originalsOk = corp != nullptr && corp->size() > 0 && ! corp->audio.empty();
        if (! modelOk && ! originalsOk)
        {
            curX = posX.load();
            curY = posY.load();
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
            continue;
        }

        // What plays: the model, the original sounds, or a blend of both.
        const float srcMix = modelOk ? juce::jlimit (0.0f, 1.0f, sourceMix.load()) : 1.0f;
        const bool useModel = modelOk && srcMix < 0.999f;
        const bool useOriginals = originalsOk && srcMix > 0.001f;

        // Originals alone are rendered in short 512-sample frames (low latency).
        const int ratio = useModel ? dec->getRatio() : 512;
        const double modelRate = useModel ? (double) dec->getSampleRate() : hostRate;
        const double speed = modelRate / hostRate;   // model samples per host sample
        const double frameSeconds = ratio / modelRate;
        if (! useModel)
            for (int c = 0; c < 2; ++c) { staging[c].clear(); interp[c].reset(); }

        // Keep about two latent frames (plus one host block) buffered ahead.
        const int frameAtHost = (int) std::ceil (ratio / speed);
        const int targetFill = juce::jmin (fifoCapacity - frameAtHost - 64, 2 * frameAtHost + maxBlock);
        if (fifo.getNumReady() >= targetFill)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }

        const bool midi = midiMode.load();
        env.setParameters ({ juce::jmax (0.001f, attackSec.load()), 0.0f, 1.0f, juce::jmax (0.005f, releaseSec.load()) });

        // 0. Notes (monophonic, last note wins; legato keeps the envelope going).
        bool retrigger = false;
        {
            int s1, n1, s2, n2;
            const int ready = noteFifo.getNumReady();
            noteFifo.prepareToRead (ready, s1, n1, s2, n2);
            auto handle = [&] (const NoteEvent& ev)
            {
                heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), ev.note), heldNotes.end());
                if (ev.on)
                {
                    const bool wasHeld = ! heldNotes.empty();
                    heldNotes.push_back (ev.note);
                    lastNote = ev.note;
                    noteGain = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, ev.velocity);
                    const float a = rnd.nextFloat() * juce::MathConstants<float>::twoPi;
                    const float r = std::sqrt (rnd.nextFloat());
                    sprayDx = r * std::cos (a);
                    sprayDy = r * std::sin (a);
                    if (! wasHeld) { env.noteOn(); retrigger = true; }
                }
                else if (heldNotes.empty())
                    env.noteOff();
                else
                    lastNote = heldNotes.back();
            };
            for (int i = 0; i < n1; ++i) handle (noteEvents[s1 + i]);
            for (int i = 0; i < n2; ++i) handle (noteEvents[s2 + i]);
            noteFifo.finishedRead (n1 + n2);

            uiNoteFifo.prepareToRead (uiNoteFifo.getNumReady(), s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) handle (uiNoteEvents[s1 + i]);
            for (int i = 0; i < n2; ++i) handle (uiNoteEvents[s2 + i]);
            uiNoteFifo.finishedRead (n1 + n2);
        }

        // 1. Base position: the X/Y cursor, or the zone of the last MIDI note.
        float bx = posX.load(), by = posY.load();
        int zone = -1;
        if (midi && lastNote >= 0 && corp->numZones() > 0)
        {
            zone = (lastNote % 12) % corp->numZones();
            const float radius = spray.load() * juce::jmax (0.06f, 2.0f * corp->zoneRadius[(size_t) zone]);
            bx = corp->zones[(size_t) zone * 2] + radius * sprayDx;
            by = corp->zones[(size_t) zone * 2 + 1] + radius * sprayDy;
        }
        activeZone = midi ? zone : -1;

        // 2. Path: a recorded gesture, looping freely or locked to the host tempo.
        float px = bx, py = by;
        if (pathOn.load() && pth != nullptr)
        {
            const float dur = pth->back().t;
            const int beats = pathSyncBeats.load();
            double u;   // position in path lengths
            if (beats > 0)
            {
                const double bpm = bpmStamp.load();
                if (hostPlaying.load())
                {
                    const double ppq = ppqStamp.load() + (double) (producedTotal - consumedStamp.load()) / hostRate * bpm / 60.0;
                    u = ppq / beats;
                }
                else
                {
                    pathClock += frameSeconds * bpm / 60.0;
                    u = pathClock / beats;
                }
            }
            else
            {
                if (retrigger && pathFollows.load() && midi)
                    pathClock = 0.0;
                pathClock += frameSeconds * pathSpeed.load() / dur;
                u = pathClock;
            }

            double phase;
            switch (pathDir.load())
            {
                case 1:  phase = 1.0 - (u - std::floor (u)); break;
                case 2:  { const double q = u - 2.0 * std::floor (u / 2.0); phase = q <= 1.0 ? q : 2.0 - q; } break;
                default: phase = u - std::floor (u); break;
            }
            pathPhase = (float) phase;

            const auto pt = samplePath (*pth, (float) phase * dur);
            if (pathFollows.load())
            {
                px = bx + (pt.x - pth->front().x);
                py = by + (pt.y - pth->front().y);
            }
            else
            {
                px = pt.x;
                py = pt.y;
            }
        }
        else
            pathPhase = -1.0f;

        px = juce::jlimit (0.0f, 1.0f, px);
        py = juce::jlimit (0.0f, 1.0f, py);
        baseX = bx; baseY = by;
        curX = px;  curY = py;

        // 3. Terrain: position -> target latent vector (model only).
        int produced = 0;
        if (useModel)
        {
        const int dim = dec->getLatentSize();
        target.resize ((size_t) dim);
        const float mix = juce::jlimit (0.0f, 1.0f, terrainMix.load());
        const bool useLearned = lt != nullptr && lt->trainedOn() == corp.get() && lt->getDim() == dim && mix > 0.0f;
        if (mix < 1.0f || ! useLearned)
            Terrain::query (*corp, px, py, Terrain::blurToSigma (blur.load()), target.data());
        if (useLearned)
        {
            learnedZ.resize ((size_t) dim);
            lt->query (px, py, learnedZ.data());
            if (mix >= 1.0f)
                target = learnedZ;
            else
                for (int d = 0; d < dim; ++d)
                    target[(size_t) d] += mix * (learnedZ[(size_t) d] - target[(size_t) d]);
        }

        // 4. Flow: follow the original sound's own motion from the nearest frame.
        const float fl = juce::jlimit (0.0f, 1.0f, flow.load());
        if (fl > 0.0f)
        {
            if (flowCorpus != corp.get() || head < 0 || head >= corp->size())
            {
                flowCorpus = corp.get();
                head = seekNear (*corp, px, py, rnd);
                headFrac = 0.0;
            }
            // Stay on a leash: if the cursor moved away, jump near it.
            const float leash = juce::jmax (0.04f, 1.5f * Terrain::blurToSigma (blur.load()));
            const float hx = corp->xy[(size_t) head * 2] - px, hy = corp->xy[(size_t) head * 2 + 1] - py;
            if (hx * hx + hy * hy > leash * leash)
            {
                head = seekNear (*corp, px, py, rnd);
                headFrac = 0.0;
            }

            headFrac += juce::jlimit (0.05f, 8.0f, flowSpeed.load());
            while (headFrac >= 1.0)
            {
                headFrac -= 1.0;
                const int nx = corp->nextFrame (head);
                head = nx >= 0 ? nx : seekNear (*corp, px, py, rnd);
            }

            const int nx = corp->nextFrame (head);
            const float* a = corp->latents.data() + (size_t) head * (size_t) dim;
            const float* b = corp->latents.data() + (size_t) (nx >= 0 ? nx : head) * (size_t) dim;
            const float u = (float) headFrac;
            for (int d = 0; d < dim; ++d)
            {
                const float zf = a[d] + u * (b[d] - a[d]);
                target[(size_t) d] += fl * (zf - target[(size_t) d]);
            }
        }
        else
            head = -1;

        // 2. Glide: smooth the latent trajectory over time.
        if (dec != currentModel || z.size() != target.size())
        {
            currentModel = dec;
            z = target;
            for (int c = 0; c < 2; ++c) { staging[c].clear(); interp[c].reset(); }
        }
        else
        {
            const float g = glide.load();
            const double tau = 0.0005 + 3.0 * (double) g * g;
            const float k = (float) (1.0 - std::exp (-frameSeconds / tau));
            for (size_t d = 0; d < z.size(); ++d)
                z[d] += k * (target[d] - z[d]);
        }

        // 3. Decode one latent frame -> `ratio` samples at the model rate.
        std::vector<std::vector<float>> audio;
        try
        {
            audio = dec->decode (z.data(), 1);
        }
        catch (const std::exception& e)
        {
            setStatus (juce::String ("Decoding error: ") + e.what());
            std::this_thread::sleep_for (std::chrono::milliseconds (200));
            continue;
        }
        if (audio.empty())
            continue;

        modelChannels = (int) audio.size();
        for (int c = 0; c < 2; ++c)
        {
            const auto& src = audio[(size_t) juce::jmin (c, (int) audio.size() - 1)];
            staging[c].insert (staging[c].end(), src.begin(), src.end());
        }

        // 4. Resample to the host rate.
        if (std::abs (speed - 1.0) < 1e-9)
        {
            produced = (int) staging[0].size();
            resampled.resize ((size_t) produced * 2);
            for (int c = 0; c < 2; ++c)
            {
                std::copy (staging[c].begin(), staging[c].end(), resampled.begin() + c * produced);
                staging[c].clear();
            }
        }
        else
        {
            const int available = (int) staging[0].size();
            produced = juce::jmax (0, (int) std::floor ((available - 8) / speed));
            resampled.resize ((size_t) produced * 2);
            int used = 0;
            for (int c = 0; c < 2; ++c)
            {
                used = interp[c].process (speed, staging[c].data(), resampled.data() + c * produced, produced);
                staging[c].erase (staging[c].begin(), staging[c].begin() + used);
            }
        }
        }   // useModel
        else
        {
            produced = ratio;
            resampled.assign ((size_t) produced * 2, 0.0f);
        }

        // 4b. Space: where the model voice and each grain are heard.
        const auto fmt = Spatial::formatFor (outputChoice.load(), frontCentre.load(), useCentre.load(), lfeOn.load());
        {
            if (placerCorpus != corp.get())
            {
                placerCorpus = corp.get();
                placer.prepare (*corp);
                placerDirVersion = -1;
            }
            const int dv = soundDirVersion.load();
            if (dv != placerDirVersion)
            {
                placerDirVersion = dv;
                std::lock_guard<std::mutex> lock (snapMutex);
                placer.soundAz.resize (corp->sources.size());
                for (size_t s = 0; s < corp->sources.size(); ++s)
                {
                    auto it = soundDirections.find (juce::String (corp->sources[s].path));
                    placer.soundAz[s] = it != soundDirections.end() ? it->second
                                                                    : Spatial::defaultSoundAz ((int) s, (int) corp->sources.size());
                }
            }
            placer.by = (Spatial::PlaceBy) juce::jlimit (0, 6, placeBy.load());
            placer.height = (Spatial::Height) juce::jlimit (0, 6, heightMode.load());
            placer.width = juce::jlimit (0.0f, 1.0f, spaceWidth.load());
            placer.scatter = juce::jlimit (0.0f, 1.0f, scatter.load());
            rotationDeg = (float) std::fmod (rotationDeg + rotateSpeed.load() * 360.0 * frameSeconds, 360.0);
            placer.rotation = rotationDeg;
        }

        constexpr int NC = Spatial::maxChannels;
        mixBuf.assign ((size_t) produced * NC, 0.0f);
        if (useModel && produced > 0)
        {
            const auto place = placer.placeAt (*corp, px, py);
            float gl[NC], gr[NC];
            Spatial::gains (fmt, place, modelChannels > 1, gl, gr);
            if (! modelGainsValid) { std::copy (gl, gl + NC, prevGl); std::copy (gr, gr + NC, prevGr); modelGainsValid = true; }
            publishModelPlace (place);
            const float* l = resampled.data();
            const float* r = resampled.data() + produced;
            const float amount = 1.0f - srcMix;
            for (int c = 0; c < fmt.channels; ++c)
            {
                float* o = mixBuf.data() + (size_t) c * (size_t) produced;
                const float a0 = prevGl[c], a1 = gl[c], b0 = prevGr[c], b1 = gr[c];
                if (a0 == 0.0f && a1 == 0.0f && b0 == 0.0f && b1 == 0.0f)
                    continue;
                for (int i = 0; i < produced; ++i)
                {
                    const float t = (float) (i + 1) / (float) produced;
                    o[i] = amount * ((a0 + t * (a1 - a0)) * l[i] + (b0 + t * (b1 - b0)) * r[i]);
                }
            }
            std::copy (gl, gl + NC, prevGl);
            std::copy (gr, gr + NC, prevGr);
        }
        else
        {
            modelGainsValid = false;
            modelPlaceActive = false;
        }

        // 4c. Original sounds: grains of the moments around the cursor.
        if (useOriginals && produced > 0)
        {
            const float g = glide.load();
            const double tau = 0.0005 + 3.0 * (double) g * g;
            const float k = (float) (1.0 - std::exp (-frameSeconds / tau));
            if (gx < 0.0f) { gx = px; gy = py; }
            gx += k * (px - gx);
            gy += k * (py - gy);

            GrainPlayer::Params gp;
            gp.x = gx; gp.y = gy;
            gp.sigma = juce::jmax (0.004f, Terrain::blurToSigma (blur.load()));
            gp.flow = juce::jlimit (0.0f, 1.0f, flow.load());
            gp.flowSpeed = juce::jlimit (0.05f, 8.0f, flowSpeed.load());
            gp.grainMs = juce::jlimit (10.0f, 1000.0f, grainMs.load());
            gp.leash = juce::jmax (0.04f, 1.5f * gp.sigma);
            gp.format = fmt;
            gp.placer = &placer;
            gp.onGrain = [this] (const Spatial::Place& pl, int source) { publishGrain (pl, source); };
            origBuf.assign ((size_t) produced * NC, 0.0f);
            float* outs[NC];
            for (int c = 0; c < NC; ++c)
                outs[c] = origBuf.data() + (size_t) c * (size_t) produced;
            grainPlayer.render (outs, fmt.channels, produced, hostRate, *corp, gp, rnd);
            for (size_t i = 0; i < (size_t) fmt.channels * (size_t) produced; ++i)
                mixBuf[i] += srcMix * origBuf[i];
        }
        else
            gx = gy = -1.0f;

        // 5. MIDI mode: the note envelope gates the sound.
        if (midi)
        {
            for (int i = 0; i < produced; ++i)
            {
                const float g = env.getNextSample() * noteGain;
                for (int c = 0; c < fmt.channels; ++c)
                    mixBuf[(size_t) c * (size_t) produced + (size_t) i] *= g;
            }
        }
        else if (env.isActive())
            env.reset();

        // 6. LFE: keep only the lows (Linkwitz-Riley, 24 dB/octave at 120 Hz).
        {
            const int lfeCh = fmt.lfe && fmt.layout != nullptr ? fmt.layout->lfeChannel : -1;
            if (lfeCh != lfeFilterChannel || hostRate != lfeFilterRate)
            {
                lfeFilterChannel = lfeCh;
                lfeFilterRate = hostRate;
                const double w = std::tan (juce::MathConstants<double>::pi * 120.0 / hostRate);
                const double q = std::sqrt (0.5), n = 1.0 / (1.0 + w / q + w * w);
                for (auto& b : lfeFilter)
                {
                    b.b0 = (float) (w * w * n); b.b1 = 2.0f * b.b0; b.b2 = b.b0;
                    b.a1 = (float) (2.0 * (w * w - 1.0) * n); b.a2 = (float) ((1.0 - w / q + w * w) * n);
                    b.z1 = b.z2 = 0.0f;
                }
            }
            if (lfeCh >= 0)
            {
                float* x = mixBuf.data() + (size_t) lfeCh * (size_t) produced;
                for (int i = 0; i < produced; ++i)
                {
                    float v = x[i];
                    for (auto& b : lfeFilter)
                    {
                        const float y = b.b0 * v + b.z1;
                        b.z1 = b.b1 * v - b.a1 * y + b.z2;
                        b.z2 = b.b2 * v - b.a2 * y;
                        v = y;
                    }
                    x[i] = v;
                }
            }
        }
        producedTotal += produced;

        int offset = 0;
        while (offset < produced && running.load())
        {
            const int space = fifo.getFreeSpace();
            if (space <= 0) { std::this_thread::sleep_for (std::chrono::milliseconds (1)); continue; }
            const int n = juce::jmin (space, produced - offset);
            int s1, n1, s2, n2;
            fifo.prepareToWrite (n, s1, n1, s2, n2);
            for (int c = 0; c < NC; ++c)
            {
                const float* src = mixBuf.data() + (size_t) c * (size_t) produced + (size_t) offset;
                if (n1 > 0) fifoBuffer.copyFrom (c, s1, src, n1);
                if (n2 > 0) fifoBuffer.copyFrom (c, s2, src + n1, n2);
            }
            fifo.finishedWrite (n1 + n2);
            offset += n1 + n2;
        }
    }
}

//==============================================================================
void LatentEngine::runOnImportThread (std::function<void()> job)
{
    ++busyJobs;
    importPool.addJob ([this, job]
    {
        try { job(); }
        catch (const std::exception& e) { setStatus (e.what()); }
        --busyJobs;
    });
}

void LatentEngine::setStatus (const juce::String& s)
{
    std::lock_guard<std::mutex> lock (snapMutex);
    status = s;
}

juce::String LatentEngine::getStatus() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return status;
}

juce::String LatentEngine::getModelDescription() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return modelDescription;
}

juce::String LatentEngine::getModelPath() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return modelPath;
}

juce::StringArray LatentEngine::getFilePaths() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return filePaths;
}

int LatentEngine::getColourIndex (const juce::String& path) const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    auto it = colourOf.find (path);
    return it != colourOf.end() ? it->second : 0;
}

void LatentEngine::assignColourLocked (const juce::String& path)
{
    if (colourOf.find (path) == colourOf.end())
        colourOf[path] = nextColour++;
}

std::shared_ptr<const Corpus> LatentEngine::getCorpus() const
{
    std::lock_guard<std::mutex> lock (snapMutex);
    return corpus;
}

void LatentEngine::publishCorpus (std::shared_ptr<Corpus> c)
{
    std::shared_ptr<const Corpus> published = c;
    {
        std::lock_guard<std::mutex> lock (snapMutex);
        corpus = std::move (c);
    }
    ++corpusVersion;
    startTraining (published != nullptr && published->modelLatents ? published : nullptr);
}

//==============================================================================
void LatentEngine::loadModelAsync (const juce::File& modelFile)
{
    runOnImportThread ([this, modelFile]
    {
        setStatus ("Loading " + modelFile.getFileName() + "...");
        auto enc = std::make_shared<RaveModel>();
        auto dec = std::make_shared<RaveModel>();
        try
        {
            enc->load (modelFile.getFullPathName().toStdString());
            dec->load (modelFile.getFullPathName().toStdString());
        }
        catch (const std::exception& e)
        {
            setStatus (e.what());
            return;
        }

        juce::String desc;
        desc << modelFile.getFileNameWithoutExtension() << "  |  " << dec->getLatentSize() << " dims, "
             << dec->getSampleRate() << " Hz" << (dec->sampleRateIsKnown() ? "" : " (assumed)")
             << ", " << dec->getOutputChannels() << " ch";

        encoder = enc;
        noModel = false;
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            decoder = dec;
            modelPath = modelFile.getFullPathName();
            modelDescription = desc;
        }
        reencodeAll();
    });
}

void LatentEngine::restoreAsync (const juce::String& path, const juce::StringArray& paths, bool withoutModel, int lx, int ly)
{
    {
        std::lock_guard<std::mutex> lock (snapMutex);
        filePaths = paths;
        for (auto& p : paths)
            assignColourLocked (p);
    }
    layoutAxisX = juce::jlimit (0, 4, lx);
    layoutAxisY = juce::jlimit (0, 4, ly);
    ++fileListVersion;
    if (path.isNotEmpty() && juce::File (path).existsAsFile())
        loadModelAsync (juce::File (path));
    else if (path.isNotEmpty())
        setStatus ("Model not found: " + path);
    else if (withoutModel)
        useWithoutModelAsync();
}

void LatentEngine::prepareCorpus (Corpus& c) const
{
    c.dim = encoder != nullptr ? encoder->getLatentSize() : Corpus::numDesc;
    c.modelLatents = encoder != nullptr;
    c.layoutX = layoutAxisX.load();
    c.layoutY = layoutAxisY.load();
}

void LatentEngine::useWithoutModelAsync()
{
    runOnImportThread ([this]
    {
        encoder = nullptr;
        noModel = true;
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            decoder = nullptr;
            modelPath = {};
            modelDescription = {};
        }
        reencodeAll();
    });
}

void LatentEngine::setLayoutAxesAsync (int x, int y)
{
    layoutAxisX = juce::jlimit (0, 4, x);
    layoutAxisY = juce::jlimit (0, 4, y);
    runOnImportThread ([this]
    {
        auto previous = getCorpus();
        if (previous == nullptr)
            return;
        auto next = std::make_shared<Corpus> (*previous);
        next->layoutX = layoutAxisX.load();
        next->layoutY = layoutAxisY.load();
        next->computeLayout();
        publishCorpus (next);
    });
}

void LatentEngine::addFilesAsync (const juce::StringArray& paths)
{
    runOnImportThread ([this, paths]
    {
        juce::StringArray added;
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            for (auto& p : paths)
                if (! filePaths.contains (p)) { filePaths.add (p); added.add (p); assignColourLocked (p); }
        }
        ++fileListVersion;
        if (encoder == nullptr && ! noModel.load())
        {
            setStatus (juce::String (added.size()) + " file(s) waiting: choose a model, or play without one");
            return;
        }

        auto previous = getCorpus();
        auto next = previous != nullptr ? std::make_shared<Corpus> (*previous) : std::make_shared<Corpus>();
        prepareCorpus (*next);

        juce::StringArray errors;
        for (auto& p : added)
            encodeInto (*next, p, errors);
        next->computeLayout();
        const int n = next->size();
        publishCorpus (next);
        setStatus (juce::String (n) + " frames on the map" + (errors.isEmpty() ? juce::String() : "  |  " + errors.joinIntoString ("; ")));
    });
}

void LatentEngine::clearCorpus()
{
    runOnImportThread ([this]
    {
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            filePaths.clear();
        }
        ++fileListVersion;
        publishCorpus (nullptr);
        setStatus ("Map cleared: drop audio files on it");
    });
}

void LatentEngine::removeFileAsync (const juce::String& path)
{
    runOnImportThread ([this, path]
    {
        {
            std::lock_guard<std::mutex> lock (snapMutex);
            filePaths.removeString (path);
        }
        ++fileListVersion;

        auto previous = getCorpus();
        if (previous == nullptr)
        {
            setStatus (juce::File (path).getFileName() + " removed");
            return;
        }

        // Rebuild the corpus without that file's frames and re-index the sources.
        auto next = std::make_shared<Corpus>();
        next->dim = previous->dim;
        next->modelLatents = previous->modelLatents;
        next->unitSeconds = previous->unitSeconds;
        next->layoutX = previous->layoutX;
        next->layoutY = previous->layoutY;
        std::vector<int> remap (previous->sources.size(), -1);
        for (size_t i = 0; i < previous->sources.size(); ++i)
        {
            if (juce::String (previous->sources[i].path) == path)
                continue;
            remap[i] = (int) next->sources.size();
            next->sources.push_back (previous->sources[i]);
            next->audio.push_back (i < previous->audio.size() ? previous->audio[i] : nullptr);
        }
        const bool hasDesc = previous->desc.size() == (size_t) previous->size() * Corpus::numDesc;
        const int dim = previous->dim;
        for (int i = 0; i < previous->size(); ++i)
        {
            const int src = remap[(size_t) previous->sourceIndex[(size_t) i]];
            if (src < 0)
                continue;
            next->latents.insert (next->latents.end(), previous->latents.begin() + i * dim, previous->latents.begin() + (i + 1) * dim);
            next->sourceIndex.push_back (src);
            next->frameTime.push_back (previous->frameTime[(size_t) i]);
            if (hasDesc)
                next->desc.insert (next->desc.end(), previous->desc.begin() + i * Corpus::numDesc, previous->desc.begin() + (i + 1) * Corpus::numDesc);
        }
        next->computeLayout();
        const int n = next->size();
        publishCorpus (n > 0 || ! next->sources.empty() ? next : nullptr);
        setStatus (juce::File (path).getFileName() + " removed  |  " + juce::String (n) + " frames on the map");
    });
}

void LatentEngine::reencodeAll()
{
    auto next = std::make_shared<Corpus>();
    prepareCorpus (*next);
    juce::StringArray errors;
    for (auto& p : getFilePaths())
        encodeInto (*next, p, errors);
    next->computeLayout();
    const int n = next->size();
    publishCorpus (next);
    setStatus ((n > 0 ? juce::String (n) + " frames on the map"
                      : juce::String (encoder != nullptr ? "Model loaded: drop audio files on the map" : "No model: drop audio files on the map"))
               + (errors.isEmpty() ? juce::String() : "  |  " + errors.joinIntoString ("; ")));
}

void LatentEngine::encodeInto (Corpus& next, const juce::String& path, juce::StringArray& errors)
{
    juce::File f (path);
    setStatus ((encoder != nullptr ? "Encoding " : "Analysing ") + f.getFileName() + "...");
    next.sources.push_back ({ f.getFileName().toStdString(), path.toStdString(), getColourIndex (path) });
    juce::String err;
    try
    {
        if (! encodeFile (encoder.get(), f, next, (int) next.sources.size() - 1, err))
            errors.add (err);
    }
    catch (const std::exception& e)
    {
        errors.add (f.getFileName() + ": " + e.what());
    }
}

bool LatentEngine::encodeFile (RaveModel* enc, const juce::File& file, Corpus& into, int sourceIdx, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or missing file: " + file.getFileName();
        return false;
    }

    const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 60 * 20);
    if (juce::isPositiveAndBelow (sourceIdx, (int) into.sources.size()))
        into.sources[(size_t) sourceIdx].durationSec = (float) (len / reader->sampleRate);
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);

    // Keep the original audio for concatenative playback.
    {
        auto a = std::make_shared<SourceAudio>();
        a->rate = reader->sampleRate;
        a->stereo = buf.getNumChannels() > 1;
        a->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
        a->right.assign (buf.getReadPointer (juce::jmin (1, buf.getNumChannels() - 1)), buf.getReadPointer (juce::jmin (1, buf.getNumChannels() - 1)) + len);
        if (into.audio.size() < into.sources.size())
            into.audio.resize (into.sources.size());
        if (juce::isPositiveAndBelow (sourceIdx, (int) into.audio.size()))
            into.audio[(size_t) sourceIdx] = a;
    }

    std::vector<float> mono ((size_t) len, 0.0f);
    for (int src = 0; src < buf.getNumChannels(); ++src)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (src), 1.0f / (float) buf.getNumChannels(), len);

    Analysis::Analyzer analyzer;
    auto pushDesc = [&into] (const Analysis::Descriptor& d)
    {
        into.desc.push_back (d.pitch);
        into.desc.push_back (d.clarity);
        into.desc.push_back (d.brightness);
        into.desc.push_back (d.noisiness);
        into.desc.push_back (d.loudness);
    };

    if (enc == nullptr)
    {
        // No model: one unit every ~23 ms, described by its audio descriptors.
        const double sr = reader->sampleRate;
        const int hop = juce::jmax (64, (int) std::round (0.0232 * sr));
        const int window = juce::jmin (4096, 2 * hop);
        into.unitSeconds = (float) (hop / sr);
        if (len < window)
        {
            error = file.getFileName() + " is too short";
            return false;
        }
        for (int start = 0; start + window <= len; start += hop)
        {
            const float* x = mono.data() + start;
            double sum = 0.0;
            for (int k = 0; k < hop; ++k) sum += x[k] * x[k];
            if (std::sqrt (sum / hop) < silenceThreshold)
                continue;
            const auto d = analyzer.analyse (x, window, sr);
            // the "latents" of a model-free corpus are the descriptors themselves
            into.latents.push_back (d.clarity > 0.6f ? d.pitch : 0.0f);
            into.latents.push_back (d.clarity);
            into.latents.push_back (d.brightness);
            into.latents.push_back (d.noisiness);
            into.latents.push_back (d.loudness);
            pushDesc (d);
            into.sourceIndex.push_back (sourceIdx);
            into.frameTime.push_back ((float) (start / sr));
        }
        return true;
    }

    const int inCh = enc->getInputChannels();
    const int ratio = enc->getRatio();
    const int dim = enc->getLatentSize();
    into.unitSeconds = (float) ((double) ratio / enc->getSampleRate());

    // Build the model's input channels (mono mixdown, or L/R for stereo models).
    std::vector<std::vector<float>> chans ((size_t) inCh);
    for (int c = 0; c < inCh; ++c)
    {
        if (inCh == 1)
            chans[(size_t) c] = resample (mono, reader->sampleRate, enc->getSampleRate());
        else
        {
            std::vector<float> s (buf.getReadPointer (juce::jmin (c, buf.getNumChannels() - 1)),
                                  buf.getReadPointer (juce::jmin (c, buf.getNumChannels() - 1)) + len);
            chans[(size_t) c] = resample (s, reader->sampleRate, enc->getSampleRate());
        }
    }

    const int total = (int) chans[0].size();
    const int frames = total / ratio;
    if (frames < 1)
    {
        error = file.getFileName() + " is too short";
        return false;
    }

    // Flush the streaming state left by the previous file.
    {
        std::vector<std::vector<float>> zeros ((size_t) inCh, std::vector<float> ((size_t) ratio * 4, 0.0f));
        enc->encode (zeros);
    }

    const int framesPerChunk = juce::jmax (1, 16384 / ratio);
    for (int f0 = 0; f0 < frames; f0 += framesPerChunk)
    {
        const int nf = juce::jmin (framesPerChunk, frames - f0);
        std::vector<std::vector<float>> chunk ((size_t) inCh);
        for (int c = 0; c < inCh; ++c)
            chunk[(size_t) c].assign (chans[(size_t) c].begin() + f0 * ratio, chans[(size_t) c].begin() + (f0 + nf) * ratio);

        auto z = enc->encode (chunk);
        const int gotFrames = (int) (z.size() / (size_t) dim);

        for (int i = 0; i < gotFrames; ++i)
        {
            // Skip silent frames so silence doesn't take up space on the map.
            const float* s = chunk[0].data() + i * ratio;
            double sum = 0.0;
            for (int k = 0; k < ratio; ++k) sum += s[k] * s[k];
            if (std::sqrt (sum / ratio) < silenceThreshold)
                continue;

            into.latents.insert (into.latents.end(), z.begin() + i * dim, z.begin() + (i + 1) * dim);
            pushDesc (analyzer.analyse (s, juce::jmin (ratio, 4096), enc->getSampleRate()));
            into.sourceIndex.push_back (sourceIdx);
            into.frameTime.push_back ((float) ((double) (f0 + i) * ratio / enc->getSampleRate()));
        }
    }
    return true;
}
