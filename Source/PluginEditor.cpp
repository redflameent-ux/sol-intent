#include <BinaryData.h>
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
    const bool linked = s.getProperties()["linked"];
    auto arcCol = learning ? violet : linked ? violet.brighter (0.2f) : s.isEnabled() ? teal : teal.withAlpha (0.4f);
    g.setColour (arcCol.withAlpha (0.18f));
    g.strokePath (val, PathStrokeType (stroke * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    // modulation range: a thin outer arc showing how far the modulation can push the knob
    const float modDepth = s.getProperties()["modDepth"];
    if (std::abs (modDepth) > 0.0001f)
    {
        const float a1 = to, a2 = jlimit (start, end, to + modDepth * (end - start));
        Path modArc;
        modArc.addCentredArc (c.x, c.y, r - 0.5f, r - 0.5f, 0.0f, jmin (a1, a2), jmax (a1, a2), true);
        g.setColour (gold.withAlpha (0.9f));
        g.strokePath (modArc, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    }

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
    : proc (p), baseLabel (text), small (isSmall)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (small ? Slider::NoTextBox : Slider::TextBoxBelow, false, 74, 15);
    slider.setPopupDisplayEnabled (small, true, nullptr);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    slider.setColour (Slider::textBoxTextColourId, ui::textDim.brighter (0.4f));
    slider.onRightClick = [this] { showMenu(); };
    addAndMakeVisible (slider);

    label.setJustificationType (Justification::centred);
    label.setFont (FontOptions (small ? 9.5f : 10.5f));
    label.setColour (Label::textColourId, ui::goldDim.brighter (0.4f));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);
    setParam (id, text);
}

void ParamKnob::setParam (const String& id, const String& text)
{
    if (id == paramID && text == baseLabel && att != nullptr) return;
    att.reset();
    paramID = id;
    baseLabel = text;
    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, slider);
    slider.setDoubleClickReturnValue (true, proc.params.getParameterRange (id).convertFrom0to1 (proc.params.getParameter (id)->getDefaultValue()));
    const bool isMacro = id.startsWith ("macro");
    slider.setTooltip (isMacro ? "A macro moves every knob linked to it. Right-click any other knob to link it here; right-click this one to rename it."
                               : "Drag to change. Double-click to reset. Right-click to link it to your MIDI controller or a macro.");
    label.setText (text.toUpperCase(), dontSendNotification);
    shownLabel.clear();
    refreshLearnState();
}

