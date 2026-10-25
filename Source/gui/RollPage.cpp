#include "RollPage.h"
#include "../PluginProcessor.h"

using namespace juce;

namespace sol::gui
{
static const char* gridNames[] = { "Off", "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
static const double gridValues[] = { 0.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
static const char* speedNames[] = { "1/2", "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
static const double speedValues[] = { 2.0, 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };

static String pitchName (float p)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int k = (int) std::round (p);
    return String (n[((k % 12) + 12) % 12]) + String (k / 12 - 2);   // C3 = 60
}

static const Colour lineCol  = col::wavetable;
static const Colour selCol   = col::sampler;
static const Colour pressCol = Colour (0xff4f8dff);
static const Colour slideCol = col::mod;
static const Colour velCol   = col::analog;

// The same colours as the ring display: each note's place on the circle of fifths,
// cyan -> violet -> gold, so notes that sound good together have neighbouring colours.
static Colour noteColour (float pitch)
{
    const int n = ((int) std::round (pitch) % 12 + 12) % 12;
    const float t = (float) ((n * 7) % 12) / 11.0f;
    return t < 0.5f ? col::wavetable.interpolatedWith (col::granular, t * 2.0f)
                    : col::granular.interpolatedWith (col::analog, (t - 0.5f) * 2.0f);
}

// ============================================================================ canvas
RollCanvas::RollCanvas (RollPage& p) : page (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (hBar);
    addAndMakeVisible (vBar);
    hBar.addListener (this);
    vBar.addListener (this);
    for (auto* b : { &hBar, &vBar })
    {
        b->setColour (ScrollBar::thumbColourId, col::border.brighter (0.4f));
        b->setAutoHide (false);
    }
}

Rectangle<int> RollCanvas::gridArea() const
{
    auto r = getLocalBounds();
    r.removeFromLeft (46);
    r.removeFromTop (22);
    r.removeFromRight (10);
    r.removeFromBottom (10);
    return r;
}

Rectangle<float> RollCanvas::legendArea() const
{
    auto g = gridArea().toFloat();
    return { g.getRight() - 92.0f, g.getY() + 6.0f, 84.0f, 78.0f };
}

float RollCanvas::rowHeight() const { return (float) gridArea().getHeight() / viewPitches; }
float RollCanvas::xForBeat (double b) const { auto g = gridArea(); return (float) g.getX() + (float) ((b - viewStart) / viewBeats) * (float) g.getWidth(); }
double RollCanvas::beatForX (float x) const { auto g = gridArea(); return viewStart + (double) (x - (float) g.getX()) / g.getWidth() * viewBeats; }
float RollCanvas::yForPitch (float p) const { auto g = gridArea(); return (float) g.getBottom() - (p - viewLowPitch + 0.5f) * rowHeight(); }
float RollCanvas::pitchForY (float y) const { auto g = gridArea(); return viewLowPitch + ((float) g.getBottom() - y) / rowHeight() - 0.5f; }

void RollCanvas::resized()
{
    auto g = gridArea();
    hBar.setBounds (g.getX(), g.getBottom(), g.getWidth(), 10);
    vBar.setBounds (g.getRight(), g.getY(), 10, g.getHeight());
    updateScrollBars();
}

void RollCanvas::updateScrollBars()
{
    const double total = juce::jmax (page.model.lengthBeats, viewStart + viewBeats) + 4.0;
    hBar.setRangeLimits (0.0, total, dontSendNotification);
    hBar.setCurrentRange (viewStart, viewBeats, dontSendNotification);
    vBar.setRangeLimits (0.0, 128.0, dontSendNotification);
    vBar.setCurrentRange (128.0 - (viewLowPitch + viewPitches), viewPitches, dontSendNotification);
}

void RollCanvas::scrollBarMoved (ScrollBar* bar, double start)
{
    if (bar == &hBar) viewStart = start;
    else viewLowPitch = (float) juce::jlimit (0.0, 128.0 - viewPitches, 128.0 - start - viewPitches);
    repaint();
}

void RollCanvas::fitToPattern()
{
    viewStart = 0.0;
    viewBeats = juce::jmax (4.0, page.model.lengthBeats);
    float lo = 127, hi = 0;
    for (auto& l : page.model.lines)
        for (auto& n : l.nodes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    if (hi >= lo)
    {
        const float mid = (lo + hi) * 0.5f;
        viewPitches = juce::jlimit (24.0f, 72.0f, hi - lo + 12.0f);
        viewLowPitch = juce::jlimit (0.0f, 128.0f - viewPitches, mid - viewPitches * 0.5f);
    }
    updateScrollBars();
    repaint();
}

Path RollCanvas::linePath (const roll::Line& l, bool withWobble) const
{
    Path p;
    const float x0 = juce::jmax (xForBeat (l.start()), (float) gridArea().getX() - 4.0f);
    const float x1 = juce::jmin (xForBeat (l.end()), (float) gridArea().getRight() + 4.0f);
    if (x1 <= x0) return p;
    bool first = true;
    const auto& m = page.model;
    for (float x = x0; ; x += 2.0f)
    {
        const float xx = juce::jmin (x, x1);
        const double b = beatForX (xx);
        float pitch = l.pitchAt (b);
        if (withWobble && m.modShape > 0)
        {
            const double cycles = (b - l.start()) / juce::jmax (1.0e-3, m.modRateBeats);
            const float ph = (float) (cycles - std::floor (cycles));
            float w = std::sin (ph * MathConstants<float>::twoPi);
            if (m.modShape == 2) w = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            if (m.modShape == 3) w = 1.0f - 2.0f * ph;
            if (m.modShape == 4) w = ph < 0.5f ? 1.0f : -1.0f;
            if (! m.bipolar) w = (w + 1.0f) * 0.5f;
            const double len = l.end() - l.start();
            const float fade = m.modFade > 0.001f && len > 0 ? (float) juce::jlimit (0.0, 1.0, (b - l.start()) / (m.modFade * len)) : 1.0f;
            pitch += w * fade * m.modAmount * 0.01f;
        }
        const float y = yForPitch (pitch);
        if (first) { p.startNewSubPath (xx, y); first = false; } else p.lineTo (xx, y);
        if (xx >= x1) break;
    }
    return p;
}

RollCanvas::Hit RollCanvas::hitTest (Point<float> pt) const
{
    Hit h;
    const auto& lines = page.model.lines;
    for (int i = (int) lines.size() - 1; i >= 0; --i)
    {
        const auto& l = lines[(size_t) i];
        for (int n = 0; n < (int) l.nodes.size(); ++n)
        {
            const Point<float> np (xForBeat (l.nodes[(size_t) n].beat), yForPitch (l.nodes[(size_t) n].pitch));
            if (np.getDistanceFrom (pt) < 8.0f) { h.lineId = l.id; h.node = n; return h; }
        }
    }
    const double b = beatForX (pt.x);
    for (int i = (int) lines.size() - 1; i >= 0; --i)
    {
        const auto& l = lines[(size_t) i];
        if (b < l.start() || b > l.end()) continue;
        if (std::abs (yForPitch (l.pitchAt (b)) - pt.y) < juce::jmax (6.0f, rowHeight() * 0.5f))
        {
            h.lineId = l.id;
            for (int n = 0; n + 1 < (int) l.nodes.size(); ++n)
                if (b <= l.nodes[(size_t) n + 1].beat) { h.segment = n; break; }
            if (h.segment < 0) h.segment = (int) l.nodes.size() - 2;
            return h;
        }
    }
    return h;
}

void RollCanvas::paint (Graphics& g)
{
    auto ga = gridArea();
    g.fillAll (col::bg);
    const float rh = rowHeight();
    const auto& m = page.model;

    // ---- rows ----
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (ga);
        for (int p = (int) std::floor (viewLowPitch) - 1; p <= (int) (viewLowPitch + viewPitches) + 1; ++p)
        {
            static const bool black[] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
            const float y = yForPitch ((float) p) - rh * 0.5f;
            g.setColour (black[((p % 12) + 12) % 12] ? Colour (0xff0a0b0f) : Colour (0xff111319));
            g.fillRect (Rectangle<float> ((float) ga.getX(), y, (float) ga.getWidth(), rh));
            if (p % 12 == 0)
            {
                g.setColour (col::border.withAlpha (0.8f));
                g.drawHorizontalLine ((int) (y + rh), (float) ga.getX(), (float) ga.getRight());
            }
        }

        // ---- time grid ----
        const double grid = page.gridBeats();
        const double pxPerBeat = ga.getWidth() / viewBeats;
        if (grid > 0 && grid * pxPerBeat > 7.0)
            for (double b = std::floor (viewStart / grid) * grid; b <= viewStart + viewBeats; b += grid)
            {
                g.setColour (Colour (0xff1a1d26));
                g.drawVerticalLine ((int) xForBeat (b), (float) ga.getY(), (float) ga.getBottom());
            }
        for (double b = std::floor (viewStart); b <= viewStart + viewBeats; b += 1.0)
        {
            const bool bar = std::fmod (b, 4.0) < 0.001;
            if (! bar && pxPerBeat < 10.0) continue;
            g.setColour (bar ? col::border.brighter (0.25f) : Colour (0xff20242e));
            g.drawVerticalLine ((int) xForBeat (b), (float) ga.getY(), (float) ga.getBottom());
        }

        // past the end of the pattern
        const float endX = xForBeat (m.lengthBeats);
        if (endX < (float) ga.getRight())
        {
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRect (Rectangle<float> (endX, (float) ga.getY(), (float) ga.getRight() - endX, (float) ga.getHeight()));
        }

        // ---- the lines ----
        for (auto& l : m.lines)
        {
            const bool sel = page.selection.count (l.id) > 0;
            const Colour c = sel ? selCol : noteColour (l.startPitch());

            // ---- automation, drawn as soft ribbons around each line ----
            const float lx0 = xForBeat (l.start()), lx1 = xForBeat (l.end());
            // velocity: a wedge at the start of the note, as tall as the hit is hard, fading along it
            if (page.showVelocity)
            {
                const float fadeLen = jmax (12.0f, jmin ((lx1 - lx0) * 0.35f, xForBeat (l.start() + 1.0) - lx0));
                Path wedge;
                wedge.startNewSubPath (lx0, yForPitch (l.startPitch()));
                for (float x = lx0; x <= lx0 + fadeLen && x <= lx1; x += 2.0f)
                {
                    const float k = 1.0f - (x - lx0) / fadeLen;
                    wedge.lineTo (x, yForPitch (l.pitchAt (beatForX (x))) - l.velocity * rh * 2.6f * k * k);
                }
                for (float x = jmin (lx0 + fadeLen, lx1); x >= lx0; x -= 2.0f)
                    wedge.lineTo (x, yForPitch (l.pitchAt (beatForX (x))));
                wedge.closeSubPath();
                g.setColour (velCol.withAlpha (0.30f));
                g.fillPath (wedge);
                g.setColour (velCol.withAlpha (0.65f));
                g.strokePath (wedge, PathStrokeType (0.8f));
            }
            // pressure: a band above the line, as thick as the pressure
            if (page.showPressure && ! l.pressure.empty())
            {
                Path area;
                bool first = true;
                for (float x = lx0; x <= lx1; x += 3.0f)
                {
                    const double b = beatForX (x);
                    const float y = yForPitch (l.pitchAt (b)) - l.pressureAt (b) * rh * 3.0f;
                    if (first) { area.startNewSubPath (x, y); first = false; } else area.lineTo (x, y);
                }
                for (float x = lx1; x >= lx0; x -= 3.0f) area.lineTo (x, yForPitch (l.pitchAt (beatForX (x))));
                area.closeSubPath();
                g.setColour (pressCol.withAlpha (0.26f));
                g.fillPath (area);
                g.setColour (pressCol.withAlpha (0.7f));
                g.strokePath (area, PathStrokeType (0.8f));
            }
            // slide: a band between the line and the slide amount (above = right, below = left)
            if (page.showSlide && ! l.slide.empty())
            {
                Path band, trace;
                bool first = true;
                for (float x = lx0; x <= lx1; x += 3.0f)
                {
                    const double b = beatForX (x);
                    const float y = yForPitch (l.pitchAt (b)) - (l.slideAt (b) - 0.5f) * rh * 4.0f;
                    if (first) { band.startNewSubPath (x, y); trace.startNewSubPath (x, y); first = false; }
                    else { band.lineTo (x, y); trace.lineTo (x, y); }
                }
                for (float x = lx1; x >= lx0; x -= 3.0f) band.lineTo (x, yForPitch (l.pitchAt (beatForX (x))));
                band.closeSubPath();
                g.setColour (slideCol.withAlpha (0.20f));
                g.fillPath (band);
                g.setColour (slideCol.withAlpha (0.85f));
                g.strokePath (trace, PathStrokeType (1.2f));
            }

            if (m.modShape > 0)
            {
                g.setColour (c.withAlpha (0.35f));
                g.strokePath (linePath (l, true), PathStrokeType (1.0f));
            }
            auto path = linePath (l, false);
            const double playBeat = page.engine.playheadBeat.load();
            const bool sounding = page.engine.transportRunning.load() && playBeat >= l.start() && playBeat < l.end();

            // how loud this note is actually ringing (strike = bright, then it fades as the bowl dies away)
            const float ring = sounding ? page.ringLevel (juce::jlimit (0, 127, roundToInt (l.startPitch()))) : 0.0f;

            // glow: soft layers fading outwards, swelling with the sound
            const float boost = 1.0f + 2.2f * ring;
            const float widths[] = { 16.0f, 10.0f, 6.0f };
            const float alphas[] = { 0.035f, 0.07f, 0.14f };
            for (int k = 0; k < 3; ++k)
            {
                g.setColour (c.withAlpha (jmin (1.0f, alphas[k] * boost)));
                g.strokePath (path, PathStrokeType (widths[k] * (1.0f + 0.4f * ring), PathStrokeType::curved, PathStrokeType::rounded));
            }

            // while it rings, a rippling halo travels along it - like the rings in the circle of fifths
            if (ring > 0.01f)
            {
                const double tnow = Time::getMillisecondCounterHiRes() * 0.001;
                const float amp = 2.0f + 6.0f * ring;
                const int lobes = 3 + ((int) std::round (l.startPitch())) % 12;   // each note ripples its own way
                for (int side = 0; side < 2; ++side)
                {
                    Path ripple;
                    bool first = true;
                    const float rx0 = jmax (xForBeat (l.start()), (float) ga.getX()), rx1 = jmin (xForBeat (l.end()), (float) ga.getRight());
                    for (float x = rx0; x <= rx1; x += 2.0f)
                    {
                        const float w = std::sin (x * 0.012f * (float) lobes + (float) tnow * 5.0f * (side == 0 ? 1.0f : -0.7f) + (float) l.id);
                        const float y = yForPitch (l.pitchAt (beatForX (x))) + (side == 0 ? -1.0f : 1.0f) * amp * (0.6f + 0.4f * w);
                        if (first) { ripple.startNewSubPath (x, y); first = false; } else ripple.lineTo (x, y);
                    }
                    g.setColour (c.withAlpha (0.10f + 0.15f * ring));
                    g.strokePath (ripple, PathStrokeType (4.0f + 3.0f * ring));
                    g.setColour (c.withAlpha (0.35f + 0.5f * ring));
                    g.strokePath (ripple, PathStrokeType (1.0f + 1.2f * ring));
                }
            }
            g.setColour (c);
            g.strokePath (path, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colours::white.withAlpha (sounding ? 0.85f : 0.45f));   // hot white core
            g.strokePath (path, PathStrokeType (0.8f, PathStrokeType::curved, PathStrokeType::rounded));

            // glitter: specks scattered along the line that twinkle on and off
            if (page.showGlitter)
            {
                const double now = Time::getMillisecondCounterHiRes() * 0.001;
                const float x0 = jmax (xForBeat (l.start()), (float) ga.getX());
                const float x1 = jmin (xForBeat (l.end()), (float) ga.getRight());
                const int count = jlimit (0, 120, (int) ((x1 - x0) / (sounding ? 7.0f : 11.0f)));
                for (int k = 0; k < count; ++k)
                {
                    // fixed per speck (so they don't jump around), different for every line
                    auto hash = [] (juce::uint32 v) { v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16; return (float) (v & 0xffffff) / 16777215.0f; };
                    const auto seed = (juce::uint32) (l.id * 7919 + k * 104729);
                    const float px = x0 + hash (seed) * (x1 - x0);
                    const float drift = (hash (seed + 1) - 0.5f) * 9.0f;
                    const float speed = 0.6f + hash (seed + 2) * 1.8f;
                    const float tw = std::sin ((float) now * speed * MathConstants<float>::twoPi + hash (seed + 3) * 6.283f);
                    const float bright = std::pow (jmax (0.0f, tw), 6.0f) * (0.6f + 0.4f * jmin (1.0f, ring * 2.0f + (sounding ? 0.4f : 0.0f)));
                    if (bright < 0.04f) continue;
                    const float py = yForPitch (l.pitchAt (beatForX (px))) + drift;
                    const float tint = hash (seed + 4);
                    auto speck = tint < 0.6f ? Colours::white : tint < 0.85f ? c.brighter (0.6f) : velCol.brighter (0.4f);
                    const float size = 1.2f + 2.4f * bright * (0.5f + hash (seed + 5));
                    g.setColour (speck.withAlpha (0.25f * bright));
                    g.fillEllipse (Rectangle<float> (size * 3.0f, size * 3.0f).withCentre ({ px, py }));
                    g.setColour (speck.withAlpha (bright));
                    g.fillEllipse (Rectangle<float> (size, size).withCentre ({ px, py }));
                    if (bright > 0.55f)   // the brightest ones flash into little stars
                    {
                        g.setColour (speck.withAlpha (0.6f * bright));
                        g.drawLine (px - size * 2.2f, py, px + size * 2.2f, py, 0.7f);
                        g.drawLine (px, py - size * 2.2f, px, py + size * 2.2f, 0.7f);
                    }
                }
            }

            // a bright spark riding the line where it's playing right now
            if (sounding)
            {
                const Point<float> spark (xForBeat (playBeat), yForPitch (l.pitchAt (playBeat)));
                g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.9f), spark.x, spark.y, c.withAlpha (0.0f), spark.x + 14.0f, spark.y, true));
                g.fillEllipse (Rectangle<float> (28.0f, 28.0f).withCentre (spark));
                g.setColour (Colours::white);
                g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (spark));
            }

            for (int n = 0; n < (int) l.nodes.size(); ++n)
            {
                const auto& nd = l.nodes[(size_t) n];
                const Point<float> np (xForBeat (nd.beat), yForPitch (nd.pitch));
                const bool hot = hover.lineId == l.id && hover.node == n;
                const float r = n == 0 ? 6.0f + l.velocity * 3.0f : (n == (int) l.nodes.size() - 1 ? 3.5f : 5.0f);
                g.setColour (col::bg);
                g.fillEllipse (Rectangle<float> (r * 2, r * 2).withCentre (np));
                g.setColour (hot ? Colours::white : c.brighter (0.3f));
                g.drawEllipse (Rectangle<float> (r * 2, r * 2).withCentre (np), 1.6f);
                if (n == 0)
                {
                    g.setColour (c.withAlpha (0.6f));
                    g.fillEllipse (Rectangle<float> (r, r).withCentre (np));
                }
            }
        }

