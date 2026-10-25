#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RollPattern.h"
#include <array>
#include <atomic>
#include <deque>

// =====================================================================================
//  Plays the ROLL page's pattern in time with the host (or its own Play button) and
//  turns every line into an MPE note: its own MIDI channel, so its pitch bend, pressure
//  and slide move independently of every other note.
// =====================================================================================
namespace sol::roll
{
class RollEngine
{
public:
    RollEngine();

    void prepare (double sampleRate);

    // ---- message thread ----
    void setPattern (const Pattern& p);              // publishes a copy to the audio thread
    PatternPtr getPattern() const;
    void play();
    void pause();
    void stop();
    bool isInternalPlaying() const { return internalPlaying.load(); }
    std::atomic<bool> recording { false };

    // audition while drawing (channel 16)
    void preview (float pitch, float velocity) { previewPitch = pitch; previewVel = velocity; }
    void previewOff() { previewPitch = -1.0f; }

    struct RecEvent
    {
        enum Type { NoteOn, NoteOff, Bend, Pressure, Slide } type = NoteOn;
        double beat = 0; int channel = 1, note = 60; float value = 0;
    };
    bool popRecorded (RecEvent& e);

    // ---- audio thread ----
    // Adds the pattern's notes to `midi`. masterBendRange = the Bend knob (for recording channel 1).
    void process (juce::MidiBuffer& midi, int numSamples, juce::AudioPlayHead* playHead, int masterBendRange);

    // the Pitch Mod wobble, in semitones (shared by playback and MIDI export so they match)
    static float wobble (const Pattern& p, const Line& l, double pos);

    // for the screen
    std::atomic<double> playheadBeat { -1.0 };
    std::atomic<bool> transportRunning { false };
    std::atomic<double> currentBpm { 120.0 };

private:
    struct Active { int lineId = -1, channel = 0, note = 0; float bend = 0, pressure = -1, slide = -1; };

    void allNotesOff (juce::MidiBuffer& out, int sample);
    static int bendValue (float semis, float range)
    {
        return juce::jlimit (0, 16383, juce::roundToInt (8192.0f + semis / range * 8191.0f));
    }
    void pushRec (const RecEvent& e);

    double sampleRate = 48000.0;
    juce::SpinLock patternLock;
    PatternPtr pattern, audioPattern;
    std::deque<PatternPtr> keepAlive;               // so the audio thread never frees a pattern

    std::array<Active, 16> active {};               // index = MIDI channel - 1
    int nextChannel = 2;
    double lastPos = -1.0;
    double internalBeat = 0.0;
    std::atomic<bool> internalPlaying { false }, stopRequested { false };

    juce::MidiBuffer out;
    std::atomic<float> previewPitch { -1.0f }, previewVel { 0.8f };
    float previewPlaying = -1.0f; int previewNote = -1;

    // recording FIFO (audio -> message thread)
    juce::AbstractFifo recFifo { 4096 };
    std::array<RecEvent, 4096> recBuf {};
    std::array<float, 16> recBendRange {};
};
} // namespace sol::roll
