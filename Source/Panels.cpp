// The XY morph pad and the effects rack.
#include "PluginEditor.h"

using namespace juce;

// ============================================================================ morph pad
MorphPad::MorphPad (SolIntentProcessor& p) : proc (p)
{
    onButton.setButtonText ("Morph");
    onButton.setTooltip ("On: the dot blends the four layers - A top-left, B top-right, C bottom-left, D bottom-right.\n"
                         "Right-click the pad to let Motion 1 circle the dot on its own.");
    onAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "morphOn", onButton);
    addAndMakeVisible (onButton);
}

Rectangle<float> MorphPad::padArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (30.0f).reduced (10.0f, 6.0f).withTrimmedBottom (4.0f);
}

void MorphPad::resized()
{
    onButton.setBounds (getWidth() - 96, 4, 90, 24);
}

void MorphPad::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (ui::goldDim.brighter (0.3f));
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("MORPH", 12, 6, 80, 20, Justification::centredLeft);

    const bool on = onButton.getToggleState();
    auto r = padArea();
    g.setColour (ui::bg);
    g.fillRoundedRectangle (r, 8.0f);

    const float x = proc.morphXNow.load(), y = proc.morphYNow.load();
    const float w[4] = { (1 - x) * y, x * y, (1 - x) * (1 - y), x * (1 - y) };
    const Point<float> corners[4] = { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() };
    const Colour cols[4] = { ui::teal, ui::violet, ui::gold, ui::teal.interpolatedWith (ui::violet, 0.5f) };

    // a glow at each corner, as bright as that layer's share of the blend
    for (int i = 0; i < 4; ++i)
    {
        const float amt = on ? std::sqrt (w[i]) : 0.15f;
        auto c = corners[i];
        g.setGradientFill (ColourGradient (cols[i].withAlpha (0.55f * amt), c.x, c.y, cols[i].withAlpha (0.0f), c.x + r.getWidth() * 0.7f, c.y, true));
        g.fillRoundedRectangle (r, 8.0f);
    }

    // grid
    g.setColour (ui::line.withAlpha (0.8f));
    for (int k = 1; k < 4; ++k)
    {
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) k / 4.0f), r.getY(), r.getBottom());
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) k / 4.0f), r.getX(), r.getRight());
    }
    g.setColour (ui::goldDim.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    // corner names
    g.setFont (FontOptions (10.5f, Font::bold));
    for (int i = 0; i < 4; ++i)
    {
        auto inst = proc.getDisplayInstrument (i);
        const bool layerOn = proc.params.getRawParameterValue (SolIntentProcessor::layerParam (i, "On"))->load() > 0.5f;
        String name = SolIntentProcessor::layerLetter (i) + "  " + (inst != nullptr && inst->name != "Empty" ? inst->name : String ("empty"));
        auto box = Rectangle<float> (r.getWidth() * 0.5f - 8.0f, 14.0f);
        box.setPosition (i % 2 == 0 ? r.getX() + 6.0f : r.getRight() - box.getWidth() - 6.0f,
                         i < 2 ? r.getY() + 5.0f : r.getBottom() - 19.0f);
        g.setColour ((layerOn ? ui::text : ui::textDim).withAlpha (on ? 0.4f + 0.6f * std::sqrt (w[i]) : 0.5f));
        g.drawFittedText (name, box.toNearestInt(), i % 2 == 0 ? Justification::centredLeft : Justification::centredRight, 1, 0.8f);
    }

    // the dot
    auto dot = Point<float> (r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight());
    if (on)
    {
        g.setColour (ui::teal.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (30, 30).withCentre (dot));
        g.setColour (ui::teal.withAlpha (0.35f));
        g.drawHorizontalLine ((int) dot.y, r.getX(), r.getRight());
        g.drawVerticalLine ((int) dot.x, r.getY(), r.getBottom());
        g.setColour (ui::text);
        g.fillEllipse (Rectangle<float> (12, 12).withCentre (dot));
        g.setColour (ui::teal);
        g.drawEllipse (Rectangle<float> (16, 16).withCentre (dot), 1.5f);
    }
    else
    {
        g.setColour (ui::textDim);
        g.setFont (FontOptions (12.0f));
        g.drawFittedText ("Switch Morph on to blend\nthe four layers with the dot", r.toNearestInt().reduced (20), Justification::centred, 3);
    }
}