void ParamKnob::showMenu()
{
    const bool isMacro = paramID.startsWith ("macro");
    const int macroIndex = isMacro ? paramID.getTrailingIntValue() - 1 : -1;
    PopupMenu m;
    int cc = proc.getLearnedCC (paramID);
    m.addSectionHeader (isMacro ? proc.getMacroName (macroIndex) : baseLabel);
    m.addItem (1, "Learn MIDI control (then move a knob or fader)");
    if (cc >= 0) m.addItem (2, "Forget MIDI control (CC " + String (cc) + ")");

    if (isMacro)
    {
        m.addSeparator();
        m.addItem (20, "Rename...");
        auto links = proc.getMacroLinks (macroIndex);
        PopupMenu linked;
        for (auto& l : links)
            if (auto* prm = proc.params.getParameter (l.paramID))
                linked.addItem (-1, prm->getName (40), false);
        if (links.empty()) linked.addItem (-1, "Nothing yet - right-click a knob and pick Link to macro", false);
        m.addSubMenu ("Linked knobs (" + String ((int) links.size()) + ")", linked);
        m.addItem (21, "Unlink everything", ! links.empty());
    }
    if (SolIntentProcessor::canModulate (proc.params.getParameter (paramID)))
    {
        // modulation: any source can move this knob, by as much as you like, either way
        m.addSeparator();
        auto mods = proc.getModulations (paramID);
        PopupMenu modMenu;
        static const float amounts[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f };
        for (int src = 0; src < SolIntentProcessor::numModSources; ++src)
        {
            float current = 0.0f;
            for (auto& l : mods) if (l.source == src && ! l.quarter) current = l.depth;
            PopupMenu amountMenu;
            for (int a = 0; a < 8; ++a)
                amountMenu.addItem (100 + src * 10 + a, (amounts[a] > 0 ? "+" : "") + String (roundToInt (amounts[a] * 100)) + "% of the knob's range",
                                    true, std::abs (current - amounts[a]) < 0.001f);
            amountMenu.addSeparator();
            amountMenu.addItem (100 + src * 10 + 9, "Off", current != 0.0f);
            modMenu.addSubMenu (SolIntentProcessor::modSourceName (src)
                                    + (current != 0.0f ? "   (" + String (current > 0 ? "+" : "") + String (roundToInt (current * 100)) + "%)" : String()),
                                amountMenu, true, nullptr, current != 0.0f);
        }
        if (! mods.empty())
        {
            modMenu.addSeparator();
            modMenu.addItem (190, "Remove all modulation from this knob");
        }
        m.addSubMenu ("Modulate with", modMenu, true, nullptr, ! mods.empty());
    }

    if (! isMacro)
    {
        m.addSeparator();
        const int current = proc.getMacroFor (paramID);
        PopupMenu link;
        for (int k = 0; k < SolIntentProcessor::numMacros; ++k)
            link.addItem (10 + k, proc.getMacroName (k), true, current == k);
        m.addSubMenu ("Link to macro", link);
        if (current >= 0)
        {
            m.addItem (30, proc.getMacroName (current) + " ends here (turn the macro up to reach this setting)");
            m.addItem (31, "Unlink from " + proc.getMacroName (current));
        }
    }
    m.addSeparator();
    m.addItem (3, "Reset to default");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&slider), [this, macroIndex] (int r)
    {
        if (r == 1) proc.startLearning (paramID);
        if (r == 2) proc.forgetLearning (paramID);
        if (r == 3) if (auto* prm = proc.params.getParameter (paramID)) prm->setValueNotifyingHost (prm->getDefaultValue());
        if (r >= 10 && r < 10 + SolIntentProcessor::numMacros)
        {
            proc.linkToMacro (r - 10, paramID);
            proc.setStatus ("Linked " + baseLabel + " to " + proc.getMacroName (r - 10)
                            + ". Now set this knob where the macro should take it, right-click it and choose \"ends here\".");
        }
        if (r == 30) { int k = proc.getMacroFor (paramID); proc.setMacroEnd (k, paramID); proc.setStatus ("Set where " + proc.getMacroName (k) + " takes " + baseLabel + "."); }
        if (r == 31) proc.unlinkFromMacro (paramID);
        if (r >= 100 && r < 100 + SolIntentProcessor::numModSources * 10)
        {
            static const float amounts[] = { 1.0f, 0.5f, 0.25f, 0.1f, -0.1f, -0.25f, -0.5f, -1.0f, 0.0f, 0.0f };
            const int src = (r - 100) / 10, a = (r - 100) % 10;
            proc.setModulation (src, paramID, amounts[a]);
            if (a < 8)
                proc.setStatus (SolIntentProcessor::modSourceName (src) + " now moves " + baseLabel + " by "
                                + String (roundToInt (amounts[a] * 100)) + "%."
                                + (src <= SolIntentProcessor::motion2 ? " Set its speed and amount in MOTION." : ""));
        }
        if (r == 190) proc.clearModulations (paramID);
        if (r == 21) proc.clearMacro (macroIndex);
        if (r == 20)
        {
            auto* top = findParentComponentOfClass<AudioProcessorEditor>();
            auto* w = new AlertWindow ("Rename macro", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", proc.getMacroName (macroIndex));
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            if (top != nullptr) { top->addAndMakeVisible (w); w->centreAroundComponent (top, w->getWidth(), w->getHeight()); }
            w->enterModalState (true, ModalCallbackFunction::create ([this, w, macroIndex] (int res)
            {
                if (res == 1) proc.setMacroName (macroIndex, w->getTextEditorContents ("name"));
            }), true);
        }
        refreshLearnState();
    });
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
    const bool isMacro = paramID.startsWith ("macro");
    const int macro = isMacro ? -1 : proc.getMacroFor (paramID);
    float modDepth = 0.0f;
    String modTag;
    for (auto& l : proc.getModulations (paramID))
        if (! l.quarter || l.paramID != "morphX")
        {
            modDepth += l.depth;
            if (modTag.isEmpty()) modTag = l.source == SolIntentProcessor::motion1 ? "~1" : l.source == SolIntentProcessor::motion2 ? "~2"
                                         : l.source == SolIntentProcessor::modWheelSource ? "~MW" : "~AT";
        }
    if ((bool) slider.getProperties()["learning"] != learning || (bool) slider.getProperties()["linked"] != (macro >= 0)
        || std::abs ((float) slider.getProperties()["modDepth"] - modDepth) > 0.0001f)
    {
        slider.getProperties().set ("learning", learning);
        slider.getProperties().set ("linked", macro >= 0);
        slider.getProperties().set ("modDepth", modDepth);
        slider.repaint();
    }
    int cc = proc.getLearnedCC (paramID);
    String t;
    if (learning)          t = "MOVE A CONTROL";
    else if (isMacro)      t = proc.getMacroName (paramID.getTrailingIntValue() - 1).toUpperCase();
    else                   t = baseLabel.toUpperCase();
    if (! learning && ! small)
    {
        if (macro >= 0) t << "  M" << (macro + 1);
        else if (cc >= 0) t << "  CC" << cc;
        if (modTag.isNotEmpty()) t << "  " << modTag;
    }
    if (t != shownLabel)
    {
        shownLabel = t;
        label.setText (t, dontSendNotification);
    }
    label.setColour (Label::textColourId, learning ? ui::violet : macro >= 0 ? ui::violet.brighter (0.3f)
                                                   : cc >= 0 ? ui::teal : isMacro ? ui::gold : ui::goldDim.brighter (0.4f));
}

