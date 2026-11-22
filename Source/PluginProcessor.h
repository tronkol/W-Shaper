#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "WShaperDSP.h"

// a cappella di riferimento: audio (per la forma d'onda) + modello del timbro
struct RefData
{
    wsk::TextureData audio;
    wsk::TimbreModel model;
};

class WShaperProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    WShaperProcessor();
    ~WShaperProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool loadReference (const juce::File& file);
    void clearReference();
    const RefData* reference() const { return currentRef.load(); }
    int referenceVersion() const { return refVersion.load(); }
    juce::String referenceName() const { return refName; }

    void applyPreset (int index);

    wsk::Vis& vis() { return visData; }
    int latencyMs() const { return (int) std::lround (1000.0 * getLatencySamples() / juce::jmax (1.0, currentSR)); }
    int bandsFor (int res) const { return wsk::Engine::fftSizeFor (currentSR, res) / 2 + 1; }
    int latencyMsFor (int res) const { return (int) std::lround (1000.0 * wsk::Engine::latencyFor (currentSR, res) / juce::jmax (1.0, currentSR)); }
    double sampleRate() const { return currentSR; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    int currentRes() const { return (int) apvts.getRawParameterValue ("res")->load(); }
    void readParams (wsk::Params&);
    void run (juce::AudioBuffer<float>&, bool bypass);
    void rebuildReference();        // solo sul thread dei messaggi
    void requestRebuild();

    wsk::Vis visData;
    std::unique_ptr<wsk::Engine> engine;
    juce::CriticalSection engineLock;
    double currentSR = 48000.0;
    int currentPreset = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain { 1.0f };

    juce::AudioFormatManager formats;
    std::vector<float> refSource;     // a cappella mono, al sample rate del file
    juce::String refPath;             // salvati nello stato solo dentro getStateInformation
public:
    std::atomic<int> uiSkin { 0 }, uiPage { 0 };
private:
    double refSourceRate = 0.0;
    juce::String refName;
    std::atomic<const RefData*> currentRef { nullptr };
    std::vector<std::unique_ptr<RefData>> refStore;
    std::atomic<int> refVersion { 0 };
    juce::CriticalSection refLock;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WShaperProcessor)
};