        // legend (click to show / hide each kind of automation)
        {
            auto lg = legendArea();
            g.setColour (col::panel.withAlpha (0.85f));
            g.fillRoundedRectangle (lg, 5.0f);
            g.setColour (col::border);
            g.drawRoundedRectangle (lg, 5.0f, 1.0f);
            const std::pair<const char*, Colour> items[] = { { "Bend", noteColour (55) }, { "Velocity", velCol }, { "Pressure", pressCol }, { "Slide", slideCol }, { "Glitter", Colours::white } };
            const bool on[] = { true, page.showVelocity, page.showPressure, page.showSlide, page.showGlitter };
            g.setFont (font (10.0f));
            for (int i = 0; i < 5; ++i)
            {
                auto row = Rectangle<float> (lg.getX() + 6.0f, lg.getY() + 4.0f + (float) i * 14.0f, lg.getWidth() - 10.0f, 14.0f);
                auto box = row.removeFromLeft (10.0f).withSizeKeepingCentre (9.0f, 9.0f);
                g.setColour (items[i].second.withAlpha (on[i] ? 0.9f : 0.25f));
                if (on[i]) g.fillRoundedRectangle (box, 2.0f); else g.drawRoundedRectangle (box, 2.0f, 1.0f);
                g.setColour ((on[i] ? col::text : col::dim));
                g.drawText (items[i].first, row.withTrimmedLeft (5.0f), Justification::centredLeft);
            }
        }