// ============================================================================ ripple display
const Image& ui::logo()
{
    static const Image img = ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    return img;
}

static Colour fifthsColour (int n)
{
    const float hue = (float) (((n % 12) * 7) % 12) / 11.0f;   // emerald -> gold -> indigo around the circle of fifths
    return hue < 0.64f ? ui::teal.interpolatedWith (ui::gold, hue / 0.64f)
                       : ui::gold.interpolatedWith (ui::violet, (hue - 0.64f) / 0.36f);
}

ResonanceView::ResonanceView (SolIntentProcessor& p) : proc (p)
{
    waves.reserve (motion.size() + 64);
    fading.reserve (64);
    vblank = std::make_unique<VBlankAttachment> (this, [this]
    {
        lastVBlank = Time::getMillisecondCounterHiRes();
        tick();
    });
}

void ResonanceView::tickIfStale()
{
    if (Time::getMillisecondCounterHiRes() - lastVBlank > 150.0)
        tick();
}

// How a pitch moves the star. Everything here comes from the pitch alone, so a note always moves it the
// same way. Real frequencies (65 Hz - 4 kHz) are far too fast to see, so they're mapped onto slow visual
// rates instead: a low C2 drifts about once every 8 seconds, middle C about 4 times a minute faster, C7 ~2x a second.
static float visualRate (float midi)   { return jlimit (0.05f, 2.6f, 0.22f * std::exp2 ((midi - 48.0f) / 15.0f)); }

