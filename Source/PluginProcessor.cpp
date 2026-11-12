#include "PluginProcessor.h"
#include "PluginEditor.h"

SedimentsProcessor::SedimentsProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          // 16 outputs: enough for a ring of 16 speakers or 3rd-order Ambisonics.
                          // On a stereo track only the first two are used.
                          .withOutput ("Output", juce::AudioChannelSet::discreteChannels (Spatial::maxChannels), true)),
      params (*this, nullptr, "Sediments", createLayout())
{
    pX     = params.getRawParameterValue ("x");
    pY     = params.getRawParameterValue ("y");
    pBlur  = params.getRawParameterValue ("blur");
    pGlide = params.getRawParameterValue ("glide");
    pTerrain = params.getRawParameterValue ("terrain");
    pGain  = params.getRawParameterValue ("gain");
    pDry   = params.getRawParameterValue ("dry");
    for (auto* id : { "source", "grain", "flow", "flowSpeed", "pathOn", "pathSync", "pathSpeed", "pathDir", "pathAnchor",
                      "midiMode", "spray", "attack", "release",
                      "output", "ringStart", "place", "height", "width", "scatter", "rotate" })
        raw[id] = params.getRawParameterValue (id);
}

SedimentsProcessor::~SedimentsProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout SedimentsProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "x", 1 }, "X", 0.0f, 1.0f, 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "y", 1 }, "Y", 0.0f, 1.0f, 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "blur", 1 }, "Focus", 0.0f, 1.0f, 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "terrain", 1 }, "Terrain", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                    {
                        if (v < 0.005f) return String ("Your sounds");
                        if (v > 0.995f) return String ("Learned");
                        return String (juce::roundToInt (v * 100.0f)) + " % learned";
                    })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide", 0.0f, 1.0f, 0.35f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain",
                                                       NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dry", 1 }, "Input", 0.0f, 1.0f, 0.0f));

    // Original sounds (concatenative playback)
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "source", 1 }, "Source", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                    {
                        if (v < 0.005f) return String ("Model");
                        if (v > 0.995f) return String ("Originals");
                        return String (roundToInt (v * 100.0f)) + " % originals";
                    })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain", 1 }, "Grain size",
                    NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), 120.0f,
                    AudioParameterFloatAttributes().withLabel ("ms")));

    // Flow
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "flow", 1 }, "Flow", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "flowSpeed", 1 }, "Flow speed",
                    NormalisableRange<float> (0.1f, 4.0f, 0.0f, 0.5f), 1.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " x"; })));

    // Path
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "pathOn", 1 }, "Path play", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pathSync", 1 }, "Path sync",
                    StringArray { "Free", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "pathSpeed", 1 }, "Path speed",
                    NormalisableRange<float> (0.125f, 8.0f, 0.0f, 0.4f), 1.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " x"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pathDir", 1 }, "Path direction",
                    StringArray { "Forward", "Backward", "Back and forth" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pathAnchor", 1 }, "Path anchor",
                    StringArray { "Fixed", "Follow the cursor" }, 0));

    // MIDI
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "midiMode", 1 }, "MIDI notes", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "spray", 1 }, "Spray", 0.0f, 1.0f, 0.3f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack",
                    NormalisableRange<float> (0.001f, 4.0f, 0.0f, 0.3f), 0.01f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                        { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
                    NormalisableRange<float> (0.01f, 8.0f, 0.0f, 0.3f), 0.5f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                        { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; })));

    // Space
    StringArray outputs;
    for (int i = 0; i < Spatial::numOutputChoices; ++i)
        outputs.add (Spatial::outputNames[i]);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "output", 1 }, "Output", outputs, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ringStart", 1 }, "Speaker 1",
                    StringArray { "Front left", "Front centre" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "place", 1 }, "Place grains by",
                    StringArray { "Centre", "Map", "Each sound", "Pitch", "Brightness", "Noisiness", "Loudness" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "height", 1 }, "Height",
                    StringArray { "Flat", "Dome", "Pitch", "Brightness", "Noisiness", "Loudness" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", 0.0f, 1.0f, 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "scatter", 1 }, "Scatter", 0.0f, 1.0f, 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "rotate", 1 }, "Rotation",
                    NormalisableRange<float> (-1.0f, 1.0f, 0.0f, 0.5f, true), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                    {
                        if (std::abs (v) < 0.002f) return String ("Still");
                        const float rpm = std::abs (v) * 60.0f;
                        return String (rpm, rpm < 10.0f ? 1 : 0) + " turns/min " + (v > 0.0f ? "clockwise" : "anticlockwise");
                    })));
    return layout;
}

void SedimentsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    gainSmooth.reset (sampleRate, 0.05);
    drySmooth.reset (sampleRate, 0.05);
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    drySmooth.setCurrentAndTargetValue (pDry->load());
    engine.prepare (sampleRate, samplesPerBlock);
}

void SedimentsProcessor::releaseResources()
{
    engine.release();
}