        // playhead
        const double ph = page.engine.playheadBeat.load();
        if (ph >= 0.0)
        {
            const float x = xForBeat (ph);
            g.setColour (Colour (0xfff5c542));
            g.drawVerticalLine ((int) x, (float) ga.getY(), (float) ga.getBottom());
        }

        if (drag == Drag::Marquee)
        {
            g.setColour (selCol.withAlpha (0.12f));
            g.fillRect (marquee);
            g.setColour (selCol.withAlpha (0.7f));
            g.drawRect (marquee, 1.0f);
        }
    }

    // ---- ruler ----
    auto ruler = Rectangle<int> (ga.getX(), 0, ga.getWidth(), 22);
    g.setColour (col::panel);
    g.fillRect (ruler);
    g.setFont (font (10.0f));
    for (double b = std::floor (viewStart / 4.0) * 4.0; b <= viewStart + viewBeats; b += 4.0)
    {
        const float x = xForBeat (b);
        if (x < (float) ga.getX() - 1) continue;
        g.setColour (col::dim);
        g.drawVerticalLine ((int) x, 12.0f, 22.0f);
        g.drawText (String ((int) (b / 4.0) + 1), (int) x + 3, 2, 30, 12, Justification::left);
    }
    {
        const float x = xForBeat (page.model.lengthBeats);
        Path tri; tri.addTriangle (x - 5, 12, x + 5, 12, x, 21);
        g.setColour (selCol);
        g.fillPath (tri);
    }
    const double ph = page.engine.playheadBeat.load();
    if (ph >= 0.0)
    {
        const float x = xForBeat (ph);
        Path tri; tri.addTriangle (x - 5, 2, x + 5, 2, x, 11);
        g.setColour (Colour (0xfff5c542));
        g.fillPath (tri);
    }

    // ---- keyboard strip ----
    auto keys = Rectangle<int> (0, ga.getY(), 46, ga.getHeight());
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (keys);
    for (int p = (int) std::floor (viewLowPitch) - 1; p <= (int) (viewLowPitch + viewPitches) + 1; ++p)
    {
        static const bool black[] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
        const bool bk = black[((p % 12) + 12) % 12];
        const float y = yForPitch ((float) p) - rh * 0.5f;
        auto kr = Rectangle<float> (0.0f, y, bk ? 30.0f : 46.0f, rh).reduced (0.0f, 0.5f);
        g.setColour (bk ? Colour (0xff1a1c22) : Colour (0xffd9dde6));
        g.fillRect (kr);
        if (p % 12 == 0)
        {
            g.setColour (Colour (0xff303540));
            g.setFont (font (9.5f));
            g.drawText (pitchName ((float) p), kr.withTrimmedLeft (22.0f).toNearestInt(), Justification::centredLeft);
        }
    }
}

