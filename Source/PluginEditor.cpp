#include "PluginEditor.h"
#include <cmath>

using namespace juce;

juce::String ui::u8 (const char* s) { return String (CharPointer_UTF8 (s)); }

// ============================================================================ look
ui::Look::Look()
{
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (TextButton::buttonColourId, panelHi);
    setColour (TextButton::buttonOnColourId, panelHi.brighter (0.15f));
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, gold);
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, violet.withAlpha (0.4f));
    setColour (Label::textColourId, text);
    setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    setColour (ListBox::outlineColourId, Colours::transparentBlack);
    setColour (ScrollBar::thumbColourId, line.brighter (0.3f));
    setColour (ToggleButton::textColourId, text);
    setColour (ComboBox::backgroundColourId, panelHi);
    setColour (ComboBox::textColourId, text);
    setColour (ComboBox::outlineColourId, line.brighter (0.15f));
    setColour (ComboBox::arrowColourId, gold);
    setColour (PopupMenu::backgroundColourId, panel);
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::highlightedBackgroundColourId, panelHi.brighter (0.15f));
    setColour (PopupMenu::highlightedTextColourId, gold);
    setColour (PopupMenu::headerTextColourId, textDim);
    setColour (AlertWindow::backgroundColourId, panel);
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, goldDim);
    setColour (TextEditor::backgroundColourId, bg);
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::outlineColourId, line.brighter (0.3f));
    setColour (TextEditor::focusedOutlineColourId, gold);
    setColour (TooltipWindow::backgroundColourId, panelHi);
    setColour (TooltipWindow::textColourId, text);
    setColour (TooltipWindow::outlineColourId, goldDim);
    setColour (BubbleComponent::backgroundColourId, panelHi);
    setColour (BubbleComponent::outlineColourId, goldDim);
}

Label* ui::Look::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (FontOptions (11.0f));
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::textColourId, s.getSliderStyle() == Slider::LinearBar ? text : textDim.brighter (0.3f));
    return l;
}