void ResonanceView::tick()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTick > 0.0 ? (float) jlimit (0.0, 0.1, now - lastTick) : 1.0f / 30.0f;
    lastTick = now;
    phase += 0.035f * dt * 30.0f;   // same pace at any refresh rate

    // ---- follow every voice: real pitch, strength and its own wave phase ----
    waves.clear();
    const float attack = 1.0f - std::exp (-dt / 0.035f), release = 1.0f - std::exp (-dt / 0.12f);
    for (size_t i = 0; i < motion.size(); ++i)
    {
        auto& view = proc.voiceViews[i];
        auto& m = motion[i];
        const float hz = view.hz.load (std::memory_order_relaxed);
        const float lvl = view.level.load (std::memory_order_relaxed);
        const unsigned int serial = view.serial.load (std::memory_order_relaxed);
        float target = 0.0f;
        if (hz > 1.0f && lvl > 1.0e-6f)
        {
            if (serial != m.serial || ! m.alive)
            {
                if (m.alive && m.intensity > 0.01f && fading.size() < 64) fading.push_back (m);   // let the old note fade out
                m = { serial, 0.0f, 0.0f, 0.0f, lvl, 0.0f, true };   // a new note: its wave starts from zero
            }
            m.midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);   // the true sounding pitch (432 Hz bowls sit a little below the key)
            m.peak = jmax (m.peak, lvl);
            // velocity sets how hard it moves; the real output level (relative to this note's own peak)
            // carries it through the attack, the release and the recording's tail
            target = std::sqrt (view.velocity.load (std::memory_order_relaxed)) * std::pow (lvl / m.peak, 0.6f);
        }
        if (! m.alive) continue;
        m.intensity += (target - m.intensity) * (target > m.intensity ? attack : release);
        m.phase += MathConstants<float>::twoPi * visualRate (m.midi) * dt;
        m.age += dt;
        if (target <= 0.0f && m.intensity < 0.002f) { m.alive = false; m.intensity = 0.0f; continue; }
        waves.push_back ({ m.midi, m.phase, m.age, m.intensity });
    }
    for (auto& m : fading)
    {
        m.intensity -= m.intensity * release;
        m.phase += MathConstants<float>::twoPi * visualRate (m.midi) * dt;
        m.age += dt;
        waves.push_back ({ m.midi, m.phase, m.age, m.intensity });
    }
    fading.erase (std::remove_if (fading.begin(), fading.end(), [] (const Motion& m) { return m.intensity < 0.002f; }), fading.end());

    if (waves.size() > 24)   // a big chord: the 24 strongest are plenty (and keeps drawing quick)
    {
        std::partial_sort (waves.begin(), waves.begin() + 24, waves.end(), [] (const Wave& a, const Wave& b) { return a.strength > b.strength; });
        waves.resize (24);
    }

    // every new note draws the next line of the star (counted, so even the shortest hit is caught)
    for (int n = 0; n < 128; ++n)
    {
        const unsigned int hits = proc.noteHits[(size_t) n].load (std::memory_order_relaxed);
        if (hits != seenHits[(size_t) n])
        {
            if (starMode)
                for (unsigned int k = 0; k < juce::jmin (3u, hits - seenHits[(size_t) n]); ++k) addStarLine (n);
            seenHits[(size_t) n] = hits;
        }
    }

    float deepest = 0.0f;
    Colour deepestCol = breathCol;
    for (int n = 0; n < 128; ++n)
    {
        const bool on = proc.keyboardState.isNoteOnForChannels (0xffff, n);
        if (on)
        {
            // breathing in: light drifts from the note's place on the ring towards the centre
            held[(size_t) n] += dt;
            const float ch = charge (held[(size_t) n]);
            if (ch > deepest) { deepest = ch; deepestCol = fifthsColour (n); }
            const float spawnRate = 8.0f + 26.0f * ch;
            for (float k = rng.nextFloat(); k < spawnRate * dt; k += 1.0f)
                if (specks.size() < 700)
                    specks.push_back ({ noteAngle (n) + (rng.nextFloat() - 0.5f) * 0.5f, 1.05f + rng.nextFloat() * 0.1f,
                                        0.18f + 0.25f * rng.nextFloat(), 0.0f, 6.0f, 1.0f + 1.8f * rng.nextFloat(),
                                        rng.nextFloat() < 0.4f ? Colours::white : fifthsColour (n), true });
        }
        else if (held[(size_t) n] > 0.0f)
        {
            // breathing out: the longer it was held, the bigger, brighter and longer the release
            const float strength = jmax (0.15f, charge (held[(size_t) n]));
            exhales.push_back ({ strength, 0.0f, 0.8f + 3.2f * strength, fifthsColour (n) });
            const int burst = (int) (20 + 110 * strength);
            for (int k = 0; k < burst && specks.size() < 900; ++k)
                specks.push_back ({ rng.nextFloat() * MathConstants<float>::twoPi, 0.04f + 0.08f * rng.nextFloat(),
                                    (0.25f + 0.75f * rng.nextFloat()) * (0.5f + strength), 0.0f, 0.9f + 3.0f * strength * rng.nextFloat(),
                                    1.0f + 2.5f * rng.nextFloat() * strength,
                                    rng.nextFloat() < 0.5f ? Colours::white : fifthsColour (n), false });
            held[(size_t) n] = 0.0f;
        }
    }

    // the gathered light swells slowly while held and is let go quickly on release
    if (deepest > breathIn) breathIn += (deepest - breathIn) * jmin (1.0f, dt * 2.0f);
    else                    breathIn += (deepest - breathIn) * jmin (1.0f, dt * 6.0f);
    if (deepest > 0.0f) breathCol = breathCol.interpolatedWith (deepestCol, jmin (1.0f, dt * 3.0f));

    if (starMode)
    {
        for (auto& l : star) l.grow = jmin (1.0f, l.grow + dt / 0.7f);   // each line travels from point to point
        if (! starComplete && star.size() == 10 && star.back().grow >= 1.0f)
        {
            starComplete = true;   // the tenth line has closed the star: back to the source
            starFlash = 1.0f;
        }
        starIdle += dt;
        if (starFlash > 0.0f)
        {
            starFlash = jmax (0.0f, starFlash - dt / 3.5f);
            if (starFlash <= 0.0f) { star.clear(); starComplete = false; }
        }
        else if (! star.empty() && starIdle > 14.0f) { star.clear(); }   // left alone a while: it fades and starts over
    }

    for (auto& s : specks)
    {
        s.life += dt;
        if (s.inward) { s.radius -= s.speed * dt * (0.6f + 0.6f * s.radius); s.angle += 0.35f * dt; }   // gentle spiral in
        else          { s.radius += s.speed * dt; s.speed *= std::pow (0.6f, dt); s.angle -= 0.15f * dt; }
    }
    specks.erase (std::remove_if (specks.begin(), specks.end(), [] (const Speck& s)
                  { return s.life > s.maxLife || (s.inward && s.radius < 0.06f) || (! s.inward && s.radius > 1.35f); }), specks.end());
    for (auto& e : exhales) e.age += dt;
    exhales.erase (std::remove_if (exhales.begin(), exhales.end(), [] (const Exhale& e) { return e.age > e.duration; }), exhales.end());
    if (isShowing() || ! isVisible()) repaint();
}