// ---------------------------------------------------------------------------- mouse
void RollCanvas::mouseMove (const MouseEvent& e)
{
    auto h = hitTest (e.position);
    if (h.lineId != hover.lineId || h.node != hover.node) { hover = h; repaint(); }
    setMouseCursor (h.node >= 0 ? MouseCursor::DraggingHandCursor
                  : h.lineId >= 0 ? (page.tool == RollPage::Tool::Draw ? MouseCursor::CrosshairCursor : MouseCursor::PointingHandCursor)
                  : (page.tool == RollPage::Tool::Draw ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor));
}

void RollCanvas::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    downPos = e.position;
    beforeDrag = page.model;
    lastPaintBeat = -1.0;
    drag = Drag::None;

    if (e.x < 46 && e.y > 22)   // keyboard strip: audition
    {
        page.audition ((float) std::round (pitchForY (e.position.y)));
        return;
    }
    if (! gridArea().contains (e.getPosition())) return;

    if (legendArea().contains (e.position))   // show / hide automation kinds
    {
        const int row = (int) ((e.position.y - legendArea().getY() - 4.0f) / 14.0f);
        if (row == 1) page.showVelocity = ! page.showVelocity;
        if (row == 2) page.showPressure = ! page.showPressure;
        if (row == 3) page.showSlide = ! page.showSlide;
        if (row == 4) page.showGlitter = ! page.showGlitter;
        repaint();
        return;
    }

    auto h = hitTest (e.position);
    auto& m = page.model;

    // ---- right-click menu on a line ----
    if (e.mods.isPopupMenu() && h.lineId >= 0 && ! e.mods.isAltDown())
    {
        const int id = h.lineId;
        PopupMenu menu;
        menu.addItem (1, "Delete note");
        menu.addItem (2, "Duplicate");
        menu.addItem (3, "Flatten (remove bends)");
        menu.addSeparator();
        menu.addItem (4, "Clear pressure");
        menu.addItem (5, "Clear slide");
        menu.addSeparator();
        menu.addItem (6, "Add a pressure swell");
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, id] (int r)
        {
            auto& mm = page.model;
            auto* l = mm.find (id);
            if (l == nullptr || r == 0) return;
            page.beginEdit();
            if (r == 1) { mm.lines.erase (std::remove_if (mm.lines.begin(), mm.lines.end(), [id] (auto& x) { return x.id == id; }), mm.lines.end()); page.selection.erase (id); }
            if (r == 2)
            {
                auto copy = *l;
                const double len = copy.end() - copy.start();
                copy.id = mm.nextId++;
                for (auto& n : copy.nodes) n.beat += len;
                for (auto& p : copy.pressure) p.beat += len;
                for (auto& p : copy.slide) p.beat += len;
                mm.lines.push_back (copy);
            }
            if (r == 3) for (auto& n : l->nodes) { n.pitch = l->nodes.front().pitch; n.curve = 0; }
            if (r == 4) l->pressure.clear();
            if (r == 5) l->slide.clear();
            if (r == 6)
            {
                const double s = l->start(), len = l->end() - l->start();
                l->pressure = { { s, 0.0f }, { s + len * 0.6, 1.0f }, { l->end(), 0.2f } };
            }
            page.publish();
            repaint();
        });
        return;
    }

    page.beginEdit();

    // ---- lanes other than pitch ----
    if (page.lane == RollPage::Lane::Velocity)
    {
        if (h.lineId >= 0)
        {
            if (! page.selection.count (h.lineId)) { page.selection = { h.lineId }; }
            drag = Drag::Velocity;
        }
        repaint();
        return;
    }
    if (page.lane == RollPage::Lane::Pressure || page.lane == RollPage::Lane::Slide)
    {
        drag = Drag::Paint;
        mouseDrag (e);
        return;
    }

    // ---- pitch lane ----
    if (h.node >= 0)
    {
        dragLine = h.lineId; dragNode = h.node;
        if (! page.selection.count (h.lineId) && ! e.mods.isShiftDown()) page.selection = { h.lineId };
        drag = Drag::Node;
        if (auto* l = m.find (dragLine)) page.audition (l->nodes[(size_t) dragNode].pitch, l->velocity);
        return;
    }
    if (h.lineId >= 0 && (e.mods.isAltDown() || e.mods.isPopupMenu()))
    {
        dragLine = h.lineId; dragNode = h.segment;
        if (auto* l = m.find (dragLine)) downCurve = l->nodes[(size_t) dragNode].curve;
        drag = Drag::Curve;
        return;
    }
    if (h.lineId >= 0)
    {
        if (page.tool == RollPage::Tool::Draw)
        {
            // add a bend point here and drag it
            if (auto* l = m.find (h.lineId))
            {
                const double b = page.snapBeat (beatForX (e.position.x));
                if (b > l->start() + 1.0e-3 && b < l->end() - 1.0e-3)
                {
                    roll::Node nd { b, l->pitchAt (b), 0.0f };
                    l->nodes.insert (l->nodes.begin() + h.segment + 1, nd);
                    dragLine = l->id; dragNode = h.segment + 1;
                    page.selection = { l->id };
                    drag = Drag::Node;
                    page.publish();
                }
            }
            return;
        }
        if (e.mods.isShiftDown())
        {
            if (page.selection.count (h.lineId)) page.selection.erase (h.lineId); else page.selection.insert (h.lineId);
        }
        else if (! page.selection.count (h.lineId)) page.selection = { h.lineId };
        drag = Drag::MoveLines;
        if (auto* l = m.find (h.lineId)) page.audition (l->startPitch(), l->velocity);
        repaint();
        return;
    }

    // ---- empty space ----
    if (page.tool == RollPage::Tool::Draw)
    {
        roll::Line l;
        l.id = m.nextId++;
        const double start = juce::jmax (0.0, page.snapBeat (beatForX (e.position.x)));
        const float pitch = juce::jlimit (0.0f, 127.0f, (float) std::round (pitchForY (e.position.y)));
        const double g = page.gridBeats() > 0 ? page.gridBeats() : 0.25;
        l.nodes = { { start, pitch, 0.0f }, { start + g, pitch, 0.0f } };
        m.lines.push_back (l);
        page.selection = { l.id };
        dragLine = l.id; dragNode = 1;
        drag = Drag::NewLine;
        page.audition (pitch);
        page.publish();
        repaint();
    }
    else
    {
        if (! e.mods.isShiftDown()) page.selection.clear();
        drag = Drag::Marquee;
        marquee = {};
        repaint();
    }
}

