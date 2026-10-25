#include "SamplerVoice.h"
#include <cmath>

namespace sol
{

void SamplerVoice::startNote (int note, float velocity, juce::SynthesiserSound*, int pitchWheel)
{
    zone = nullptr;
    if (settings.instrument == nullptr)
    {
        clearCurrentNote();
        return;
    }

    auto vel127 = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    if (auto* art = settings.instrument->get (settings.articulation))
        zone = art->findZone (note, vel127, settings.rrCounter++);
    if (zone == nullptr || zone->audio.getNumSamples() < 4)
    {
        zone = nullptr;
        clearCurrentNote();
        return;
    }

    pos = 0.0;
    noteOffset = (float) (note - zone->rootNote);
    gain = zone->gain * ((1.0f - settings.velSens) + settings.velSens * std::pow (velocity, 1.6f));
    // notes on channel 1 come from a normal keyboard and use the Bend knob; notes on channels
    // 2-16 are MPE (the ROLL page, or an MPE controller), bending +/-48 semitones on their own
    mpe = ! isPlayingChannel (1);
    bendRange = mpe ? 48.0f : settings.bendRange;
    bendTarget = bendNow = bendFromWheel (pitchWheel);
    pressure = pressureNow = 0.0f;
    slide = slideNow = 0.5f;
    released = false;
    level = 0.0f;

    env.setSampleRate (getSampleRate());
    env.setParameters ({ settings.attackSec, settings.decaySec, settings.sustain, settings.releaseSec });
    env.reset();
    env.noteOn();
}

void SamplerVoice::stopNote (float, bool allowTailOff)
{
    if (! allowTailOff || zone == nullptr)
    {
        env.reset();
        zone = nullptr;
        level = 0.0f;
        clearCurrentNote();
        return;
    }

    if (settings.letRing)
    {
        released = true;   // keep ringing until the recording ends
        return;
    }

    env.setParameters ({ settings.attackSec, settings.decaySec, settings.sustain, settings.releaseSec });
    env.noteOff();
}

void SamplerVoice::channelPressureChanged (int newValue)
{
    if (mpe) pressure = juce::jlimit (0.0f, 1.0f, (float) newValue / 127.0f);
}

void SamplerVoice::controllerMoved (int controller, int value)
{
    if (mpe && controller == 74) slide = juce::jlimit (0.0f, 1.0f, (float) value / 127.0f);
}

void SamplerVoice::pitchWheelMoved (int newValue)
{
    bendTarget = bendFromWheel (newValue);
}

void SamplerVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    if (zone == nullptr)
        return;

    // "Let ring" switched off while this note was held over? Start the release now.
    if (released && ! settings.letRing)
    {
        released = false;
        env.setParameters ({ settings.attackSec, settings.decaySec, settings.sustain, settings.releaseSec });
        env.noteOff();
    }

    const auto* srcL = zone->audio.getReadPointer (0);
    const auto* srcR = zone->audio.getReadPointer (1);
    const int len = zone->audio.getNumSamples();
    const double srRatio = zone->sampleRate / getSampleRate();
    const float bendSmooth = 1.0f - std::exp (-1.0f / (0.004f * (float) getSampleRate()));   // ~4 ms glide on bends
    const float exprSmooth = 1.0f - std::exp (-1.0f / (0.01f * (float) getSampleRate()));    // ~10 ms on pressure / slide
    if (! mpe) bendRange = settings.bendRange;   // follow the Bend knob live

    auto ratioFor = [&] (float bend)
    {
        float semis = noteOffset + settings.tuneSemis + bend * bendRange;
        return (semis == 0.0f ? 1.0 : std::exp2 (semis / 12.0)) * srRatio;
    };

    double ratio = ratioFor (bendNow);
    float peak = 0.0f;
    auto* L = out.getWritePointer (0, start);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        if (std::abs (bendTarget - bendNow) > 1.0e-5f)
        {
            bendNow += (bendTarget - bendNow) * bendSmooth;
            ratio = ratioFor (bendNow);
        }

        const int idx = (int) pos;
        if (idx >= len - 1 || ! env.isActive())
        {
            zone = nullptr;
            level = 0.0f;
            clearCurrentNote();
            return;
        }

        const float frac = (float) (pos - idx);
        float l, r;
        if (frac == 0.0f)
        {
            l = srcL[idx];   // exact original sample
            r = srcR[idx];
        }
        else
        {
            // 4-point Hermite
            const int i0 = juce::jmax (0, idx - 1), i2 = idx + 1, i3 = juce::jmin (len - 1, idx + 2);
            auto herm = [frac] (float y0, float y1, float y2, float y3)
            {
                float c1 = 0.5f * (y2 - y0);
                float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * frac + c2) * frac + c1) * frac + y1;
            };
            l = herm (srcL[i0], srcL[idx], srcL[i2], srcL[i3]);
            r = herm (srcR[i0], srcR[idx], srcR[i2], srcR[i3]);
        }

        float g = gain * env.getNextSample();
        if (pos > (double) (len - 257))   // a tiny fade over the last few ms so a recording never ends with a click
            g *= juce::jmax (0.0f, (float) ((double) (len - 1) - pos) / 256.0f);
        if (mpe)
        {
            pressureNow += (pressure - pressureNow) * exprSmooth;
            slideNow += (slide - slideNow) * exprSmooth;
            g *= 1.0f + pressureNow;                                   // pressure swells it, up to +6 dB
            const float pan = (slideNow - 0.5f) * 2.0f;                // slide moves it left / right
            l *= juce::jmin (1.0f, 1.0f - pan);
            r *= juce::jmin (1.0f, 1.0f + pan);
        }
        l *= g; r *= g;
        if (R != nullptr) { L[i] += l; R[i] += r; }
        else              { L[i] += 0.5f * (l + r); }
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
        pos += ratio;
    }

    level = juce::jmax (peak, level * 0.9f);
}

} // namespace sol