// ---------------------------------------------------------------------------- hidden star mode
// The five points, clockwise from the top, and the ten-step path that draws the star: out from the top,
// across, down, back up... the seventh line returns to the top (half way), the tenth closes it for good.
static const int starPath[10][2] = { { 0, 4 }, { 4, 1 }, { 1, 3 }, { 3, 4 }, { 4, 2 },
                                     { 2, 3 }, { 3, 0 }, { 0, 2 }, { 2, 1 }, { 1, 0 } };

static Colour starColour (int step)   // the spectrum, root red up to crown violet, one hue per step
{
    return Colour::fromHSV (0.78f * (float) step / 9.0f, 0.82f, 1.0f, 1.0f);
}

void ResonanceView::setStarMode (bool on)
{
    starMode = on;
    for (int n = 0; n < 128; ++n) seenHits[(size_t) n] = proc.noteHits[(size_t) n].load();   // only notes from now on count
    star.clear();
    starFlash = 0.0f;
    starComplete = false;
    repaint();
}

void ResonanceView::addStarLine (int note)
{
    starIdle = 0.0f;
    if (starComplete || star.size() >= 10) { star.clear(); starComplete = false; }   // a new star begins
    star.push_back ({ (int) star.size(), note, 0.0f });
}

// One note's push on a point of the star. Each pitch has its own character:
//  - where it points: its place on the circle of fifths
//  - its symmetry (2-5 lobes): also from the circle of fifths, so neighbouring notes differ
//  - how tightly its ripples are spaced: low notes broad and gentle, high notes fine and busy
//  - how far it moves things: low notes swing wide, high notes shimmer in close
// A wave flows outward from the centre (its phase keeps moving), the shape breathes in and out, and on
// the attack the movement spreads from the middle to the edge like a ripple on water.
static Point<float> notePush (int note, float rr, float theta, float wavePhase, float age, float strength, float R)
{
    const int fifth = ((note % 12 + 12) % 12) * 7 % 12;
    const float theta0 = MathConstants<float>::twoPi * (float) fifth / 12.0f;
    const float lobes = (float) (2 + fifth % 4);
    const float t = jlimit (0.0f, 1.0f, (float) (note - 24) / 72.0f);   // 0 = low, 1 = high
    const float k = 0.7f + 3.4f * t;                                     // ripples across the radius
    const float amp = R * (0.065f - 0.034f * t) * strength;
    const float spin = (fifth % 2 == 0 ? 1.0f : -1.0f) * 0.25f;           // half the notes turn one way, half the other

    const float front = age * (1.1f + 1.4f * (1.0f - t));                // the onset spreading out from the centre
    const float gate = jlimit (0.0f, 1.0f, (front - rr) / 0.35f + 1.0f);
    if (gate <= 0.0f) return {};

    const float travel = MathConstants<float>::twoPi * k * rr - wavePhase;
    const float angular = lobes * (theta - theta0) + spin * wavePhase;
    const float radial = std::sin (travel) * std::cos (angular)
                       + 0.35f * std::sin (0.5f * wavePhase + (float) fifth) * rr;   // breathing: expand, contract
    const float swirl = 0.4f * std::cos (travel) * std::sin (angular);
    const float d = amp * gate * jmin (1.0f, rr * 6.0f);                 // the very centre stays put
    return { d * (radial * std::sin (theta) + swirl * std::cos (theta)),
             d * (-radial * std::cos (theta) + swirl * std::sin (theta)) };
}

