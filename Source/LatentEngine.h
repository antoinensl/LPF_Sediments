#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <map>

#include "Corpus.h"
#include "RaveModel.h"
#include "LearnedTerrain.h"
#include "GrainPlayer.h"
#include "Spatial.h"

// Everything that isn't UI or plugin plumbing:
//  - an import thread that loads models and encodes dropped files into the corpus,
//  - a synthesis thread that reads the map position, queries the terrain,
//    decodes with RAVE and fills a lock-free FIFO at the host sample rate,
//  - render(), called on the audio thread, which only reads that FIFO.
class LatentEngine
{
public:
    LatentEngine();
    ~LatentEngine();

    // Controls, written by the processor from its parameters.
    std::atomic<float> posX { 0.5f }, posY { 0.5f }, blur { 0.3f }, glide { 0.4f };
    // 0 = mix of the nearest corpus frames, 1 = learned terrain (when trained)
    std::atomic<float> terrainMix { 0.0f };

    // Flow: instead of a frozen snapshot, follow how the original sound moved in
    // time from the point under the cursor. 0 = off, 1 = full.
    std::atomic<float> flow { 0.0f }, flowSpeed { 1.0f };

    // Path (recorded gesture)
    enum class PathDir { forward = 0, reverse, pingPong };
    std::atomic<bool>  pathOn { false }, pathFollows { false };
    std::atomic<int>   pathSyncBeats { 0 };      // 0 = free (recorded length x speed)
    std::atomic<float> pathSpeed { 1.0f };
    std::atomic<int>   pathDir { 0 };

    // MIDI mode: notes choose zones and gate the sound.
    std::atomic<bool>  midiMode { false };
    std::atomic<float> spray { 0.0f }, attackSec { 0.01f }, releaseSec { 0.4f };

    // Original sounds (concatenative playback): 0 = all model, 1 = all originals.
    // Without a model, the originals are always what plays.
    std::atomic<float> sourceMix { 0.0f }, grainMs { 120.0f };

    // Space: output format (see Spatial::outputNames) and how grains are placed.
    std::atomic<int>   outputChoice { 0 }, placeBy { 0 }, heightMode { 0 };
    std::atomic<bool>  frontCentre { false }, useCentre { true }, lfeOn { false };
    std::atomic<float> spaceWidth { 1.0f }, scatter { 0.0f }, rotateSpeed { 0.0f };   // rotation in turns per second
    std::atomic<int>   hostOutputChannels { 2 };    // what the host gives us
    // "Sound plays": when false, the synthesised sound fades out (the engine keeps running).
    std::atomic<bool>  gateOpen { true };
    std::atomic<double> hostRateForGate { 48000.0 };
    float gateGain = 1.0f;                           // audio thread only
    // Per-sound direction (degrees, 0 = front, clockwise) for "Each sound".
    void setSoundDirections (const std::map<juce::String, float>& directions);

    // Recent grain places, for the display (written by the synthesis thread).
    struct GrainMark { std::atomic<float> az { 0 }, el { 0 }, dist { 0 }; std::atomic<int> source { -1 }; std::atomic<juce::uint32> when { 0 }; };
    static constexpr int numGrainMarks = 64;
    GrainMark grainMarks[numGrainMarks];
    std::atomic<float> modelAz { 0 }, modelEl { 0 }, modelDist { 0 };
    std::atomic<bool>  modelPlaceActive { false };

    // Where the sound actually is right now (after path / MIDI), for the UI.
    std::atomic<float> curX { 0.5f }, curY { 0.5f }, baseX { 0.5f }, baseY { 0.5f };
    std::atomic<int>   activeZone { -1 };
    std::atomic<float> pathPhase { -1.0f };      // < 0 when no path is playing

    struct PathPoint { float t, x, y; };          // t in seconds from the start
    void setPath (std::vector<PathPoint> points);  // empty = clear
    std::shared_ptr<const std::vector<PathPoint>> getPath() const;
    int getPathVersion() const noexcept { return pathVersion.load(); }

    // Audio thread: MIDI notes, and the host transport for tempo sync.
    void pushNote (int note, float velocity, bool isOn) noexcept;
    // Message thread: notes from the on-screen keyboard.
    void pushNoteFromUI (int note, float velocity, bool isOn) noexcept;

    // Listening to the original sounds (not through the model).
    // durationSec <= 0 plays the file from startSec to its end (at most 60 s).
    void auditionAsync (const juce::String& path, double startSec, double durationSec);
    void stopAudition();
    juce::String getAuditionPath() const;      // empty when nothing is playing
    void setTransport (bool playing, double ppq, double bpm) noexcept;

    // Host side --------------------------------------------------------------
    void prepare (double hostSampleRate, int maxBlockSize);
    void release();
    // Adds generated audio (times gain) into the buffer. Audio thread only.
    void render (juce::AudioBuffer<float>& buffer, int numSamples, float gain) noexcept;

    // Message thread actions (work happens on the import thread) -------------
    void loadModelAsync (const juce::File& modelFile);
    void addFilesAsync (const juce::StringArray& paths);
    void clearCorpus();
    // Removes one file (and its points) from the map. No re-encoding needed.
    void removeFileAsync (const juce::String& path);
    // Used when restoring a saved session: sets the file list, then loads the model
    // (which re-encodes every file).
    void restoreAsync (const juce::String& modelPath, const juce::StringArray& filePaths,
                       bool withoutModel = false, int layoutX = 0, int layoutY = 0);
    // Play the original sounds only, with no model: files are analysed with audio
    // descriptors (pitch, brightness, noisiness, loudness) to draw the map.
    void useWithoutModelAsync();
    bool isWithoutModel() const noexcept { return noModel.load(); }
    // Map axes: 0 similarity, 1 pitch, 2 brightness, 3 noisiness, 4 loudness
    void setLayoutAxesAsync (int x, int y);
    int getLayoutX() const noexcept { return layoutAxisX.load(); }
    int getLayoutY() const noexcept { return layoutAxisY.load(); }