void RollCanvas::mouseDrag (const MouseEvent& e)
{
    auto& m = page.model;
    const bool fine = e.mods.isShiftDown();
    switch (drag)
    {
        case Drag::NewLine:
        {
            if (auto* l = m.find (dragLine))
            {
                const double g = page.gridBeats() > 0 ? page.gridBeats() : 1.0 / 64.0;
                double b = page.snapBeat (beatForX (e.position.x));
                b = juce::jmax (l->nodes.front().beat + g, b);
                l->nodes.back().beat = b;
                l->nodes.back().pitch = l->nodes.front().pitch;   // new notes start flat; bend them after
            }
            break;
        }
        case Drag::Node:
        {
            if (auto* l = m.find (dragLine))
            {
                auto& nodes = l->nodes;
                const int i = dragNode;
                const double lo = i > 0 ? nodes[(size_t) i - 1].beat + 1.0e-3 : 0.0;
                const double hi = i + 1 < (int) nodes.size() ? nodes[(size_t) i + 1].beat - 1.0e-3 : 1.0e9;
                double b = fine ? beatForX (e.position.x) : page.snapBeat (beatForX (e.position.x));
                nodes[(size_t) i].beat = juce::jlimit (lo, hi, b);
                float p = pitchForY (e.position.y);
                if (page.snapPitch && ! fine) p = std::round (p);
                nodes[(size_t) i].pitch = juce::jlimit (0.0f, 127.0f, p);
                page.audition (nodes[(size_t) i].pitch, l->velocity);
                page.setHint (pitchName (nodes[(size_t) i].pitch) + (std::abs (p - std::round (p)) > 0.01f ? String::formatted (" %+d ct", (int) std::round ((p - std::round (p)) * 100.0f)) : String())
                              + "   beat " + String (nodes[(size_t) i].beat + 1.0, 2));
            }
            break;
        }
        case Drag::Curve:
        {
            if (auto* l = m.find (dragLine))
            {
                auto& n = l->nodes[(size_t) dragNode];
                const float dir = l->nodes[(size_t) dragNode + 1].pitch >= n.pitch ? -1.0f : 1.0f;
                n.curve = juce::jlimit (-1.0f, 1.0f, downCurve + dir * (e.position.y - downPos.y) * 0.01f);
                page.setHint ("Curve " + String (n.curve, 2));
            }
            break;
        }
        case Drag::MoveLines:
        {
            double db = beatForX (e.position.x) - beatForX (downPos.x);
            float dp = pitchForY (e.position.y) - pitchForY (downPos.y);
            if (! fine) { const double g = page.gridBeats(); if (g > 0) db = std::round (db / g) * g; }
            if (page.snapPitch && ! fine) dp = std::round (dp);
            m = beforeDrag;
            for (auto& l : m.lines)
                if (page.selection.count (l.id))
                {
                    const double shift = juce::jmax (-l.start(), db);
                    for (auto& n : l.nodes) { n.beat += shift; n.pitch = juce::jlimit (0.0f, 127.0f, n.pitch + dp); }
                    for (auto& p : l.pressure) p.beat += shift;
                    for (auto& p : l.slide) p.beat += shift;
                }
            break;
        }
        case Drag::Marquee:
        {
            marquee = Rectangle<float> (downPos, e.position);
            page.selection.clear();
            for (auto& l : m.lines)
                for (auto& n : l.nodes)
                    if (marquee.contains (xForBeat (n.beat), yForPitch (n.pitch))) { page.selection.insert (l.id); break; }
            break;
        }
        case Drag::Velocity:
        {
            const float dv = -(e.position.y - downPos.y) / 150.0f;
            m = beforeDrag;
            for (auto& l : m.lines)
                if (page.selection.count (l.id)) l.velocity = juce::jlimit (0.02f, 1.0f, l.velocity + dv);
            if (auto* l = m.find (*page.selection.begin())) page.setHint ("Velocity " + String (juce::roundToInt (l->velocity * 127.0f)));
            break;
        }
        case Drag::Paint:
        {
            // paint pressure / slide onto the line under the mouse
            const double b = beatForX (e.position.x);
            roll::Line* best = nullptr;
            float bestD = 1.0e9f;
            for (auto& l : m.lines)
            {
                if (b < l.start() || b > l.end()) continue;
                const float d = std::abs (yForPitch (l.pitchAt (b)) - e.position.y);
                const float limit = rowHeight() * 6.0f;
                if (d < bestD && d < limit) { bestD = d; best = &l; }
            }
            if (best != nullptr)
            {
                const float yLine = yForPitch (best->pitchAt (b));
                const bool pressure = page.lane == RollPage::Lane::Pressure;
                const float value = pressure ? juce::jlimit (0.0f, 1.0f, (yLine - e.position.y) / (rowHeight() * 3.0f))
                                             : juce::jlimit (0.0f, 1.0f, 0.5f + (yLine - e.position.y) / (rowHeight() * 4.0f));
                auto& pts = pressure ? best->pressure : best->slide;
                const double lo = lastPaintBeat >= 0 ? juce::jmin (lastPaintBeat, b) : b - 1.0e-4;
                const double hi = lastPaintBeat >= 0 ? juce::jmax (lastPaintBeat, b) : b + 1.0e-4;
                pts.erase (std::remove_if (pts.begin(), pts.end(), [lo, hi] (const roll::Point& p) { return p.beat > lo && p.beat <= hi; }), pts.end());
                if (pts.empty())   // anchor the ends so the curve covers the whole note
                {
                    pts.push_back ({ best->start(), pressure ? 0.0f : 0.5f });
                    pts.push_back ({ best->end(), pressure ? 0.0f : 0.5f });
                }
                pts.push_back ({ juce::jlimit (best->start(), best->end(), b), value });
                std::sort (pts.begin(), pts.end(), [] (auto& a, auto& c) { return a.beat < c.beat; });
                lastPaintBeat = b;
                page.setHint ((pressure ? "Pressure " : "Slide ") + String (juce::roundToInt (value * 100.0f)) + "%");
            }
            break;
        }
        case Drag::None: break;
    }
    page.publish();
    repaint();
}

void RollCanvas::mouseUp (const MouseEvent&)
{
    if (drag == Drag::NewLine || drag == Drag::Node || drag == Drag::MoveLines)
        for (auto& l : page.model.lines) l.sortNodes();
    drag = Drag::None;
    page.stopAudition();
    page.publish();
    repaint();
}

void RollCanvas::mouseDoubleClick (const MouseEvent& e)
{
    auto h = hitTest (e.position);
    if (h.node >= 0)
        if (auto* l = page.model.find (h.lineId); l != nullptr && l->nodes.size() > 2 && h.node > 0 && h.node < (int) l->nodes.size() - 1)
        {
            page.beginEdit();
            l->nodes.erase (l->nodes.begin() + h.node);   // remove a bend point
            page.publish();
            repaint();
        }
}

void RollCanvas::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX);
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = beatForX (e.position.x);
        viewBeats = juce::jlimit (0.5, 256.0, viewBeats * (d > 0 ? 0.85 : 1.18));
        viewStart = juce::jmax (0.0, anchor - (e.position.x - (float) gridArea().getX()) / gridArea().getWidth() * viewBeats);
    }
    else if (e.mods.isAltDown())
    {
        const float anchor = pitchForY (e.position.y);
        viewPitches = juce::jlimit (8.0f, 96.0f, viewPitches * (d > 0 ? 0.88f : 1.14f));
        viewLowPitch = juce::jlimit (0.0f, 128.0f - viewPitches, anchor - viewPitches * 0.5f);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        viewStart = juce::jmax (0.0, viewStart - d * viewBeats * 0.25);
    else
        viewLowPitch = juce::jlimit (0.0f, 128.0f - viewPitches, viewLowPitch + d * 6.0f);
    updateScrollBars();
    repaint();
}

