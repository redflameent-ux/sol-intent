#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Library.h"
#include "SamplerVoice.h"
#include "Tone.h"
#include "Effects.h"
#include "roll/RollEngine.h"
#include <array>
#include <atomic>
#include <mutex>

class SolIntentProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numLayers = 4;
    static constexpr int voicesPerLayer = 64;   // bowls and drums ring a long time, so plenty of voices

    // What every voice is sounding right now, for the star display. Written by the audio thread once
    // per block (plain atomic stores - no locks, no allocation), read by the screen whenever it likes.
    struct VoiceView
    {
        std::atomic<float> hz { 0.0f };          // real sounding pitch: tuning, key, Tune knobs and bend included
        std::atomic<float> level { 0.0f };       // its actual output level, so it follows the recording's tail
        std::atomic<float> velocity { 0.0f };
        std::atomic<unsigned int> serial { 0 };  // changes when the voice starts a new note
    };
    std::array<VoiceView, (size_t) (numLayers * voicesPerLayer)> voiceViews;
    static juce::String layerLetter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }
    static juce::String layerParam (int i, const char* what) { return juce::String::charToString ((juce::juce_wchar) ('a' + i)) + what; }

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
    // how many voices a layer is using (for the tests)
    int debugActiveVoices (int layer) { int n = 0; for (int i = 0; i < layers[(size_t) layer].synth.getNumVoices(); ++i) if (layers[(size_t) layer].synth.getVoice (i)->isVoiceActive()) ++n; return n; }
    bool supportsMPE() const override { return true; }   // tells Logic to send each note on its own channel
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
    void clearLayer (int layer);                             // empty the slot and switch it off
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
    bool loadPreset (const juce::File&, bool withInstruments = false);
    bool presetsChangeInstruments() const;          // a per-computer choice in the preset menu
    void setPresetsChangeInstruments (bool);
    juce::String currentPresetName { "Init" };

    // ---- macros: each of the 4 macro knobs moves any number of other knobs between two settings
    static constexpr int numMacros = 4;
    struct MacroLink { int macro = 0; juce::String paramID; float from = 0, to = 1; };   // normalised 0..1
    void linkToMacro (int macro, const juce::String& paramID);          // from = where the knob is now
    void setMacroEnd (int macro, const juce::String& paramID);          // to = where the knob is now
    void unlinkFromMacro (const juce::String& paramID);
    int  getMacroFor (const juce::String& paramID) const;               // -1 if not linked
    std::vector<MacroLink> getMacroLinks (int macro) const;
    void clearMacro (int macro);
    juce::String getMacroName (int macro) const;
    void setMacroName (int macro, const juce::String&);
    void applyMacros (bool force = false);                               // pushes macro positions to linked knobs

    // ---- modulation: any knob can be moved by Motion 1, Motion 2 (slow waves), the mod wheel or aftertouch
    enum ModSource { motion1 = 0, motion2, modWheelSource, aftertouchSource, numModSources };
    static juce::String modSourceName (int s);
    struct ModLink { int source = 0; juce::String paramID; float depth = 0.25f; bool quarter = false; };   // depth: -1..1 of the knob's range
    void setModulation (int source, const juce::String& paramID, float depth);   // depth 0 removes it
    std::vector<ModLink> getModulations (const juce::String& paramID = {}) const; // empty id = all of them
    void clearModulations (const juce::String& paramID);
    void setOrbit (bool on);                                                      // Motion 1 circles the morph dot
    bool isOrbiting() const;
    static bool canModulate (const juce::AudioProcessorParameter*);

    // ---- morph pad: where the blend actually is right now (includes any modulation)
    std::atomic<float> morphXNow { 0.5f }, morphYNow { 0.5f };

    // ---- MIDI learn
    void startLearning (const juce::String& paramID);
    void forgetLearning (const juce::String& paramID);
    int  getLearnedCC (const juce::String& paramID) const;
    juce::String getLearningParam() const;

    // ---- the ROLL page: notes drawn as bendable lines, played as MPE (each note on its own channel)
    sol::roll::RollEngine roll;

    // ---- for the screen
    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<float>, 128> noteGlow {};
    std::array<std::atomic<unsigned int>, 128> noteHits {};   // counts every note-on, so the display never misses a short hit
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<int> lastNote { -1 };
    int padBaseNote = 48;
    int activeLayer = 0;