    // Info for the UI and for saving state ----------------------------------
    std::shared_ptr<const Corpus> getCorpus() const;
    int getCorpusVersion() const noexcept { return corpusVersion.load(); }
    juce::String getStatus() const;
    juce::String getModelDescription() const;
    juce::String getModelPath() const;
    juce::StringArray getFilePaths() const;
    int getFileListVersion() const noexcept { return fileListVersion.load(); }
    int getColourIndex (const juce::String& path) const;   // stable per file
    bool isBusy() const noexcept { return busyJobs.load() > 0; }

    // Learned terrain: retrained in the background each time the corpus changes.
    std::shared_ptr<const LearnedTerrain> getLearnedTerrain() const;
    int getLearnedVersion() const noexcept { return learnedVersion.load(); }
    float getTrainingProgress() const noexcept { return trainProgress.load(); }  // < 0 when idle
    bool isTraining() const noexcept { return trainProgress.load() >= 0.0f; }

    // Offline helper (also used by the command-line test): encode one file.
    // With encoder == nullptr the file is analysed with descriptors only.
    static bool encodeFile (RaveModel* encoder, const juce::File& file, Corpus& into,
                            int sourceIdx, juce::String& error);

private:
    // Import thread
    juce::ThreadPool importPool { juce::ThreadPoolOptions{}.withThreadName ("Sediments import").withNumberOfThreads (1) };
    std::shared_ptr<RaveModel> encoder;              // only touched on the import thread
    void runOnImportThread (std::function<void()> job);
    void reencodeAll();
    void encodeInto (Corpus& next, const juce::String& path, juce::StringArray& errors);
    void publishCorpus (std::shared_ptr<Corpus> c);
    void setStatus (const juce::String& s);

    // Shared snapshots
    mutable std::mutex snapMutex;
    std::shared_ptr<RaveModel> decoder;
    std::shared_ptr<const Corpus> corpus;
    juce::StringArray filePaths;
    juce::String modelPath, modelDescription, status { "Load a RAVE model (.ts) to start" };
    std::atomic<int> corpusVersion { 0 }, busyJobs { 0 }, fileListVersion { 0 };
    std::atomic<bool> noModel { false };
    std::atomic<int> layoutAxisX { 0 }, layoutAxisY { 0 };
    void prepareCorpus (Corpus& c) const;
    std::map<juce::String, int> colourOf;   // guarded by snapMutex
    int nextColour = 0;
    void assignColourLocked (const juce::String& path);

    // Path
    std::shared_ptr<const std::vector<PathPoint>> path;   // guarded by snapMutex
    std::atomic<int> pathVersion { 0 };

    // Notes: audio thread -> synthesis thread
    struct NoteEvent { int note; float velocity; bool on; };
    static constexpr int noteFifoSize = 512;
    juce::AbstractFifo noteFifo { noteFifoSize };
    NoteEvent noteEvents[noteFifoSize];

    juce::AbstractFifo uiNoteFifo { noteFifoSize };
    NoteEvent uiNoteEvents[noteFifoSize];

    struct AuditionClip
    {
        juce::AudioBuffer<float> buffer;      // stereo, host rate
        juce::String path;
        int pos = 0;                          // audio thread only
        std::atomic<bool> finished { false }, stopping { false };
        float stopGain = 1.0f;                // audio thread only
    };
    std::unique_ptr<AuditionClip> audition;   // swapped under auditionLock
    mutable juce::SpinLock auditionLock;
    juce::ThreadPool auditionPool { juce::ThreadPoolOptions{}.withThreadName ("Sediments audition").withNumberOfThreads (1) };
    std::atomic<int> auditionRequest { 0 };

    // Transport stamp: at host sample `consumedStamp` the host was at `ppqStamp`.
    std::atomic<juce::int64> consumedTotal { 0 }, consumedStamp { 0 };
    std::atomic<double> ppqStamp { 0.0 }, bpmStamp { 120.0 };
    std::atomic<bool> hostPlaying { false };

    // Learned terrain training thread
    std::shared_ptr<const LearnedTerrain> learned;   // guarded by snapMutex
    std::thread trainer;
    std::mutex trainerMutex;
    std::atomic<int> trainGeneration { 0 }, learnedVersion { 0 };
    std::atomic<float> trainProgress { -1.0f };
    void startTraining (std::shared_ptr<const Corpus> c);
    void stopTraining();

    // Synthesis thread
    std::thread worker;
    std::atomic<bool> running { false };
    double hostRate = 48000.0;
    int maxBlock = 512;
    void workerLoop();
    void startWorker();
    void stopWorker();

    // FIFO between synthesis thread and audio thread (up to 16 channels, host rate)
    static constexpr int fifoCapacity = 1 << 15;
    juce::AbstractFifo fifo { fifoCapacity };
    juce::AudioBuffer<float> fifoBuffer { Spatial::maxChannels, fifoCapacity };

    // Space
    std::map<juce::String, float> soundDirections;   // guarded by snapMutex
    std::atomic<int> soundDirVersion { 0 };
    std::atomic<int> grainMarkNext { 0 };
    std::atomic<juce::uint32> lastGrainMark { 0 };
    void publishGrain (const Spatial::Place&, int source) noexcept;
    void publishModelPlace (const Spatial::Place&) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatentEngine)
};