// ============================================================================ page
RollPage::RollPage (SolIntentProcessor& p) : engine (p.roll), proc (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    model = *engine.getPattern();
    addAndMakeVisible (canvas);

    for (auto* b : { &drawBtn, &selectBtn, &cutBtn, &copyBtn, &pasteBtn, &undoBtn, &redoBtn, &openBtn, &saveBtn, &loopBtn,
                     &quantBtn, &exportBtn, &clearBtn, &playBtn, &stopBtn, &recBtn, &keysBtn, &polarityBtn, &snapBtn })
    {
        b->setColour (TextButton::buttonOnColourId, col::wavetable.withAlpha (0.35f));
        addAndMakeVisible (b);
    }
    recBtn.setColour (TextButton::buttonOnColourId, col::filter.withAlpha (0.5f));

    drawBtn.onClick = [this] { setTool (Tool::Draw); };
    selectBtn.onClick = [this] { setTool (Tool::Select); };
    cutBtn.onClick = [this] { copySelection (true); };
    copyBtn.onClick = [this] { copySelection (false); };
    pasteBtn.onClick = [this] { paste(); };
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    openBtn.onClick = [this] { openPattern(); };
    saveBtn.onClick = [this] { savePattern(); };
    loopBtn.onClick = [this] { beginEdit(); model.loop = ! model.loop; publish(); refreshControls(); };
    quantBtn.onClick = [this] { quantise(); };
    exportBtn.onClick = [this] { exportMidi(); };
    clearBtn.onClick = [this] { clearAll(); };
    playBtn.onClick = [this] { if (engine.isInternalPlaying()) engine.pause(); else engine.play(); refreshControls(); };
    stopBtn.onClick = [this] { engine.stop(); refreshControls(); };
    recBtn.onClick = [this] { engine.recording = ! engine.recording.load(); refreshControls(); };
    keysBtn.setClickingTogglesState (true);
    keysBtn.setToggleState (true, dontSendNotification);
    keysBtn.setTooltip ("Hear notes while you draw and drag them");
    snapBtn.setClickingTogglesState (true);
    snapBtn.setToggleState (true, dontSendNotification);
    snapBtn.onClick = [this] { snapPitch = snapBtn.getToggleState(); };
    snapBtn.setTooltip ("Snap bend points to semitones (hold Shift while dragging to go free)");
    polarityBtn.onClick = [this] { beginEdit(); model.bipolar = ! model.bipolar; publish(); refreshControls(); };
    recBtn.setTooltip ("Record what you play (with pitch bend and pressure) while the pattern runs");
    playBtn.setTooltip ("Play the pattern on its own. When your DAW is playing, the pattern follows it.");
    exportBtn.setTooltip ("Save the pattern as an MPE MIDI file (for Logic, Ableton, Bitwig...)");
    quantBtn.setTooltip ("Snap the starts and ends of the selected notes (or all notes) to the grid");

    for (int i = 0; i < 7; ++i) gridBox.addItem (gridNames[i], i + 1);
    gridBox.setSelectedItemIndex (2, dontSendNotification);
    gridBox.onChange = [this] { canvas.repaint(); };
    modShapeBox.addItemList ({ "None", "Sine", "Triangle", "Saw", "Square", "Random" }, 1);
    modShapeBox.onChange = [this] { if (updating) return; beginEdit(); model.modShape = modShapeBox.getSelectedItemIndex(); publish(); canvas.repaint(); };
    for (int i = 0; i < 7; ++i) modSpeedBox.addItem (speedNames[i], i + 1);
    modSpeedBox.onChange = [this] { if (updating) return; beginEdit(); model.modRateBeats = speedValues[juce::jmax (0, modSpeedBox.getSelectedItemIndex())]; publish(); canvas.repaint(); };
    laneBox.addItemList ({ "Pitch", "Velocity", "Pressure", "Slide" }, 1);
    laneBox.setSelectedItemIndex (0, dontSendNotification);
    laneBox.onChange = [this]
    {
        lane = (Lane) laneBox.getSelectedItemIndex();
        static const char* hints[] = {
            "PITCH - Draw: click empty space for a note, click a line to add a bend point, drag points to bend. Alt-drag a line to curve it. Double-click a point to remove it.",
            "VELOCITY - drag a note up or down to change how hard it's hit.",
            "PRESSURE - paint above a note to swell it (route Aftertouch in the mod matrix, or use the presets that already do).",
            "SLIDE - paint above or below a note to move its Slide (MPE) mod source." };
        setHint (hints[(int) lane]);
    };
    for (auto* c : { &gridBox, &modShapeBox, &modSpeedBox, &laneBox }) addAndMakeVisible (c);

    barsSlider.setSliderStyle (Slider::IncDecButtons);
    barsSlider.setRange (1, 64, 1);
    barsSlider.setTextBoxStyle (Slider::TextBoxLeft, false, 34, 22);
    barsSlider.onValueChange = [this] { if (updating) return; beginEdit(); model.lengthBeats = barsSlider.getValue() * 4.0; publish(); canvas.updateScrollBars(); canvas.repaint(); };
    amountSlider.setSliderStyle (Slider::LinearHorizontal);
    amountSlider.setRange (0, 200, 1);
    amountSlider.setTextBoxStyle (Slider::TextBoxRight, false, 34, 20);
    amountSlider.setTooltip ("Pitch Mod depth in cents (100 = a semitone)");
    amountSlider.onValueChange = [this] { if (updating) return; model.modAmount = (float) amountSlider.getValue(); publish(); canvas.repaint(); };
    fadeSlider.setSliderStyle (Slider::LinearHorizontal);
    fadeSlider.setRange (0.0, 1.0, 0.01);
    fadeSlider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    fadeSlider.setTooltip ("How long the wobble takes to fade in, as a share of each note");
    fadeSlider.onValueChange = [this] { if (updating) return; model.modFade = (float) fadeSlider.getValue(); publish(); canvas.repaint(); };
    for (auto* s : { &barsSlider, &amountSlider, &fadeSlider })
    {
        s->setColour (Slider::trackColourId, col::wavetable);
        s->setColour (Slider::textBoxTextColourId, col::text);
        addAndMakeVisible (s);
    }

    const std::pair<Label*, const char*> labels[] = { { &barsL, "Bars" }, { &modL, "Pitch Mod" }, { &amountL, "Amount" },
                                                      { &speedL, "Mod Speed" }, { &fadeL, "Fade" }, { &gridL, "Grid" }, { &laneL, "Edit" } };
    for (auto& [l, t] : labels)
    {
        l->setText (t, dontSendNotification);
        l->setFont (font (10.5f));
        l->setColour (Label::textColourId, col::dim);
        l->setJustificationType (Justification::centredLeft);
        addAndMakeVisible (l);
    }

    setTool (Tool::Draw);
    laneBox.onChange();
    refreshControls();
    startTimerHz (30);
}

RollPage::~RollPage() { stopTimer(); }

void RollPage::visibilityChanged()
{
    if (isVisible())
    {
        model = *engine.getPattern();
        refreshControls();
        canvas.fitToPattern();
        grabKeyboardFocus();
    }
}

void RollPage::resized()
{
    // three toolbar rows so it fits the Sol Intent window: editing / transport & files / pitch wobble
    auto r = getLocalBounds();
    toolbarArea = r.removeFromTop (118);
    statusArea = r.removeFromBottom (20);
    r.removeFromTop (4);
    canvas.setBounds (r);

    auto t = toolbarArea.reduced (8, 6);
    auto row1 = t.removeFromTop (28);
    t.removeFromTop (4);
    auto row2 = t.removeFromTop (28);
    t.removeFromTop (4);
    auto row3 = t;
    auto place = [] (Rectangle<int>& row, Component& c, int w, int gapAfter = 4) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gapAfter); };

    place (row1, drawBtn, 56); place (row1, selectBtn, 60, 10);
    place (row1, cutBtn, 46); place (row1, copyBtn, 52); place (row1, pasteBtn, 54, 10);
    place (row1, undoBtn, 52); place (row1, redoBtn, 52, 10);
    place (row1, snapBtn, 52, 10);
    place (row1, gridL, 30, 2); place (row1, gridBox, 74, 4);
    place (row1, quantBtn, 72, 10);
    laneBox.setBounds (row1.removeFromRight (96));
    row1.removeFromRight (2);
    laneL.setBounds (row1.removeFromRight (30));

    place (row2, playBtn, 56); place (row2, stopBtn, 52); place (row2, recBtn, 48); place (row2, keysBtn, 50, 10);
    place (row2, loopBtn, 52, 10);
    place (row2, barsL, 30, 2); place (row2, barsSlider, 92, 10);
    clearBtn.setBounds (row2.removeFromRight (56));
    row2.removeFromRight (4);
    exportBtn.setBounds (row2.removeFromRight (94));
    row2.removeFromRight (4);
    saveBtn.setBounds (row2.removeFromRight (52));
    row2.removeFromRight (4);
    openBtn.setBounds (row2.removeFromRight (52));

    auto labelled = [&] (Label& l, Component& c, int w)
    {
        auto col = row3.removeFromLeft (w);
        l.setBounds (col.removeFromTop (14));
        c.setBounds (col.reduced (0, 1));
        row3.removeFromLeft (12);
    };
    labelled (modL, modShapeBox, 104);
    labelled (amountL, amountSlider, 170);
    labelled (speedL, modSpeedBox, 84);
    labelled (fadeL, fadeSlider, 130);
    {
        auto col = row3.removeFromLeft (92);
        col.removeFromTop (14);
        polarityBtn.setBounds (col.reduced (0, 1));
    }
}