bool SedimentsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out.isDisabled() || out.size() > 64)
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SedimentsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    for (int ch = inCh; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    engine.posX  = pX->load();
    engine.posY  = 1.0f - pY->load();   // Y parameter: 1 = top of the map
    engine.blur  = pBlur->load();
    engine.glide = pGlide->load();
    engine.terrainMix = pTerrain->load();

    engine.sourceMix = raw["source"]->load();
    engine.grainMs   = raw["grain"]->load();
    engine.flow      = raw["flow"]->load();
    engine.flowSpeed = raw["flowSpeed"]->load();
    engine.pathOn    = raw["pathOn"]->load() > 0.5f;
    static constexpr int syncBeats[] = { 0, 1, 2, 4, 8, 16 };
    engine.pathSyncBeats = syncBeats[juce::jlimit (0, 5, (int) raw["pathSync"]->load())];
    engine.pathSpeed   = raw["pathSpeed"]->load();
    engine.pathDir     = (int) raw["pathDir"]->load();
    engine.pathFollows = raw["pathAnchor"]->load() > 0.5f;
    engine.midiMode    = raw["midiMode"]->load() > 0.5f;
    engine.spray       = raw["spray"]->load();
    engine.attackSec   = raw["attack"]->load();
    engine.releaseSec  = raw["release"]->load();
    engine.outputChoice = (int) raw["output"]->load();
    engine.frontCentre  = raw["ringStart"]->load() > 0.5f;
    engine.placeBy      = (int) raw["place"]->load();
    engine.heightMode   = (int) raw["height"]->load();
    engine.spaceWidth   = raw["width"]->load();
    engine.scatter      = raw["scatter"]->load();
    engine.rotateSpeed  = raw["rotate"]->load();
    engine.hostOutputChannels = getTotalNumOutputChannels();

    // Host tempo and position, for paths locked to the tempo.
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto b = pos->getBpm()) bpm = *b;
        }
    engine.setTransport (playing, ppq, bpm);

    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            engine.pushNote (m.getNoteNumber(), m.getFloatVelocity(), true);
        else if (m.isNoteOff())
            engine.pushNote (m.getNoteNumber(), 0.0f, false);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (int nn = 0; nn < 128; ++nn)
                engine.pushNote (nn, 0.0f, false);
    }
    midiMessages.clear();

    // Dry input (for later: playing the guitar through the plugin)
    drySmooth.setTargetValue (pDry->load());
    for (int i = 0; i < n; ++i)
    {
        const float d = drySmooth.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] *= d;
    }

    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (pGain->load(), -48.0f));
    const float g = gainSmooth.getNextValue();
    gainSmooth.skip (juce::jmax (0, n - 1));
    engine.render (buffer, n, g);
}

juce::AudioProcessorEditor* SedimentsProcessor::createEditor()
{
    return new SedimentsEditor (*this);
}

void SedimentsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    auto session = state.getOrCreateChildWithName ("Session", nullptr);
    session.removeAllChildren (nullptr);
    session.setProperty ("model", engine.getModelPath(), nullptr);
    session.setProperty ("uiMode", uiMode, nullptr);
    session.setProperty ("noModel", engine.isWithoutModel(), nullptr);
    session.setProperty ("layoutX", engine.getLayoutX(), nullptr);
    session.setProperty ("layoutY", engine.getLayoutY(), nullptr);
    session.setProperty ("uiWidth", uiWidth, nullptr);
    session.setProperty ("uiHeight", uiHeight, nullptr);
    for (auto& f : engine.getFilePaths())
    {
        juce::ValueTree file ("File");
        file.setProperty ("path", f, nullptr);
        auto it = soundColours.find (f);
        if (it != soundColours.end())
            file.setProperty ("colour", it->second.toString(), nullptr);
        auto dir = soundDirections.find (f);
        if (dir != soundDirections.end())
            file.setProperty ("direction", dir->second, nullptr);
        session.appendChild (file, nullptr);
    }
    if (auto p = engine.getPath())
    {
        juce::MemoryBlock mb (p->data(), p->size() * sizeof (LatentEngine::PathPoint));
        session.setProperty ("path", mb.toBase64Encoding(), nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SedimentsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType()))
        return;

    juce::String model;
    juce::StringArray files;
    bool withoutModel = false;
    int lx = 0, ly = 0;
    auto session = state.getChildWithName ("Session");
    if (session.isValid())
    {
        model = session["model"].toString();
        uiMode = juce::jlimit (0, 3, (int) session.getProperty ("uiMode", 0));
        withoutModel = (bool) session.getProperty ("noModel", false);
        lx = (int) session.getProperty ("layoutX", 0);
        ly = (int) session.getProperty ("layoutY", 0);
        uiWidth = juce::jlimit (900, 3000, (int) session.getProperty ("uiWidth", uiWidth));
        uiHeight = juce::jlimit (640, 2000, (int) session.getProperty ("uiHeight", uiHeight));
        soundColours.clear();
        soundDirections.clear();
        for (auto child : session)
            if (child.hasType ("File"))
            {
                files.add (child["path"].toString());
                if (child.hasProperty ("colour"))
                    soundColours[child["path"].toString()] = juce::Colour::fromString (child["colour"].toString());
                if (child.hasProperty ("direction"))
                    soundDirections[child["path"].toString()] = (float) child["direction"];
            }
        juce::MemoryBlock mb;
        if (session.hasProperty ("path") && mb.fromBase64Encoding (session["path"].toString()))
        {
            std::vector<LatentEngine::PathPoint> pts (mb.getSize() / sizeof (LatentEngine::PathPoint));
            std::memcpy (pts.data(), mb.getData(), pts.size() * sizeof (LatentEngine::PathPoint));
            engine.setPath (std::move (pts));
        }
        state.removeChild (session, nullptr);
    }
    params.replaceState (state);
    engine.setSoundDirections (soundDirections);
    engine.restoreAsync (model, files, withoutModel, lx, ly);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SedimentsProcessor();
}