private:
    struct Layer
    {
        // a Synthesiser that can damp a hit still ringing on the same key when it's struck again (drums)
        struct Synth : juce::Synthesiser
        {
            bool choke = false;
            void noteOn (int ch, int note, float vel) override
            {
                if (choke)
                    for (auto* v : voices)
                        if (v->getCurrentlyPlayingNote() == note && v->isPlayingChannel (ch))
                            if (auto* sv = dynamic_cast<sol::SamplerVoice*> (v)) sv->choke();
                juce::Synthesiser::noteOn (ch, note, vel);
            }
        };
        Synth synth;
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
        juce::MidiBuffer midi;   // only the notes inside this layer's key zone
        juce::MidiBuffer chunkMidi;   // just the events inside the 32-sample slice being rendered
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
        std::atomic<float> *volume, *tune, *fine, *velSens, *bend,
                           *tone, *width, *revMix, *revSize, *lfoRate, *lfoDepth, *lfoRate2, *lfoDepth2, *keyswitch,
                           *eqLow, *eqMid, *eqHigh, *chorusMix, *chorusDepth, *chorusRate, *delayTime, *delayFb, *delayMix,
                           *morphOn, *morphX, *morphY;
        std::atomic<float> *on[numLayers], *vol[numLayers], *tun[numLayers], *pan[numLayers],
                           *atk[numLayers], *dec[numLayers], *sus[numLayers], *rel[numLayers], *ring[numLayers], *choke[numLayers],
                           *keyLo[numLayers], *keyHi[numLayers];
        std::atomic<float> *macro[numMacros];
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
    float modWheel = 0.0f, aftertouch = 0.0f;

    // modulation: edited on the message thread, copied to the audio thread when it changes
    struct ActiveMod { int source; juce::RangedAudioParameter* prm; std::atomic<float>* raw; float depth; bool quarter; };
    static constexpr int maxMods = 64;
    mutable std::mutex modMutex;
    std::vector<ModLink> modLinks;
    juce::SpinLock modSwap;
    std::array<ActiveMod, maxMods> pendingMods, activeMods;
    int numPendingMods = 0, numActiveMods = 0;
    std::atomic<bool> modsChanged { false };
    float srcVal[numModSources] {}, srcQuarter[2] {};
    float lfoPhase2 = 0.0f;
    void publishMods();
    void updateModSources (int numSamples);
    float mod (std::atomic<float>* raw) const;   // a knob's value with modulation added
    juce::ValueTree modsTree() const;
    void applyModsTree (const juce::ValueTree& state);
    std::atomic<bool> midiMapChanged { false };
    void saveDefaultMidiMap();
    void loadDefaultMidiMap();
    juce::ValueTree midiMapTree() const;
    void applyMidiMapTree (const juce::ValueTree&);

    // macros
    mutable std::mutex macroMutex;
    std::vector<MacroLink> macroLinks;
    std::array<juce::String, numMacros> macroNames;
    std::array<float, numMacros> lastMacro { -1.0f, -1.0f, -1.0f, -1.0f };

    // effects
    juce::Reverb reverb;
    sol::Tone toneL, toneR;
    sol::ThreeBandEQ eq;
    sol::Chorus chorus;
    sol::Echo echo;
    double rate = 44100.0;
    float lastTone = 99.0f, limGain = 1.0f, lfoPhase = 0.0f, lastMasterGain = 1.0f;
    juce::MidiBuffer filtered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolIntentProcessor)
};