void RollPage::paint (Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRoundedRectangle (toolbarArea.toFloat(), 8.0f);
    g.setColour (col::border);
    g.drawRoundedRectangle (toolbarArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::dim);
    g.setFont (font (11.0f));
    g.drawFittedText (hint, statusArea.reduced (8, 0), Justification::centredLeft, 1);
}

void RollPage::setTool (Tool t)
{
    tool = t;
    drawBtn.setToggleState (t == Tool::Draw, dontSendNotification);
    selectBtn.setToggleState (t == Tool::Select, dontSendNotification);
    setHint (t == Tool::Draw
        ? "DRAW (D) - click empty space for a note, click a line to add a bend point, drag points to bend. Alt-drag a line to curve it. Right-click a note for more."
        : "SELECT (S) - click or drag a box to select, drag to move, Shift-click to add. Delete removes. Ctrl+C / Ctrl+V copy and paste.");
}

void RollPage::refreshControls()
{
    const ScopedValueSetter<bool> svs (updating, true);
    barsSlider.setValue (std::round (model.lengthBeats / 4.0), dontSendNotification);
    modShapeBox.setSelectedItemIndex (model.modShape, dontSendNotification);
    amountSlider.setValue (model.modAmount, dontSendNotification);
    fadeSlider.setValue (model.modFade, dontSendNotification);
    int speedIdx = 3;
    for (int i = 0; i < 7; ++i) if (std::abs (speedValues[i] - model.modRateBeats) < 1.0e-6) speedIdx = i;
    modSpeedBox.setSelectedItemIndex (speedIdx, dontSendNotification);
    polarityBtn.setButtonText (model.bipolar ? "Bipolar" : "Unipolar");
    loopBtn.setToggleState (model.loop, dontSendNotification);
    playBtn.setButtonText (engine.isInternalPlaying() ? "Pause" : "Play");
    playBtn.setToggleState (engine.isInternalPlaying(), dontSendNotification);
    recBtn.setToggleState (engine.recording.load(), dontSendNotification);
    undoBtn.setEnabled (! undoStack.empty());
    redoBtn.setEnabled (! redoStack.empty());
    pasteBtn.setEnabled (! clipboard.empty());
}

float RollPage::ringLevel (int note) const
{
    return jlimit (0.0f, 1.0f, std::sqrt (proc.noteGlow[(size_t) jlimit (0, 127, note)].load() * 3.0f));
}

double RollPage::gridBeats() const { return gridValues[juce::jlimit (0, 6, gridBox.getSelectedItemIndex())]; }

double RollPage::snapBeat (double b) const
{
    const double g = gridBeats();
    return g > 0 ? std::round (b / g) * g : b;
}

void RollPage::beginEdit()
{
    undoStack.push_back (model);
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
    undoBtn.setEnabled (true);
    redoBtn.setEnabled (false);
}

void RollPage::publish() { engine.setPattern (model); }

void RollPage::audition (float pitch, float vel)
{
    if (keysBtn.getToggleState()) engine.preview (pitch, vel);
}

void RollPage::stopAudition() { engine.previewOff(); }

void RollPage::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (model);
    model = undoStack.back();
    undoStack.pop_back();
    publish(); refreshControls(); canvas.repaint();
}

void RollPage::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (model);
    model = redoStack.back();
    redoStack.pop_back();
    publish(); refreshControls(); canvas.repaint();
}

void RollPage::copySelection (bool cut)
{
    clipboard.clear();
    for (auto& l : model.lines) if (selection.count (l.id)) clipboard.push_back (l);
    if (cut && ! clipboard.empty())
    {
        beginEdit();
        model.lines.erase (std::remove_if (model.lines.begin(), model.lines.end(), [this] (auto& l) { return selection.count (l.id) > 0; }), model.lines.end());
        selection.clear();
        publish();
    }
    refreshControls();
    canvas.repaint();
}

void RollPage::paste()
{
    if (clipboard.empty()) return;
    beginEdit();
    // paste right after the copied notes end
    double first = 1.0e9, last = 0.0;
    for (auto& l : clipboard) { first = juce::jmin (first, l.start()); last = juce::jmax (last, l.end()); }
    const double g = gridBeats() > 0 ? gridBeats() : 0.25;
    const double shift = std::ceil ((last - first) / g) * g;
    selection.clear();
    for (auto l : clipboard)
    {
        l.id = model.nextId++;
        for (auto& n : l.nodes) n.beat += shift;
        for (auto& p : l.pressure) p.beat += shift;
        for (auto& p : l.slide) p.beat += shift;
        model.lines.push_back (l);
        selection.insert (l.id);
    }
    clipboard.clear();
    for (auto& l : model.lines) if (selection.count (l.id)) clipboard.push_back (l);   // paste again continues on
    publish();
    canvas.repaint();
}

void RollPage::deleteSelection()
{
    if (selection.empty()) return;
    beginEdit();
    model.lines.erase (std::remove_if (model.lines.begin(), model.lines.end(), [this] (auto& l) { return selection.count (l.id) > 0; }), model.lines.end());
    selection.clear();
    publish();
    canvas.repaint();
}

void RollPage::selectAll()
{
    selection.clear();
    for (auto& l : model.lines) selection.insert (l.id);
    canvas.repaint();
}

void RollPage::quantise()
{
    const double g = gridBeats();
    if (g <= 0) return;
    beginEdit();
    for (auto& l : model.lines)
    {
        if (! selection.empty() && ! selection.count (l.id)) continue;
        const double shift = std::round (l.start() / g) * g - l.start();
        for (auto& n : l.nodes) n.beat += shift;
        for (auto& p : l.pressure) p.beat += shift;
        for (auto& p : l.slide) p.beat += shift;
        const double end = juce::jmax (l.start() + g, std::round (l.end() / g) * g);
        l.nodes.back().beat = juce::jmax (l.nodes[l.nodes.size() - 2].beat + 1.0e-3, end);
    }
    publish();
    canvas.repaint();
}

void RollPage::clearAll()
{
    if (model.lines.empty()) return;
    beginEdit();
    model.lines.clear();
    selection.clear();
    publish();
    canvas.repaint();
}

static File rollFolder()
{
    auto f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Sol Intent").getChildFile ("Rolls");
    f.createDirectory();
    return f;
}

void RollPage::savePattern()
{
    chooser = std::make_unique<FileChooser> ("Save pattern", rollFolder().getChildFile ("My pattern.ozroll"), "*.ozroll");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
        [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == File()) return;
            if (! f.hasFileExtension ("ozroll")) f = f.withFileExtension ("ozroll");
            if (auto xml = model.toTree().createXml()) xml->writeTo (f);
            setHint ("Saved " + f.getFileName());
        });
}

void RollPage::openPattern()
{
    chooser = std::make_unique<FileChooser> ("Open pattern", rollFolder(), "*.ozroll");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (auto xml = XmlDocument::parse (f))
        {
            beginEdit();
            model = roll::Pattern::fromTree (ValueTree::fromXml (*xml));
            selection.clear();
            publish();
            refreshControls();
            canvas.fitToPattern();
            setHint ("Opened " + f.getFileName());
        }
    });
}