void MorphPad::moveTo (Point<float> pos)
{
    auto r = padArea();
    const float x = jlimit (0.0f, 1.0f, (pos.x - r.getX()) / r.getWidth());
    const float y = jlimit (0.0f, 1.0f, (r.getBottom() - pos.y) / r.getHeight());
    proc.params.getParameter ("morphX")->setValueNotifyingHost (x);
    proc.params.getParameter ("morphY")->setValueNotifyingHost (y);
    repaint();
}

void MorphPad::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem (1, "Learn MIDI control for left / right");
        m.addItem (2, "Learn MIDI control for up / down");
        m.addItem (3, "Back to the middle");
        m.addSeparator();
        m.addItem (4, "Orbit with Motion 1 (the dot circles on its own)", true, proc.isOrbiting());
        m.showMenuAsync (PopupMenu::Options(), [this] (int r)
        {
            if (r == 4)
            {
                const bool on = ! proc.isOrbiting();
                proc.setOrbit (on);
                if (on) onButton.setToggleState (true, sendNotificationSync);
                proc.setStatus (on ? "The dot now circles with Motion 1 - Rate 1 sets the speed, Amount 1 the size."
                                   : "Orbit off.");
            }
            if (r == 1) proc.startLearning ("morphX");
            if (r == 2) proc.startLearning ("morphY");
            if (r == 3) { proc.params.getParameter ("morphX")->setValueNotifyingHost (0.5f); proc.params.getParameter ("morphY")->setValueNotifyingHost (0.5f); }
        });
        return;
    }
    if (! padArea().contains (e.position)) return;
    if (! onButton.getToggleState()) onButton.setToggleState (true, sendNotificationSync);   // touching the pad switches it on
    dragging = true;
    proc.params.getParameter ("morphX")->beginChangeGesture();
    proc.params.getParameter ("morphY")->beginChangeGesture();
    moveTo (e.position);
}

void MorphPad::mouseDrag (const MouseEvent& e) { if (dragging) moveTo (e.position); }

void MorphPad::mouseUp (const MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    proc.params.getParameter ("morphX")->endChangeGesture();
    proc.params.getParameter ("morphY")->endChangeGesture();
}

// ============================================================================ effects rack
FxView::FxView (SolIntentProcessor& p) : proc (p)
{
    struct Def { const char* id; const char* label; bool bipolar; };
    auto module = [this] (const String& title, const String& blurb, std::initializer_list<Def> defs)
    {
        Module m { title, blurb, {}, {} };
        for (auto& d : defs)
        {
            auto* k = knobs.add (new ParamKnob (proc, d.id, d.label, d.bipolar));
            addAndMakeVisible (k);
            m.knobs.push_back (k);
        }
        modules.push_back (m);
    };
    module ("EQ",     "Shape the lows, mids and highs. Tone is a quick dark-to-bright sweep.",
            { { "eqLow", "Low", true }, { "eqMid", "Mid", true }, { "eqHigh", "High", true }, { "tone", "Tone", true } });
    module ("CHORUS", "A slow shimmer that widens and thickens.",
            { { "chorusMix", "Amount", false }, { "chorusDepth", "Depth", false }, { "chorusRate", "Rate", false } });
    module ("ECHO",   "Repeats that bounce left and right and soften as they fade.",
            { { "delayMix", "Amount", false }, { "delayTime", "Time", false }, { "delayFeedback", "Repeats", false } });
    module ("SPACE",  "The room around the sound, and how wide it spreads.",
            { { "reverbMix", "Space", false }, { "reverbSize", "Room", false }, { "width", "Width", false } });
}

void FxView::paint (Graphics& g)
{
    for (auto& m : modules)
    {
        auto r = m.area.toFloat();
        g.setColour (ui::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (ui::goldDim.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        auto head = m.area.reduced (14, 0).removeFromLeft (150);
        g.setColour (ui::gold);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (m.title, head.withHeight (40).translated (0, 8), Justification::topLeft);
        g.setColour (ui::textDim);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText (m.blurb, head.withTrimmedTop (30).withTrimmedBottom (10), Justification::topLeft, 4);
    }
}

void FxView::resized()
{
    auto r = getLocalBounds();
    const int h = (r.getHeight() - 3 * 8) / (int) modules.size();
    for (auto& m : modules)
    {
        m.area = r.removeFromTop (h);
        r.removeFromTop (8);
        auto k = m.area.reduced (10, 8).withTrimmedLeft (160);
        const int w = jmin (96, k.getWidth() / 4);
        for (auto* knob : m.knobs)
            knob->setBounds (k.removeFromLeft (w).withSizeKeepingCentre (w, jmin (k.getHeight(), 92)));
    }
}

void FxView::refresh()
{
    for (auto* k : knobs) k->refreshLearnState();
}