Point<float> ResonanceView::warp (Point<float> p, Point<float> c, float R) const
{
    if (waves.empty()) return p;
    const auto rel = p - c;
    const float rr = rel.getDistanceFromOrigin() / R;
    const float theta = std::atan2 (rel.x, -rel.y);   // clockwise from the top, like the rest of the display
    Point<float> d;
    for (auto& w : waves)
    {
        // a bent note sits between two keys: blend their characters so bends glide instead of jumping
        const float lo = std::floor (w.midi), f = w.midi - lo;
        d += notePush ((int) lo, rr, theta, w.phase, w.age, w.strength * (1.0f - f), R);
        if (f > 0.001f) d += notePush ((int) lo + 1, rr, theta, w.phase, w.age, w.strength * f, R);
    }
    // a big chord adds up, but the star keeps its shape: soften anything beyond ~14% of the radius
    const float len = d.getDistanceFromOrigin(), lim = R * 0.14f;
    if (len > 1.0e-4f) d *= lim * std::tanh (len / lim) / len;
    return p + d;
}

Path ResonanceView::warpedLine (Point<float> a, Point<float> b, float upTo, Point<float> c, float R) const
{
    Path path;
    const int segs = waves.empty() ? 1 : 40;
    const int last = jmax (1, (int) std::ceil (segs * upTo));
    for (int i = 0; i <= last; ++i)
    {
        const float t = jmin (upTo, (float) i / (float) segs);
        const auto pt = warp (a + (b - a) * t, c, R);
        if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
    }
    return path;
}