void ui::Look::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    auto b = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    auto size = jmin (b.getWidth(), b.getHeight());
    auto c = b.getCentre();
    auto r = size * 0.5f;
    const float stroke = size > 40 ? 3.0f : 2.5f;

    // body: a small dark bowl seen from above
    g.setGradientFill (ColourGradient (panelHi.brighter (0.15f), c.x, c.y - r, bg, c.x, c.y + r, false));
    g.fillEllipse (Rectangle<float> (r * 1.4f, r * 1.4f).withCentre (c));
    g.setColour (goldDim);
    g.drawEllipse (Rectangle<float> (r * 1.4f, r * 1.4f).withCentre (c), 1.0f);

    Path track;
    track.addCentredArc (c.x, c.y, r - stroke, r - stroke, 0.0f, start, end, true);
    g.setColour (line);
    g.strokePath (track, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    const bool bipolar = s.getProperties()["bipolar"];
    const bool learning = s.getProperties()["learning"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    const float to = start + pos * (end - start);
    Path val;
    val.addCentredArc (c.x, c.y, r - stroke, r - stroke, 0.0f, jmin (from, to), jmax (from, to), true);
    auto arcCol = learning ? violet : s.isEnabled() ? teal : teal.withAlpha (0.4f);
    g.setColour (arcCol.withAlpha (0.18f));
    g.strokePath (val, PathStrokeType (stroke * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    auto tip = c.getPointOnCircumference (r * 0.52f, to);
    g.setColour (text);
    g.fillEllipse (Rectangle<float> (4.5f, 4.5f).withCentre (tip));
}

void ui::Look::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearBar) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    auto r = Rectangle<int> (x, y, w, h).toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (gold.withAlpha (s.isEnabled() ? 0.22f : 0.08f));
    g.fillRoundedRectangle (r.withRight (pos), 4.0f);
    g.setColour (line.brighter (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
}

void ui::Look::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (down ? panelHi.brighter (0.2f) : over ? panelHi.brighter (0.1f) : on ? panelHi.brighter (0.06f) : panelHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? gold : over ? goldDim : line.brighter (0.15f));
    g.drawRoundedRectangle (r, 6.0f, on ? 1.4f : 1.0f);
}

void ui::Look::drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const float pw = jmin (34.0f, r.getHeight() * 1.9f), ph = pw * 0.55f;
    auto pill = r.removeFromLeft (pw).withSizeKeepingCentre (pw, ph);
    const bool on = b.getToggleState();
    g.setColour (on ? gold.withAlpha (0.25f) : panelHi);
    g.fillRoundedRectangle (pill, ph * 0.5f);
    g.setColour (on ? gold : (over ? goldDim : line.brighter (0.3f)));
    g.drawRoundedRectangle (pill, ph * 0.5f, 1.0f);
    const float kd = ph - 6.0f;
    g.fillEllipse (Rectangle<float> (kd, kd).withCentre ({ on ? pill.getRight() - ph * 0.5f : pill.getX() + ph * 0.5f, pill.getCentreY() }));
    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (text);
        g.setFont (FontOptions (13.0f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (8.0f), Justification::centredLeft);
    }
}

void ui::Look::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (panelHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (false) ? goldDim : line.brighter (0.15f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    Path arrow;
    auto ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (gold);
    g.fillPath (arrow);
}

// ============================================================================ knob
ParamKnob::ParamKnob (SolIntentProcessor& p, const String& id, const String& text, bool bipolar, bool isSmall)
    : proc (p), paramID (id), baseLabel (text), small (isSmall)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (small ? Slider::NoTextBox : Slider::TextBoxBelow, false, 74, 15);
    slider.setPopupDisplayEnabled (small, true, nullptr);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, ui::textDim.brighter (0.4f));
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, slider);
    slider.setDoubleClickReturnValue (true, proc.params.getParameterRange (id).convertFrom0to1 (proc.params.getParameter (id)->getDefaultValue()));
    slider.setTooltip ("Drag to change. Double-click to reset. Right-click to link it to a knob or fader on your MIDI controller.");
    slider.onRightClick = [this]
    {
        PopupMenu m;
        int cc = proc.getLearnedCC (paramID);
        m.addSectionHeader (baseLabel);
        m.addItem (1, "Learn MIDI control (then move a knob or fader)");
        if (cc >= 0) m.addItem (2, "Forget MIDI control (CC " + String (cc) + ")");
        m.addItem (3, "Reset to default");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&slider), [this] (int r)
        {
            if (r == 1) proc.startLearning (paramID);
            if (r == 2) proc.forgetLearning (paramID);
            if (r == 3) if (auto* prm = proc.params.getParameter (paramID)) prm->setValueNotifyingHost (prm->getDefaultValue());
            refreshLearnState();
        });
    };
    addAndMakeVisible (slider);

    label.setText (text.toUpperCase(), dontSendNotification);
    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (small ? 9.5f : 10.5f));
    label.setColour (Label::textColourId, ui::goldDim.brighter (0.4f));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    if (small)
    {
        label.setBounds (r.removeFromBottom (12));
        slider.setBounds (r);
    }
    else
    {
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }
}

void ParamKnob::refreshLearnState()
{
    const bool learning = proc.getLearningParam() == paramID;
    if ((bool) slider.getProperties()["learning"] != learning)
    {
        slider.getProperties().set ("learning", learning);
        slider.repaint();
    }
    int cc = proc.getLearnedCC (paramID);
    auto t = learning ? String ("MOVE A CONTROL") : baseLabel.toUpperCase() + (cc >= 0 && ! small ? "  CC" + String (cc) : String());
    label.setText (t, dontSendNotification);
    label.setColour (Label::textColourId, learning ? ui::violet : cc >= 0 ? ui::teal : ui::goldDim.brighter (0.4f));
}

