#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <vector>

// =====================================================================================
//  ROLL page data: notes drawn as lines that bend.
//  A line is a chain of nodes (time in beats, pitch in semitones - fractional pitches are
//  fine). Between nodes the pitch glides along a curve. Every line also has its own
//  velocity, a pressure curve and a slide curve (the three MPE "dimensions").
// =====================================================================================
namespace sol::roll
{
struct Node
{
    double beat = 0.0;
    float pitch = 60.0f;     // MIDI note number, may be fractional
    float curve = 0.0f;      // shape of the glide to the NEXT node: 0 = smooth S, +1 = fast then settle, -1 = slow then swoop
};

struct Point { double beat = 0.0; float value = 0.0f; };   // value 0..1

struct Line
{
    int id = 0;
    std::vector<Node> nodes;          // at least 2, sorted by beat
    float velocity = 0.8f;            // 0..1
    std::vector<Point> pressure;      // empty = no pressure
    std::vector<Point> slide;         // empty = centre (no slide)

    double start() const { return nodes.empty() ? 0.0 : nodes.front().beat; }
    double end() const   { return nodes.empty() ? 0.0 : nodes.back().beat; }
    float startPitch() const { return nodes.empty() ? 60.0f : nodes.front().pitch; }

    static float shape (float t, float curve)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        const float smooth = t * t * (3.0f - 2.0f * t);
        if (std::abs (curve) < 1.0e-4f) return smooth;
        const float powered = curve > 0.0f ? 1.0f - std::pow (1.0f - t, 1.0f + 4.0f * curve)
                                           : std::pow (t, 1.0f - 4.0f * curve);
        const float a = std::abs (curve);
        return smooth + (powered - smooth) * a;
    }

    float pitchAt (double beat) const
    {
        if (nodes.empty()) return 60.0f;
        if (beat <= nodes.front().beat) return nodes.front().pitch;
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
        {
            const auto& a = nodes[i];
            const auto& b = nodes[i + 1];
            if (beat <= b.beat)
            {
                const double len = b.beat - a.beat;
                const float t = len > 1.0e-9 ? (float) ((beat - a.beat) / len) : 1.0f;
                return a.pitch + (b.pitch - a.pitch) * shape (t, a.curve);
            }
        }
        return nodes.back().pitch;
    }

    static float curveAt (const std::vector<Point>& pts, double beat, float fallback)
    {
        if (pts.empty()) return fallback;
        if (beat <= pts.front().beat) return pts.front().value;
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            if (beat <= pts[i + 1].beat)
            {
                const double len = pts[i + 1].beat - pts[i].beat;
                const float t = len > 1.0e-9 ? (float) ((beat - pts[i].beat) / len) : 1.0f;
                return pts[i].value + (pts[i + 1].value - pts[i].value) * t;
            }
        return pts.back().value;
    }

    float pressureAt (double beat) const { return curveAt (pressure, beat, 0.0f); }
    float slideAt (double beat) const    { return curveAt (slide, beat, 0.5f); }

    void sortNodes()
    {
        std::sort (nodes.begin(), nodes.end(), [] (const Node& a, const Node& b) { return a.beat < b.beat; });
        std::sort (pressure.begin(), pressure.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
        std::sort (slide.begin(), slide.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
    }
};

struct Pattern
{
    std::vector<Line> lines;
    double lengthBeats = 16.0;        // 4 bars of 4/4
    bool loop = true;

    // Pitch Mod: a wobble added on top of every line while it plays
    int modShape = 0;                 // 0 none, 1 sine, 2 triangle, 3 saw, 4 square, 5 random
    float modAmount = 50.0f;          // cents (100 = one semitone)
    double modRateBeats = 0.25;       // one cycle per 1/16 note
    float modFade = 0.5f;             // 0..1: how much of each note the wobble takes to fade in
    bool bipolar = false;

    int nextId = 1;

    Line* find (int id)
    {
        for (auto& l : lines) if (l.id == id) return &l;
        return nullptr;
    }

    // ---- save / load ----
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("Roll");
        t.setProperty ("length", lengthBeats, nullptr);
        t.setProperty ("loop", loop, nullptr);
        t.setProperty ("modShape", modShape, nullptr);
        t.setProperty ("modAmount", modAmount, nullptr);
        t.setProperty ("modRate", modRateBeats, nullptr);
        t.setProperty ("modFade", modFade, nullptr);
        t.setProperty ("bipolar", bipolar, nullptr);
        for (auto& l : lines)
        {
            juce::ValueTree lt ("Line");
            lt.setProperty ("vel", l.velocity, nullptr);
            juce::String nodesText, pText, sText;
            for (auto& n : l.nodes) nodesText << juce::String (n.beat, 5) << ' ' << juce::String (n.pitch, 4) << ' ' << juce::String (n.curve, 3) << ';';
            for (auto& p : l.pressure) pText << juce::String (p.beat, 5) << ' ' << juce::String (p.value, 4) << ';';
            for (auto& p : l.slide) sText << juce::String (p.beat, 5) << ' ' << juce::String (p.value, 4) << ';';
            lt.setProperty ("nodes", nodesText, nullptr);
            lt.setProperty ("pressure", pText, nullptr);
            lt.setProperty ("slide", sText, nullptr);
            t.appendChild (lt, nullptr);
        }
        return t;
    }

    static Pattern fromTree (const juce::ValueTree& t)
    {
        Pattern p;
        if (! t.hasType ("Roll")) return p;
        p.lengthBeats = juce::jlimit (1.0, 512.0, (double) t.getProperty ("length", 16.0));
        p.loop = (bool) t.getProperty ("loop", true);
        p.modShape = (int) t.getProperty ("modShape", 0);
        p.modAmount = (float) t.getProperty ("modAmount", 50.0f);
        p.modRateBeats = (double) t.getProperty ("modRate", 0.25);
        p.modFade = (float) t.getProperty ("modFade", 0.5f);
        p.bipolar = (bool) t.getProperty ("bipolar", false);
        for (auto lt : t)
        {
            if (! lt.hasType ("Line")) continue;
            Line l;
            l.id = p.nextId++;
            l.velocity = (float) lt.getProperty ("vel", 0.8f);
            for (auto& tok : juce::StringArray::fromTokens (lt.getProperty ("nodes").toString(), ";", ""))
            {
                auto parts = juce::StringArray::fromTokens (tok, " ", "");
                if (parts.size() >= 2) l.nodes.push_back ({ parts[0].getDoubleValue(), parts[1].getFloatValue(), parts.size() > 2 ? parts[2].getFloatValue() : 0.0f });
            }
            auto readPts = [] (const juce::String& text, std::vector<Point>& out)
            {
                for (auto& tok : juce::StringArray::fromTokens (text, ";", ""))
                {
                    auto parts = juce::StringArray::fromTokens (tok, " ", "");
                    if (parts.size() >= 2) out.push_back ({ parts[0].getDoubleValue(), parts[1].getFloatValue() });
                }
            };
            readPts (lt.getProperty ("pressure").toString(), l.pressure);
            readPts (lt.getProperty ("slide").toString(), l.slide);
            if (l.nodes.size() >= 2) { l.sortNodes(); p.lines.push_back (std::move (l)); }
        }
        return p;
    }
};

using PatternPtr = std::shared_ptr<const Pattern>;

} // namespace sol::roll
