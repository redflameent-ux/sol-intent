#include "SamplerVoice.h"
#include <cmath>

namespace sol
{

// ---------------------------------------------------------------------------- reading the recording
// pos is always counted in samples of the original file; octave k reads the copy at 1/2^k the rate
void SamplerVoice::read (const Zone& z, int octave, double pos, float& l, float& r)
{
    const juce::AudioBuffer<float>& buf = octave == 0 ? z.audio : z.octaves[(size_t) octave - 1];
    const double p = octave == 0 ? pos : pos / (double) (1 << octave);
    const int len = buf.getNumSamples();
    const int idx = (int) p;
    const float* srcL = buf.getReadPointer (0);
    const float* srcR = buf.getReadPointer (1);
    if (idx >= len - 1) { l = r = 0.0f; return; }
    const float frac = (float) (p - idx);
    if (frac == 0.0f)
    {
        l = srcL[idx];   // exact original sample
        r = srcR[idx];
        return;
    }
    // 4-point Hermite
    const int i0 = juce::jmax (0, idx - 1), i2 = idx + 1, i3 = juce::jmin (len - 1, idx + 2);
    auto herm = [frac] (float y0, float y1, float y2, float y3)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    };
    l = herm (srcL[i0], srcL[idx], srcL[i2], srcL[i3]);
    r = herm (srcR[i0], srcR[idx], srcR[i2], srcR[i3]);
}

// Playing more than ~1.5x faster than the file folds its top end back down as harsh noise,
// so step down to a filtered half-rate copy. A little hysteresis keeps bends from flicking between copies.
int SamplerVoice::octaveFor (const Zone& z, double ratio, int current)
{
    const int maxOct = (int) z.octaves.size();
    int want = 0;
    while (want < maxOct && ratio / (double) (1 << want) >= 1.5) ++want;
    if (want == current) return current;
    if (want > current) return ratio / (double) (1 << current) >= 1.5 * 1.03 ? want : current;
    return ratio / (double) (1 << want) < 1.5 / 1.03 ? want : current;
}

// ---------------------------------------------------------------------------- notes
void SamplerVoice::startNote (int note, float velocity, juce::SynthesiserSound*, int pitchWheel)
{
    zone = nullptr;
    chokeLeft = 0;
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
    octave = 0;
    chokeDone = false;
    noteOffset = (float) (note - zone->rootNote);
    // Touch: 0% = every hit the same, 70% = a soft hit (velocity 64) about 10 dB down, 100% = about 15 dB down
    gain = zone->gain * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 2.5f * settings.velSens);
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
        // hand whatever is still sounding to a very short fade, so nothing stops with a click
        if (zone != nullptr && (lastGainL > 1.0e-5f || lastGainR > 1.0e-5f))
            tail = { zone, pos, lastRatio, lastGainL, lastGainR, octave, tailLen };
        env.reset();
        zone = nullptr;
        level = 0.0f;
        chokeLeft = 0;
        lastGainL = lastGainR = 0.0f;
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

void SamplerVoice::choke()
{
    if (zone != nullptr && chokeLeft == 0)
    {
        chokeLen = juce::jmax (1, (int) (getSampleRate() * 0.04));   // 40 ms, like a hand on the drum head
        chokeLeft = chokeLen;
    }
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

// ---------------------------------------------------------------------------- rendering
void SamplerVoice::renderTail (float* L, float* R, int num)
{
    for (int i = 0; i < num && tail.left > 0; ++i, --tail.left)
    {
        const float f = (float) tail.left / (float) tailLen;
        float l, r;
        read (*tail.zone, tail.octave, tail.pos, l, r);
        if (R != nullptr) { L[i] += l * tail.gainL * f; R[i] += r * tail.gainR * f; }
        else              { L[i] += 0.5f * (l * tail.gainL + r * tail.gainR) * f; }
        tail.pos += tail.ratio;
    }
    if (tail.left <= 0) tail.zone = nullptr;
}

void SamplerVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    auto* L = out.getWritePointer (0, start);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    if (tail.zone != nullptr)
        renderTail (L, R, num);

    if (zone == nullptr)
        return;

    // "Let ring" switched off while this note was held over? Start the release now.
    if (released && ! settings.letRing)
    {
        released = false;
        env.setParameters ({ settings.attackSec, settings.decaySec, settings.sustain, settings.releaseSec });
        env.noteOff();
    }

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
    octave = octaveFor (*zone, ratio, octave);
    float peak = 0.0f;

    for (int i = 0; i < num; ++i)
    {
        if (std::abs (bendTarget - bendNow) > 1.0e-5f)
        {
            bendNow += (bendTarget - bendNow) * bendSmooth;
            ratio = ratioFor (bendNow);
            if ((i & 31) == 0) octave = octaveFor (*zone, ratio, octave);
        }

        const int idx = (int) pos;
        if (idx >= len - 1 || ! env.isActive() || chokeDone)
        {
            zone = nullptr;
            level = 0.0f;
            chokeLeft = 0;
            lastGainL = lastGainR = 0.0f;
            clearCurrentNote();
            return;
        }

        float l, r;
        read (*zone, octave, pos, l, r);

        float g = gain * env.getNextSample();
        if (pos > (double) (len - 257))   // a tiny fade over the last few ms so a recording never ends with a click
            g *= juce::jmax (0.0f, (float) ((double) (len - 1) - pos) / 256.0f);
        if (chokeLeft > 0)
        {
            const float f = (float) chokeLeft / (float) chokeLen;
            g *= f * f;
            if (--chokeLeft == 0) chokeDone = true;   // faded right out; the voice ends on the next sample, even if that's in the next slice
        }
        float gl = g, gr = g;
        if (mpe)
        {
            pressureNow += (pressure - pressureNow) * exprSmooth;
            slideNow += (slide - slideNow) * exprSmooth;
            const float swell = 1.0f + pressureNow;                    // pressure swells it, up to +6 dB
            const float pan = (slideNow - 0.5f) * 2.0f;                // slide moves it left / right
            gl *= swell * juce::jmin (1.0f, 1.0f - pan);
            gr *= swell * juce::jmin (1.0f, 1.0f + pan);
        }
        l *= gl; r *= gr;
        if (R != nullptr) { L[i] += l; R[i] += r; }
        else              { L[i] += 0.5f * (l + r); }
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
        lastGainL = gl; lastGainR = gr;
        pos += ratio;
    }

    lastRatio = ratio;
    level = juce::jmax (peak, level * 0.9f);
}

} // namespace sol
