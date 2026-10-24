#pragma once
#include <cmath>

namespace sol
{

// One knob from dark to bright: left of centre rolls off the top end,
// right of centre lifts the shimmer, centre does nothing at all.
struct Tone
{
    void set (float tone, double sampleRate)
    {
        if (std::abs (tone) < 0.01f) { bypass = true; return; }
        bypass = false;
        const double pi = 3.14159265358979323846;

        if (tone < 0.0f)
        {
            double fc = 20000.0 * std::pow (0.04, (double) -tone);   // 20 kHz .. 800 Hz
            fc = std::fmin (fc, sampleRate * 0.45);
            double w = 2.0 * pi * fc / sampleRate, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
            double a0 = 1.0 + al;
            b0 = (1.0 - cw) / 2.0 / a0; b1 = (1.0 - cw) / a0; b2 = b0;
            a1 = -2.0 * cw / a0; a2 = (1.0 - al) / a0;
        }
        else
        {
            double gainDb = 9.0 * tone, A = std::pow (10.0, gainDb / 40.0);
            double w = 2.0 * pi * 2500.0 / sampleRate, cw = std::cos (w), sw = std::sin (w);
            double al = sw / 2.0 * std::sqrt (2.0), sA = 2.0 * std::sqrt (A) * al;
            double a0 = (A + 1) - (A - 1) * cw + sA;
            b0 =      A * ((A + 1) + (A - 1) * cw + sA) / a0;
            b1 = -2 * A * ((A - 1) + (A + 1) * cw)      / a0;
            b2 =      A * ((A + 1) + (A - 1) * cw - sA) / a0;
            a1 =      2 * ((A - 1) - (A + 1) * cw)      / a0;
            a2 =          ((A + 1) - (A - 1) * cw - sA) / a0;
        }
    }

    float process (float x)
    {
        if (bypass) return x;
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void reset() { z1 = z2 = 0.0; }

    bool bypass = true;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

} // namespace sol