// ============================================================================ ripple display
void ResonanceView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 10.0f);

    auto c = b.getCentre();
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.42f;

    // faint flower-of-life behind everything
    g.setColour (ui::goldDim.withAlpha (0.45f));
    const float fr = R * 0.33f;
    g.drawEllipse (Rectangle<float> (fr * 2, fr * 2).withCentre (c), 0.8f);
    for (int i = 0; i < 6; ++i)
    {
        auto pt = c.getPointOnCircumference (fr, MathConstants<float>::twoPi * (float) i / 6.0f);
        g.drawEllipse (Rectangle<float> (fr * 2, fr * 2).withCentre (pt), 0.8f);
    }

    // outer ring with the 12 notes around it, like a clock
    g.setColour (ui::goldDim.withAlpha (0.6f));
    g.drawEllipse (Rectangle<float> (R * 2, R * 2).withCentre (c), 1.0f);
    g.drawEllipse (Rectangle<float> (R * 2 - 10, R * 2 - 10).withCentre (c), 0.6f);
    g.setFont (FontOptions (10.0f));
    static const char* pcs[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
    {
        auto pt = c.getPointOnCircumference (R + 12.0f, MathConstants<float>::twoPi * (float) i / 12.0f);
        g.setColour (ui::textDim);
        g.drawText (pcs[i], Rectangle<float> (24, 12).withCentre (pt), Justification::centred);
    }

    // glow in the middle follows the output level
    const float lvl = jlimit (0.0f, 1.0f, proc.outputLevel.load() * 2.0f);
    g.setGradientFill (ColourGradient (ui::teal.withAlpha (0.08f + 0.35f * lvl), c.x, c.y,
                                       ui::teal.withAlpha (0.0f), c.x + R * 0.8f, c.y, true));
    g.fillEllipse (Rectangle<float> (R * 1.6f, R * 1.6f).withCentre (c));

    // one rippling ring per note still sounding: low notes inside, high notes outside
    for (int n = 0; n < 128; ++n)
    {
        float v = proc.noteGlow[(size_t) n].load();
        if (v < 0.0008f) continue;
        v = jlimit (0.0f, 1.0f, std::sqrt (v * 3.0f));
        const float rad = R * (0.18f + 0.78f * jlimit (0.0f, 1.0f, (float) (n - 24) / 72.0f));
        const int lobes = 3 + n % 12;
        const float amp = 2.0f + 7.0f * v;
        Path ring;
        for (int i = 0; i <= 180; ++i)
        {
            float a = MathConstants<float>::twoPi * (float) i / 180.0f;
            float rr = rad + amp * std::sin (a * (float) lobes + phase * (1.0f + (float) (n % 5) * 0.3f));
            auto pt = c.getPointOnCircumference (rr, a);
            if (i == 0) ring.startNewSubPath (pt); else ring.lineTo (pt);
        }
        ring.closeSubPath();
        const float hue = (float) (n % 12) / 11.0f;   // cyan -> violet -> gold around the octave
        auto col = hue < 0.5f ? ui::teal.interpolatedWith (ui::violet, hue * 2.0f)
                              : ui::violet.interpolatedWith (ui::gold, (hue - 0.5f) * 2.0f);
        g.setColour (col.withAlpha (0.10f + 0.15f * v));
        g.strokePath (ring, PathStrokeType (5.0f + 4.0f * v));
        g.setColour (col.withAlpha (0.35f + 0.6f * v));
        g.strokePath (ring, PathStrokeType (1.0f + 1.5f * v));

        auto dot = c.getPointOnCircumference (R - 5.0f, MathConstants<float>::twoPi * (float) (n % 12) / 12.0f);
        g.setColour (col.withAlpha (0.4f + 0.6f * v));
        g.fillEllipse (Rectangle<float> (5.0f + 5.0f * v, 5.0f + 5.0f * v).withCentre (dot));
    }

    g.setColour (ui::text);
    g.setFont (FontOptions (19.0f, Font::bold));
    g.drawFittedText (title, Rectangle<float> (R * 1.6f, 24).withCentre ({ c.x, c.y - 8 }).toNearestInt(), Justification::centred, 1);
    g.setColour (ui::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText (subtitle, Rectangle<float> (b.getWidth(), 18).withCentre ({ c.x, c.y + 14 }), Justification::centred);
}

// ============================================================================ pads
Rectangle<float> PadGrid::padBounds (int pad) const
{
    auto b = getLocalBounds().toFloat();
    const float gap = 6.0f;
    const float w = (b.getWidth() - gap * 3) / 4.0f, h = (b.getHeight() - gap * 3) / 4.0f;
    const int col = pad % 4, rowFromBottom = pad / 4;
    return { b.getX() + (float) col * (w + gap), b.getBottom() - h - (float) rowFromBottom * (h + gap), w, h };
}

int PadGrid::padAt (Point<float> pt) const
{
    for (int i = 0; i < 16; ++i)
        if (padBounds (i).contains (pt)) return i;
    return -1;
}

void PadGrid::paint (Graphics& g)
{
    for (int i = 0; i < 16; ++i)
    {
        const int note = proc.padBaseNote + i;
        auto r = padBounds (i);
        const bool playable = art != nullptr && note >= art->lowestKey && note <= art->highestKey;
        const bool recorded = art != nullptr && art->hasRootAt (note);
        const float glow = jlimit (0.0f, 1.0f, std::sqrt (proc.noteGlow[(size_t) jlimit (0, 127, note)].load() * 3.0f));
        const bool down = proc.keyboardState.isNoteOn (1, note);

        g.setColour (playable ? ui::panelHi : ui::panel);
        g.fillRoundedRectangle (r, 6.0f);
        if (glow > 0.01f || down)
        {
            g.setColour (ui::teal.withAlpha (jmax (glow * 0.35f, down ? 0.30f : 0.0f)));
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (down || glow > 0.05f ? ui::teal : playable ? ui::goldDim.withAlpha (0.7f) : ui::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, down ? 1.5f : 1.0f);

        g.setColour (playable ? ui::text : ui::textDim.withAlpha (0.5f));
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText (sol::noteName (note), r.reduced (8, 6), Justification::bottomLeft);
        g.setColour (ui::textDim.withAlpha (0.7f));
        g.setFont (FontOptions (10.0f));
        g.drawText (String (i + 1), r.reduced (8, 6), Justification::topLeft);
        if (recorded)   // a real recording lives on this pad
        {
            g.setColour (ui::teal);
            g.fillEllipse (Rectangle<float> (6, 6).withCentre ({ r.getRight() - 10, r.getY() + 10 }));
        }
    }
}

void PadGrid::mouseDown (const MouseEvent& e)
{
    int pad = padAt (e.position);
    if (pad < 0) return;
    auto r = padBounds (pad);
    // higher on the pad = harder strike
    float vel = jmap (jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight()), 0.35f, 1.0f);
    heldNote = proc.padBaseNote + pad;
    proc.keyboardState.noteOn (1, heldNote, vel);
}

void PadGrid::mouseDrag (const MouseEvent& e)
{
    int pad = padAt (e.position);
    if (pad >= 0 && proc.padBaseNote + pad != heldNote)
    {
        release();
        mouseDown (e);
    }
}

void PadGrid::mouseUp (const MouseEvent&) { release(); }

void PadGrid::release()
{
    if (heldNote >= 0) proc.keyboardState.noteOff (1, heldNote, 0.0f);
    heldNote = -1;
}

// ============================================================================ keyboard
void TintedKeyboard::drawWhiteNote (int n, Graphics& g, Rectangle<float> area, bool down, bool over, Colour lineCol, Colour)
{
    auto c = isKeyswitch (n) ? ui::violet.withSaturation (0.45f).brighter (0.6f)
                             : inRange (n) ? Colour (0xffeee3cf) : Colour (0xff4d566b);
    if (over) c = c.brighter (0.1f);
    if (down) c = ui::teal;
    g.setColour (c);
    g.fillRect (area);
    g.setColour (lineCol);
    g.fillRect (area.withWidth (1.0f));
    if (art != nullptr && art->hasRootAt (n))
    {
        g.setColour (Colour (0xff0f8fa0));
        g.fillEllipse (Rectangle<float> (5, 5).withCentre ({ area.getCentreX(), area.getBottom() - 8 }));
    }
    if (n % 12 == 0)
    {
        g.setColour (Colours::black.withAlpha (0.5f));
        g.setFont (FontOptions (9.0f));
        g.drawText (sol::noteName (n), area.withTrimmedBottom (12), Justification::centredBottom);
    }
}

void TintedKeyboard::drawBlackNote (int n, Graphics& g, Rectangle<float> area, bool down, bool over, Colour)
{
    auto c = isKeyswitch (n) ? ui::violet.darker (0.4f) : inRange (n) ? Colour (0xff0b1220) : Colour (0xff27304a);
    if (over) c = c.brighter (0.2f);
    if (down) c = ui::teal.darker (0.3f);
    g.setColour (c);
    g.fillRect (area);
    if (art != nullptr && art->hasRootAt (n))
    {
        g.setColour (ui::teal);
        g.fillEllipse (Rectangle<float> (4, 4).withCentre ({ area.getCentreX(), area.getBottom() - 6 }));
    }
}

// ============================================================================ layer card
LayerCard::LayerCard (SolIntentProcessor& p, int i)
    : proc (p), index (i),
      level (p, SolIntentProcessor::layerParam (i, "Vol"), "Level", false, true),
      tune  (p, SolIntentProcessor::layerParam (i, "Tune"), "Tune", true, true),
      pan   (p, SolIntentProcessor::layerParam (i, "Pan"), "Pan", true, true)
{
    onButton.setTooltip ("Turn this layer on or off");
    onAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.params, SolIntentProcessor::layerParam (i, "On"), onButton);
    addAndMakeVisible (onButton);
    addAndMakeVisible (level);
    addAndMakeVisible (tune);
    addAndMakeVisible (pan);
}