void ResonanceView::paintStar (Graphics& g, Point<float> c, float R)
{
    const float r = R * 0.86f;
    Point<float> v[5];
    for (int i = 0; i < 5; ++i) v[i] = c.getPointOnCircumference (r, MathConstants<float>::twoPi * (float) i / 5.0f);

    // the outer circle and a faint outline of the whole star, so its shape is always hinted at
    {
        Path ring;
        for (int i = 0; i <= 120; ++i)
        {
            const auto pt = warp (c.getPointOnCircumference (R, MathConstants<float>::twoPi * (float) i / 120.0f), c, R);
            if (i == 0) ring.startNewSubPath (pt); else ring.lineTo (pt);
        }
        g.setColour (ui::goldDim.withAlpha (0.55f));
        g.strokePath (ring, PathStrokeType (1.0f));
    }
    g.setColour (ui::goldDim.withAlpha (0.18f));
    for (auto& p : starPath) g.strokePath (warpedLine (v[p[0]], v[p[1]], 1.0f, c, R), PathStrokeType (0.8f));

    const float idleFade = star.empty() ? 1.0f : jlimit (0.0f, 1.0f, 1.0f - (starIdle - 10.0f) / 4.0f);
    const float flash = starFlash;
    const float shimmer = 0.85f + 0.15f * std::sin (phase * 2.3f);

    for (auto& l : star)
    {
        const auto a = v[starPath[l.step][0]], b = v[starPath[l.step][1]];
        const auto line = warpedLine (a, b, l.grow, c, R);
        const auto end = warp (a + (b - a) * l.grow, c, R);
        const float ring = jlimit (0.0f, 1.0f, std::sqrt (proc.noteGlow[(size_t) l.note].load() * 3.0f));
        auto col = starColour (l.step).interpolatedWith (Colours::white, flash * 0.85f);
        const float alpha = idleFade * (0.45f + 0.55f * jmax (ring, flash)) * shimmer;
        const PathStrokeType::JointStyle js = PathStrokeType::curved;
        g.setColour (col.withAlpha (0.12f * alpha));
        g.strokePath (line, PathStrokeType (10.0f + 6.0f * ring, js, PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.30f * alpha));
        g.strokePath (line, PathStrokeType (4.5f + 2.0f * ring, js, PathStrokeType::rounded));
        g.setColour (col.brighter (0.4f).withAlpha (alpha));
        g.strokePath (line, PathStrokeType (1.6f, js, PathStrokeType::rounded));
        if (l.grow < 1.0f)   // the light travelling along the line
        {
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.95f * idleFade), end.x, end.y,
                                               col.withAlpha (0.0f), end.x + 9.0f, end.y, true));
            g.fillEllipse (Rectangle<float> (18.0f, 18.0f).withCentre (end));
        }
    }

    // the five points: lit once a line has reached them (they move with the music too)
    for (int i = 0; i < 5; ++i)
    {
        float lit = 0.0f;
        Colour col = ui::goldDim;
        for (auto& l : star)
            if ((starPath[l.step][1] == i && l.grow >= 1.0f) || starPath[l.step][0] == i) { lit = 1.0f; col = starColour (l.step); }
        if (i == 0 && star.size() >= 7 && star[6].grow >= 1.0f) col = Colours::white;   // half way: back at the top
        col = col.interpolatedWith (Colours::white, flash);
        const float sz = 6.0f + 4.0f * lit + 10.0f * flash;
        const auto pt = warp (v[i], c, R);
        g.setColour (col.withAlpha ((0.12f + 0.25f * lit + 0.4f * flash) * idleFade));
        g.fillEllipse (Rectangle<float> (sz * 2.2f, sz * 2.2f).withCentre (pt));
        g.setColour (col.withAlpha ((0.45f + 0.55f * jmax (lit, flash)) * idleFade));
        g.fillEllipse (Rectangle<float> (sz * 0.7f, sz * 0.7f).withCentre (pt));
    }

    // the white flash when the star closes: light fills it from the centre and spreads past the ring
    if (flash > 0.0f)
    {
        const float t = 1.0f - flash;
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f * flash), c.x, c.y,
                                           ui::gold.withAlpha (0.0f), c.x + R * (0.4f + 0.9f * t), c.y, true));
        g.fillEllipse (Rectangle<float> (R * 2.6f, R * 2.6f).withCentre (c));
        g.setColour (Colours::white.withAlpha (0.6f * flash));
        g.drawEllipse (Rectangle<float> (R * 2 * (0.3f + 0.9f * t), R * 2 * (0.3f + 0.9f * t)).withCentre (c), 1.5f + 2.0f * flash);
    }
}

