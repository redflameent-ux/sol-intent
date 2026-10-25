#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Library.h"
#include <atomic>

namespace sol
{

// Settings the voices read every block (written on the audio thread before rendering).
struct VoiceSettings
{
    const Instrument* instrument = nullptr;
    int articulation = 0;
    float tuneSemis = 0.0f;       // everything that shifts pitch, combined
    float attackSec = 0.0f, decaySec = 0.5f, sustain = 1.0f, releaseSec = 3.0f;
    bool  letRing = true;
    float velSens = 0.7f;
    float bendRange = 12.0f;
    unsigned int rrCounter = 0;
};

struct AnySound : juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Plays one recording. At its own key with no bend or tuning it plays the file
// back sample-for-sample; other keys repitch from the nearest recording.
class SamplerVoice : public juce::SynthesiserVoice
{
public:
    explicit SamplerVoice (VoiceSettings& s) : settings (s) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<AnySound*> (s) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int controller, int value) override;
    void channelPressureChanged (int newValue) override;
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;
    using juce::SynthesiserVoice::renderNextBlock;

    float getLevel() const noexcept { return level; }

private:
    static float bendFromWheel (int v) { return juce::jlimit (-1.0f, 1.0f, (v - 8192) / 8191.0f); }

    VoiceSettings& settings;
    const Zone* zone = nullptr;
    double pos = 0.0;
    float noteOffset = 0.0f;       // key - root, in semitones
    float gain = 1.0f;
    float bendTarget = 0.0f, bendNow = 0.0f;
    float bendRange = 12.0f;      // the Bend knob for a normal keyboard; 48 for MPE notes (channels 2-16)
    bool  mpe = false;
    float pressure = 0.0f, pressureNow = 0.0f;   // MPE pressure swells the note (up to +6 dB)
    float slide = 0.5f, slideNow = 0.5f;         // MPE slide pans the note
    bool  released = false;        // key let go while "let ring" was on
    juce::ADSR env;
    float level = 0.0f;            // for the display
};

} // namespace sol