void RollPage::exportMidi()
{
    // MPE MIDI file: channel 1 is the master, each note gets its own channel (2-15) with
    // +/-48 semitone bends, pressure and CC74 slide, so other DAWs keep every bend.
    MidiMessageSequence seq;
    const int tpq = 960;
    seq.addEvent (MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jmax (20.0, engine.currentBpm.load()))), 0);
    for (const auto meta : MidiRPNGenerator::generate (1, 6, 14, false, false)) seq.addEvent (meta.getMessage(), 0);   // MPE lower zone, 14 member channels
    for (int ch = 2; ch <= 15; ++ch)
        for (const auto meta : MidiRPNGenerator::generate (ch, 0, 48, false, false)) seq.addEvent (meta.getMessage(), 0);   // 48 st per-note bend

    auto sorted = model.lines;
    std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b) { return a.start() < b.start(); });
    std::array<double, 16> busyUntil {};
    int nextCh = 2;
    for (auto& l : sorted)
    {
        int ch = -1;
        for (int t = 0; t < 14 && ch < 0; ++t)
        {
            const int c = nextCh;
            nextCh = nextCh >= 15 ? 2 : nextCh + 1;
            if (busyUntil[(size_t) c] <= l.start()) ch = c;
        }
        if (ch < 0) continue;
        busyUntil[(size_t) ch] = l.end();
        const int note = juce::jlimit (0, 127, juce::roundToInt (l.startPitch()));
        auto ticks = [tpq] (double beat) { return beat * tpq; };
        auto bend = [note] (float p) { return juce::jlimit (0, 16383, juce::roundToInt (8192.0f + (p - (float) note) / 48.0f * 8191.0f)); };
        seq.addEvent (MidiMessage::pitchWheel (ch, bend (l.pitchAt (l.start()))), ticks (l.start()));
        seq.addEvent (MidiMessage::noteOn (ch, note, juce::jlimit (0.01f, 1.0f, l.velocity)), ticks (l.start()));
        int lastBend = -1, lastPress = -1, lastSlide = -1;
        for (double b = l.start(); b < l.end(); b += 1.0 / 48.0)
        {
            const int bv = bend (l.pitchAt (b));
            if (bv != lastBend) { seq.addEvent (MidiMessage::pitchWheel (ch, bv), ticks (b)); lastBend = bv; }
            if (! l.pressure.empty())
            {
                const int pv = juce::roundToInt (l.pressureAt (b) * 127.0f);
                if (pv != lastPress) { seq.addEvent (MidiMessage::channelPressureChange (ch, pv), ticks (b)); lastPress = pv; }
            }
            if (! l.slide.empty())
            {
                const int sv = juce::roundToInt (l.slideAt (b) * 127.0f);
                if (sv != lastSlide) { seq.addEvent (MidiMessage::controllerEvent (ch, 74, sv), ticks (b)); lastSlide = sv; }
            }
        }
        seq.addEvent (MidiMessage::noteOff (ch, note), ticks (l.end()));
    }
    seq.updateMatchedPairs();
    seq.sort();

    chooser = std::make_unique<FileChooser> ("Export MPE MIDI", rollFolder().getChildFile ("Sol Intent pattern.mid"), "*.mid");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
        [this, seq, tpq] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == File()) return;
            if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
            MidiFile mf;
            mf.setTicksPerQuarterNote (tpq);
            mf.addTrack (seq);
            f.deleteFile();
            if (auto os = f.createOutputStream()) { mf.writeTo (*os); setHint ("Exported " + f.getFileName() + " (MPE)"); }
        });
}

// ---------------------------------------------------------------------------- recording
void RollPage::drainRecording()
{
    bool changed = false;
    roll::RollEngine::RecEvent e;
    while (engine.popRecorded (e))
    {
        const int ch = juce::jlimit (1, 16, e.channel);
        auto& rl = recLines[(size_t) ch];
        switch (e.type)
        {
            case roll::RollEngine::RecEvent::NoteOn:
            {
                if (! changed) beginEdit();
                roll::Line l;
                l.id = model.nextId++;
                l.velocity = juce::jlimit (0.02f, 1.0f, e.value);
                const float p = (float) e.note + recBend[(size_t) ch];
                l.nodes = { { e.beat, p, 0.0f }, { e.beat + 1.0 / 64.0, p, 0.0f } };
                model.lines.push_back (l);
                rl = { l.id, e.note, e.beat, recBend[(size_t) ch] };
                changed = true;
                break;
            }
            case roll::RollEngine::RecEvent::NoteOff:
            {
                if (auto* l = model.find (rl.lineId); l != nullptr && rl.note == e.note)
                {
                    double end = e.beat;
                    if (end <= l->start()) end = model.lengthBeats;   // wrapped around the loop
                    l->nodes.back().beat = juce::jmax (l->nodes[l->nodes.size() - 2].beat + 1.0e-3, end);
                    l->nodes.back().pitch = (float) rl.note + recBend[(size_t) ch];
                    rl = {};
                    changed = true;
                }
                break;
            }
            case roll::RollEngine::RecEvent::Bend:
            {
                recBend[(size_t) ch] = e.value;
                if (auto* l = model.find (rl.lineId); l != nullptr && e.beat > l->start())
                {
                    const float p = (float) rl.note + e.value;
                    auto& last = l->nodes.back();
                    // keep the moving end point, add a new bend point when the pitch has really moved
                    if (e.beat - rl.lastNodeBeat > 1.0 / 32.0 && std::abs (p - l->nodes[l->nodes.size() - 2].pitch) > 0.08f)
                    {
                        last = { e.beat, p, 0.0f };
                        l->nodes.push_back ({ e.beat + 1.0 / 64.0, p, 0.0f });
                        rl.lastNodeBeat = e.beat;
                    }
                    else
                    {
                        last.pitch = p;
                        last.beat = juce::jmax (last.beat, e.beat);
                    }
                    changed = true;
                }
                break;
            }
            case roll::RollEngine::RecEvent::Pressure:
            case roll::RollEngine::RecEvent::Slide:
            {
                if (auto* l = model.find (rl.lineId); l != nullptr)
                {
                    auto& pts = e.type == roll::RollEngine::RecEvent::Pressure ? l->pressure : l->slide;
                    if (pts.empty() || e.beat - pts.back().beat > 1.0 / 64.0) pts.push_back ({ e.beat, e.value });
                    changed = true;
                }
                break;
            }
        }
        // notes still held keep growing
        if (auto* l = model.find (rl.lineId); l != nullptr && e.beat > l->nodes.back().beat) l->nodes.back().beat = e.beat;
    }
    if (changed) { publish(); canvas.repaint(); }
}

void RollPage::timerCallback()
{
    if (! isVisible()) return;
    drainRecording();
    if (engine.transportRunning.load() || engine.playheadBeat.load() >= 0 || (showGlitter && isShowing())) canvas.repaint();
    const bool playing = engine.isInternalPlaying();
    if (playBtn.getToggleState() != playing) refreshControls();
}

bool RollPage::keyPressed (const KeyPress& k)
{
    const auto c = k.getTextCharacter();
    const bool cmd = k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown();
    if (cmd && (c == 'z' || c == 'Z' || k.getKeyCode() == 'Z')) { if (k.getModifiers().isShiftDown()) redo(); else undo(); return true; }
    if (cmd && (k.getKeyCode() == 'Y')) { redo(); return true; }
    if (cmd && (k.getKeyCode() == 'C')) { copySelection (false); return true; }
    if (cmd && (k.getKeyCode() == 'X')) { copySelection (true); return true; }
    if (cmd && (k.getKeyCode() == 'V')) { paste(); return true; }
    if (cmd && (k.getKeyCode() == 'A')) { selectAll(); return true; }
    if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (k == KeyPress::spaceKey) { playBtn.onClick(); return true; }
    if (! cmd && (c == 'd' || c == 'D')) { setTool (Tool::Draw); return true; }
    if (! cmd && (c == 's' || c == 'S')) { setTool (Tool::Select); return true; }
    if (! cmd && (c == 'f' || c == 'F')) { canvas.fitToPattern(); return true; }
    return false;
}

} // namespace sol::gui