void ResonanceView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 10.0f);

    auto c = b.getCentre();
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.42f;

    if (starMode)
    {
        paintStar (g, c, R);
    }
    else
    {
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
    // circle of fifths: each step clockwise is a fifth up, so notes that sound good together sit side by side
    static const char* pcs[] = { "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#", "F" };
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
        const float rad = R * (0.18f + 0.78f * jlimit (0.0f, 1.0f, (float) (n - 24) / 72.0f))
                        * (1.0f - 0.14f * charge (held[(size_t) n]));   // the ring draws in while the note is held
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
        const int fifth = ((n % 12) * 7) % 12;   // where this note sits on the circle of fifths
        auto col = fifthsColour (n);
        g.setColour (col.withAlpha (0.10f + 0.15f * v));
        g.strokePath (ring, PathStrokeType (5.0f + 4.0f * v));
        g.setColour (col.withAlpha (0.35f + 0.6f * v));
        g.strokePath (ring, PathStrokeType (1.0f + 1.5f * v));

        auto dot = c.getPointOnCircumference (R - 5.0f, MathConstants<float>::twoPi * (float) fifth / 12.0f);
        g.setColour (col.withAlpha (0.4f + 0.6f * v));
        g.fillEllipse (Rectangle<float> (5.0f + 5.0f * v, 5.0f + 5.0f * v).withCentre (dot));
    }
    }   // circle of fifths

    // ---- breath ----
    // breathing out: bright waves flowing from the centre past the ring
    for (auto& e : exhales)
    {
        const float t = e.age / e.duration;
        const float ease = 1.0f - std::pow (1.0f - t, 2.2f);
        const float rad = R * (0.08f + 1.15f * ease);
        const float fade = std::pow (1.0f - t, 1.4f) * e.strength;
        if (t < 0.35f)   // the first rush of light out of the centre
        {
            const float flash = std::pow (1.0f - t / 0.35f, 2.0f) * e.strength;
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f * flash), c.x, c.y, e.col.withAlpha (0.0f), c.x + R * (0.25f + 0.6f * ease), c.y, true));
            g.fillEllipse (Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
        }
        g.setColour (e.col.withAlpha (0.18f * fade));
        g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), 4.0f + 14.0f * fade);
        g.setColour (e.col.brighter (0.5f).withAlpha (0.75f * fade));
        g.drawEllipse (Rectangle<float> (rad * 2, rad * 2).withCentre (c), 1.0f + 2.5f * fade);
    }

    // breathing in: the light field draws inwards and gathers into a brightening core
    if (breathIn > 0.01f)
    {
        const float pull = R * (1.0f - 0.75f * breathIn);   // a soft edge closing in
        g.setColour (breathCol.withAlpha (0.10f + 0.25f * breathIn));
        g.drawEllipse (Rectangle<float> (pull * 2, pull * 2).withCentre (c), 1.0f + 2.0f * breathIn);
        const float core = R * (0.05f + 0.16f * breathIn);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.25f + 0.6f * breathIn), c.x, c.y,
                                           breathCol.withAlpha (0.0f), c.x + core * 2.2f, c.y, true));
        g.fillEllipse (Rectangle<float> (core * 4.4f, core * 4.4f).withCentre (c));
    }

    // glitter specks riding the breath
    for (auto& s : specks)
    {
        const float lifeFade = jmin (1.0f, s.life * 4.0f) * jlimit (0.0f, 1.0f, (s.maxLife - s.life) * 1.5f);
        const float twinkle = 0.55f + 0.45f * std::sin (phase * 9.0f + s.angle * 13.0f + s.size * 5.0f);
        const float a = lifeFade * twinkle * (s.inward ? jmin (1.0f, s.radius * 2.0f) : 1.0f);
        if (a < 0.03f) continue;
        auto p = c.getPointOnCircumference (R * s.radius, s.angle);
        g.setColour (s.col.withAlpha (0.22f * a));
        g.fillEllipse (Rectangle<float> (s.size * 3.2f, s.size * 3.2f).withCentre (p));
        g.setColour (s.col.withAlpha (a));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre (p));
    }

    g.setColour (ui::text.withAlpha (starMode ? 0.4f : 1.0f));   // the star takes the stage in star mode
    g.setFont (FontOptions (19.0f, Font::bold));
    g.drawFittedText (title, Rectangle<float> (R * 1.6f, 24).withCentre ({ c.x, c.y - 8 }).toNearestInt(), Justification::centred, 1);
    if (starMode) return;
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
        if (recorded)   // a real recording lives on this pad: show which one
        {
            for (auto& z : art->zones)
                if (z.rootNote == note)
                {
                    auto label = File::createFileWithoutCheckingPath (z.fileName).getFileNameWithoutExtension();
                    g.setColour (ui::text.withAlpha (0.75f));
                    g.setFont (FontOptions (10.5f));
                    g.drawFittedText (label, r.reduced (8, 18).toNearestInt(), Justification::centredLeft, 2, 0.8f);
                    break;
                }
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
                             : inRange (n) ? Colour (0xffeee3cf) : Colour (0xff4d5b52);
    if (over) c = c.brighter (0.1f);
    if (down) c = ui::teal;
    g.setColour (c);
    g.fillRect (area);
    g.setColour (lineCol);
    g.fillRect (area.withWidth (1.0f));
    if (art != nullptr && art->hasRootAt (n))
    {
        g.setColour (Colour (0xff1f8f55));
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
    auto c = isKeyswitch (n) ? ui::violet.darker (0.4f) : inRange (n) ? Colour (0xff0b120d) : Colour (0xff27352c);
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
    if (inst != nullptr && inst->name != "Empty")
    {
        if (auto* a = inst->get (proc.getArticulation (index))) newArt = a->name;
        auto value = [this] (const char* what) { return proc.params.getRawParameterValue (SolIntentProcessor::layerParam (index, what))->load(); };
        const int lo = (int) value ("KeyLo"), hi = (int) value ("KeyHi");
        if (lo > 0 || hi < 127) newArt << (newArt.isEmpty() ? "" : "  ") << sol::noteName (jmin (lo, hi)) << "-" << sol::noteName (jmax (lo, hi));
        if (value ("Ring") < 0.5f) newArt << (newArt.isEmpty() ? "" : "  ") << "held";
    }
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
