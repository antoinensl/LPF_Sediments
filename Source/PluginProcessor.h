#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LatentEngine.h"
#include <map>

class SedimentsProcessor : public juce::AudioProcessor
{
public:
    SedimentsProcessor();
    ~SedimentsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sediments"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    LatentEngine engine;
    juce::AudioProcessorValueTreeState params;

    // Interface state saved with the project (message thread only)
    std::map<juce::String, juce::Colour> soundColours;   // custom colour per file
    std::atomic<bool> mapHeld { false };                  // mouse button down on the map ("While clicking")
    std::atomic<bool> keyHeld { false };                  // Enter held on the map cursor (same, from the keyboard)
    std::map<juce::String, float> soundDirections;        // direction per file (degrees), "Each sound"
    void setSoundDirection (const juce::String& path, float degrees)
    {
        soundDirections[path] = degrees;
        engine.setSoundDirections (soundDirections);
    }
    int uiMode = 0;                                       // 0 explore, 1 gesture, 2 keys
    int uiWidth = 1180, uiHeight = 780;                   // window size at 100 % text

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY = nullptr;
    std::atomic<float>* pBlur = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pTerrain = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pDry = nullptr;
    juce::LinearSmoothedValue<float> gainSmooth, drySmooth;
    std::map<juce::String, std::atomic<float>*> raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SedimentsProcessor)
};
