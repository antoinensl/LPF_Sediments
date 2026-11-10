// Command-line test of the engine core, without a DAW:
//   sediments_cli model.ts out.wav file1.wav [file2.wav ...]
// Encodes the files, lays them out on the map, then renders a 10 s circular
// walk through the terrain using the same render path as the plugin.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/LatentEngine.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <algorithm>
#include <cmath>

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "usage: sediments_cli model.ts out.wav input1.wav [input2.wav ...]\n";
        return 1;
    }

    LatentEngine engine;
    const double hostRate = 48000.0;
    const int block = 512;
    engine.prepare (hostRate, block);

    juce::StringArray inputs;
    for (int i = 3; i < argc; ++i)
        inputs.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());

    if (juce::String (argv[1]) == "none")
        engine.useWithoutModelAsync();
    else
        engine.loadModelAsync (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    if (auto* v = std::getenv ("LT_SOURCE")) engine.sourceMix = (float) std::atof (v);
    if (auto* v = std::getenv ("LT_FLOW")) engine.flow = (float) std::atof (v);
    if (auto* v = std::getenv ("LT_LAYOUT")) { const auto xy = juce::StringArray::fromTokens (v, ",", ""); engine.setLayoutAxesAsync (xy[0].getIntValue(), xy[1].getIntValue()); }
    engine.addFilesAsync (inputs);

    while (engine.isBusy())
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

    std::cout << "model:  " << engine.getModelDescription() << "\n";
    std::cout << "status: " << engine.getStatus() << "\n";
    auto corpus = engine.getCorpus();
    if (corpus == nullptr || corpus->size() == 0)
    {
        std::cerr << "empty corpus\n";
        return 2;
    }

    if (std::getenv ("LT_DUMP_DESC") != nullptr && corpus->desc.size() == (size_t) corpus->size() * Corpus::numDesc)
    {
        for (size_t si = 0; si < corpus->sources.size(); ++si)
        {
            std::vector<float> pitches; double cl = 0, br = 0, no = 0, lo = 0; int cnt = 0;
            for (int i = 0; i < corpus->size(); ++i)
                if (corpus->sourceIndex[(size_t) i] == (int) si)
                {
                    const float* d = corpus->desc.data() + (size_t) i * Corpus::numDesc;
                    if (d[1] > 0.6f) pitches.push_back (d[0]);
                    cl += d[1]; br += d[2]; no += d[3]; lo += d[4]; ++cnt;
                }
            std::sort (pitches.begin(), pitches.end());
            std::printf ("%-12s frames %4d  pitch(median midi) %6.2f  clarity %.2f  centroid %7.0f Hz  noisiness %.3f  loudness %6.1f dB  unit %.4fs\n",
                         corpus->sources[si].name.c_str(), cnt, pitches.empty() ? -1.0f : pitches[pitches.size() / 2], cl / cnt,
                         std::pow (2.0, br / cnt), no / cnt, lo / cnt, corpus->unitSeconds);
        }
    }

    // LT_TERRAIN=0..1 sets the learned-terrain mix; LT_WAIT_TRAIN=1 waits for training first.
    if (auto* v = std::getenv ("LT_TERRAIN")) engine.terrainMix = (float) std::atof (v);
    auto trainStart = std::chrono::steady_clock::now();
    if (std::getenv ("LT_WAIT_TRAIN") != nullptr)
    {
        while (engine.isTraining()) std::this_thread::sleep_for (std::chrono::milliseconds (20));
        auto lt = engine.getLearnedTerrain();
        std::cout << "terrain trained in " << std::chrono::duration<double> (std::chrono::steady_clock::now() - trainStart).count()
                  << " s, final loss " << (lt ? lt->finalLoss : -1.0f) << "\n";
    }
    else
        std::cout << "rendering while training: " << engine.isTraining() << "\n";

    // Render 10 s, moving in a circle, in real-time-sized blocks.
    const int total = (int) (hostRate * 10);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    juce::AudioBuffer<float> blk (2, block);
    int underruns = 0;
    auto t0 = std::chrono::steady_clock::now();
    // LT_TEST selects a scenario: "midi", "path", "flow", "static" (default: circular walk)
    const juce::String test = std::getenv ("LT_TEST") != nullptr ? juce::String (std::getenv ("LT_TEST")) : juce::String();
    if (test == "midi")
    {
        engine.midiMode = true;
        engine.attackSec = 0.005f;
        engine.releaseSec = 0.15f;
        engine.spray = 0.0f;
        engine.glide = 0.0f;
    }
    if (test == "path")
    {
        std::vector<LatentEngine::PathPoint> pts;
        for (int i = 0; i <= 100; ++i)
        {
            const float t = i / 100.0f;
            pts.push_back ({ t * 1.0f, 0.5f + 0.35f * std::cos (6.2831853f * t), 0.5f + 0.35f * std::sin (6.2831853f * t) });
        }
        engine.setPath (pts);
        engine.pathOn = true;
        engine.pathSyncBeats = 4;     // 4 beats at 120 bpm = a 2 s loop
    }
    if (test == "flow" || test == "static")
    {
        engine.flow = test == "flow" ? 1.0f : 0.0f;
        engine.blur = 0.05f;
    }
    std::FILE* trace = std::fopen ("trace.csv", "w");

    for (int pos = 0; pos < total; pos += block)
    {
        const double t = pos / hostRate;
        if (test.isEmpty())
        {
            engine.posX = (float) (0.5 + 0.4 * std::cos (t * 0.6));
            engine.posY = (float) (0.5 + 0.4 * std::sin (t * 0.6));
        }
        else
        {
            engine.posX = 0.3f;
            engine.posY = 0.3f;
        }
        engine.setTransport (true, t * 2.0, 120.0);   // 120 bpm, playing from 0

        if (test == "midi")
        {
            // a note every 0.75 s: 0.5 s held, 0.25 s silence; C, D, E, F#, G#, A#, C...
            const int k = (int) (t / 0.75), kPrev = (int) ((pos - block) / hostRate / 0.75);
            const double inNote = t - k * 0.75, prevIn = (pos - block) / hostRate - kPrev * 0.75;
            if (pos == 0 || k != kPrev) engine.pushNote (60 + 2 * k, 0.9f, true);
            if (inNote >= 0.5 && prevIn < 0.5) engine.pushNote (60 + 2 * k, 0.0f, false);
        }

        // pace like a real audio callback
        auto due = t0 + std::chrono::microseconds ((long long) (1e6 * pos / hostRate));
        std::this_thread::sleep_until (due);

        blk.clear();
        engine.render (blk, block, 1.0f);
        if (blk.getMagnitude (0, block) == 0.0f && pos > hostRate * 0.5 && test != "midi")
            ++underruns;
        const int n = juce::jmin (block, total - pos);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, blk, c, 0, n);
        std::fprintf (trace, "%f,%f,%f,%d,%f\n", t, engine.curX.load(), engine.curY.load(), engine.activeZone.load(), engine.pathPhase.load());
    }
    std::fclose (trace);

    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (outFile), hostRate, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, total);
    w.reset();

    std::cout << "frames on map: " << corpus->size() << ", sources: " << corpus->sources.size()
              << ", silent blocks after warm-up: " << underruns
              << ", peak: " << out.getMagnitude (0, total) << "\n";
    engine.release();
    return 0;
}
