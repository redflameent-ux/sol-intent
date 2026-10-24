#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace sol
{

// Three-band EQ: low shelf 200 Hz, mid bell 1 kHz, high shelf 5 kHz. Flat = bypassed.
struct ThreeBandEQ
{
    void prepare (double sr) { rate = sr; for (auto& f : filters) f.reset(); last[0] = last[1] = last[2] = 99.0f; }

    void process (float* L, float* R, int n, float lowDb, float midDb, float highDb)
    {
        const float g[3] = { lowDb, midDb, highDb };
        for (int b = 0; b < 3; ++b)
        {
            if (std::abs (g[b] - last[b]) > 0.01f)
            {
                last[b] = g[b];
                const double gain = juce::Decibels::decibelsToGain ((double) g[b]);
                auto c = b == 0 ? juce::IIRCoefficients::makeLowShelf  (rate, 200.0, 0.7, gain)
                       : b == 1 ? juce::IIRCoefficients::makePeakFilter (rate, 1000.0, 0.8, gain)
                                : juce::IIRCoefficients::makeHighShelf (rate, 5000.0, 0.7, gain);
                filters[b * 2].setCoefficients (c);
                filters[b * 2 + 1].setCoefficients (c);
            }
            if (std::abs (g[b]) < 0.05f) continue;   // this band is flat
            filters[b * 2].processSamples (L, n);
            if (R != nullptr) filters[b * 2 + 1].processSamples (R, n);
        }
    }

    double rate = 44100.0;
    juce::IIRFilter filters[6];
    float last[3] { 99.0f, 99.0f, 99.0f };
};

// Gentle stereo chorus: two slowly moving delay taps, opposite phase left and right.
struct Chorus
{
    void prepare (double sr)
    {
        rate = sr;
        size = (int) (sr * 0.05) + 4;
        buf[0].assign ((size_t) size, 0.0f);
        buf[1].assign ((size_t) size, 0.0f);
        pos = 0; phase = 0.0f;
    }

    void process (float* L, float* R, int n, float mix, float depth, float hz)
    {
        if (mix < 0.001f || size == 0) return;
        const float inc = juce::MathConstants<float>::twoPi * hz / (float) rate;
        const float baseMs = 12.0f, swingMs = 1.0f + 7.0f * depth;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R != nullptr ? R[i] : inL;
            buf[0][(size_t) pos] = inL;
            buf[1][(size_t) pos] = inR;
            const float dl = (baseMs + swingMs * std::sin (phase)) * 0.001f * (float) rate;
            const float dr = (baseMs + swingMs * std::sin (phase + juce::MathConstants<float>::pi)) * 0.001f * (float) rate;
            const float wl = read (0, dl), wr = read (1, dr);
            L[i] = inL + mix * (wl - inL) * 0.7f;
            if (R != nullptr) R[i] = inR + mix * (wr - inR) * 0.7f;
            phase += inc;
            if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
            if (++pos >= size) pos = 0;
        }
    }

    float read (int ch, float delaySamples) const
    {
        float rp = (float) pos - delaySamples;
        while (rp < 0) rp += (float) size;
        const int i0 = (int) rp, i1 = (i0 + 1) % size;
        const float f = rp - (float) i0;
        return buf[ch][(size_t) i0] * (1.0f - f) + buf[ch][(size_t) i1] * f;
    }

    double rate = 44100.0;
    std::vector<float> buf[2];
    int size = 0, pos = 0;
    float phase = 0.0f;
};

// Stereo echo with a soft, darkening feedback path. Ping-pong spreads repeats left and right.
struct Echo
{
    void prepare (double sr)
    {
        rate = sr;
        size = (int) (sr * 2.6);
        buf[0].assign ((size_t) size, 0.0f);
        buf[1].assign ((size_t) size, 0.0f);
        pos = 0; lpL = lpR = 0.0f;
        smoothedDelay = -1.0f;
    }

    void process (float* L, float* R, int n, float timeMs, float feedback, float mix)
    {
        if (size == 0) return;
        if (mix < 0.001f) { // keep the line quietly empty so turning it up doesn't play old echoes
            if (smoothedDelay >= 0.0f) { std::fill (buf[0].begin(), buf[0].end(), 0.0f); std::fill (buf[1].begin(), buf[1].end(), 0.0f); smoothedDelay = -1.0f; }
            return;
        }
        const float target = juce::jlimit (1.0f, (float) size - 2.0f, timeMs * 0.001f * (float) rate);
        if (smoothedDelay < 0.0f) smoothedDelay = target;
        const float fb = juce::jlimit (0.0f, 0.92f, feedback);
        for (int i = 0; i < n; ++i)
        {
            smoothedDelay += (target - smoothedDelay) * 0.0005f;   // glide when the time changes
            float rp = (float) pos - smoothedDelay;
            if (rp < 0) rp += (float) size;
            const int i0 = (int) rp, i1 = (i0 + 1) % size;
            const float f = rp - (float) i0;
            const float dL = buf[0][(size_t) i0] * (1 - f) + buf[0][(size_t) i1] * f;
            const float dR = buf[1][(size_t) i0] * (1 - f) + buf[1][(size_t) i1] * f;
            lpL += 0.35f * (dL - lpL);
            lpR += 0.35f * (dR - lpR);

            const float inL = L[i], inR = R != nullptr ? R[i] : inL;
            const float mono = 0.5f * (inL + inR);
            buf[0][(size_t) pos] = mono + lpR * fb;   // ping-pong: each side feeds the other
            buf[1][(size_t) pos] = lpL * fb;
            L[i] = inL + mix * dL;
            if (R != nullptr) R[i] = inR + mix * dR;
            if (++pos >= size) pos = 0;
        }
    }

    double rate = 44100.0;
    std::vector<float> buf[2];
    int size = 0, pos = 0;
    float lpL = 0, lpR = 0, smoothedDelay = -1.0f;
};

} // namespace sol