void LayerCard::refresh()
{
    auto inst = proc.getDisplayInstrument (index);
    auto newName = inst != nullptr ? inst->name : (index > 0 ? String ("Empty") : String ("..."));
    String newArt;
    if (inst != nullptr)
        if (auto* a = inst->get (proc.getArticulation (index))) newArt = a->name;
    if (newName != name || newArt != articulation) { name = newName; articulation = newArt; repaint(); }
    level.refreshLearnState(); tune.refreshLearnState(); pan.refreshLearnState();
}

void LayerCard::paint (Graphics& g)
{
    const bool active = proc.activeLayer == index;
    const bool on = onButton.getToggleState();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (active ? ui::panelHi : ui::panel.brighter (0.02f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (active ? ui::gold : ui::line.brighter (0.1f));
    g.drawRoundedRectangle (r, 8.0f, active ? 1.4f : 1.0f);

    auto badge = Rectangle<float> (8, 8, 20, 20);
    g.setColour (on ? ui::gold : ui::textDim);
    g.drawEllipse (badge, 1.2f);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (SolIntentProcessor::layerLetter (index), badge, Justification::centred);

    g.setColour (on ? ui::text : ui::textDim);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawFittedText (name, 34, 7, getWidth() - 84, 18, Justification::centredLeft, 1);
    g.setColour (ui::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (articulation, 34, 30, getWidth() - 34 - 150, 14, Justification::centredLeft, 1);
    if (! on && proc.getDisplayInstrument (index) != nullptr)
    {
        g.setFont (FontOptions (10.0f));
        g.drawText ("off", 34, 46, 60, 12, Justification::centredLeft);
    }
}

void LayerCard::resized()
{
    onButton.setBounds (getWidth() - 44, 6, 36, 20);
    auto knobsRow = getLocalBounds().withTrimmedTop (27).withTrimmedBottom (3).removeFromRight (148).reduced (4, 0);
    const int w = knobsRow.getWidth() / 3;
    level.setBounds (knobsRow.removeFromLeft (w));
    tune.setBounds (knobsRow.removeFromLeft (w));
    pan.setBounds (knobsRow);
}

void LayerCard::mouseDown (const MouseEvent&)
{
    if (onActivate) onActivate();
}
