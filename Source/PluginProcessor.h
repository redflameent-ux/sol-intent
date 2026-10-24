#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Library.h"
#include "SamplerVoice.h"
#include "Tone.h"
#include <array>
#include <atomic>
#include <mutex>

class SolIntentProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numLayers = 2;

    SolIntentProcessor();
    ~SolIntentProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- library (message thread)
    void rescanLibrary();
    const std::vector<sol::InstrumentInfo>& getInstrumentList() const { return instrumentList; }
    void selectInstrument (int layer, int indexInList);
    void loadFolderIntoLayer (int layer, const juce::File& folder);
    void reloadLayersUsing (const juce::File& folder);       // after the MAP screen saves
    int  findInList (const juce::File& folder) const;
    sol::InstrumentInfo getLayerInfo (int layer) const;
    sol::InstrumentPtr getDisplayInstrument (int layer);
    bool isLoading() const { return loadsInFlight.load() > 0; }
    juce::String getStatus();
    void setStatus (const juce::String&);
    void stopAllSound() { panicRequested = true; }
    void loadNow (int layer, const sol::InstrumentInfo&);    // on the calling thread (tests)

    // articulations
    int  getArticulation (int layer) const { return layers[(size_t) layer].articulation.load(); }
    void setArticulation (int layer, int index) { layers[(size_t) layer].articulation = juce::jmax (0, index); }
    int  getKeyswitchBase() const;                           // -1 when off

    // ---- presets
    juce::Array<juce::File> getPresets() const;
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File&);
    juce::String currentPresetName { "Init" };

    // ---- MIDI learn
    void startLearning (const juce::String& paramID);
    void forgetLearning (const juce::String& paramID);
    int  getLearnedCC (const juce::String& paramID) const;
    juce::String getLearningParam() const;

    // ---- for the screen
    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<float>, 128> noteGlow {};
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<int> lastNote { -1 };
    int padBaseNote = 48;
    int activeLayer = 0;

private:
    struct Layer
    {
        juce::Synthesiser synth;
        sol::VoiceSettings settings;
        juce::SpinLock swapLock;
        sol::InstrumentPtr pending, current, display;
        std::vector<sol::InstrumentPtr> retired;
        std::atomic<bool> hasPending { false };
        std::atomic<int> articulation { 0 }, generation { 0 };
        sol::InstrumentInfo info;
        juce::AudioBuffer<float> buffer;
        float gainL = 0.0f, gainR = 0.0f;
        bool wasOn = true;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void queueLoad (int layer, const sol::InstrumentInfo&);
    void hand (int layer, sol::InstrumentPtr, const juce::String& error, int generation);
    void handleMidi (juce::MidiBuffer& in, juce::MidiBuffer& out);
    void timerCallback() override;
    juce::ValueTree makeState();
    void applyState (const juce::ValueTree&);

    struct ParamPtrs
    {
        std::atomic<float> *volume, *tune, *fine, *attack, *decay, *sustain, *release, *letRing, *velSens, *bend,
                           *tone, *width, *revMix, *revSize, *lfoRate, *lfoDepth, *lfoTarget, *keyswitch;
        std::atomic<float> *on[2], *vol[2], *tun[2], *pan[2];
    } P;

    std::array<Layer, numLayers> layers;
    juce::AudioFormatManager formats;
    juce::ThreadPool loader { 1 };
    std::atomic<bool> panicRequested { false };

    std::mutex displayMutex;   // guards display pointers + status; never touched by audio
    juce::String status;

    std::vector<sol::InstrumentInfo> instrumentList;
    std::atomic<int> loadsInFlight { 0 };

    // MIDI learn
    std::vector<juce::RangedAudioParameter*> learnable;
    std::array<std::atomic<int>, 128> ccToParam {};
    std::atomic<int> learningIndex { -1 };
    float modWheel = 0.0f;

    // effects
    juce::Reverb reverb;
    sol::Tone toneL, toneR;
    double rate = 44100.0;
    float lastTone = 99.0f, limGain = 1.0f, lfoPhase = 0.0f, lastMasterGain = 1.0f;
    juce::MidiBuffer filtered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolIntentProcessor)
};
