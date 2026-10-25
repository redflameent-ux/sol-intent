// The main window: header with presets, layer slots + library on the left, PLAY / MAP in the middle,
// sound shaping on the right, keyboard along the bottom.
#include <BinaryData.h>
#include "PluginEditor.h"

using namespace juce;

static void askName (Component* top, const String& title, const String& initial, std::function<void (String)> done)
{
    auto* w = new AlertWindow (title, {}, MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    top->addAndMakeVisible (w);
    w->centreAroundComponent (top, w->getWidth(), w->getHeight());
    w->enterModalState (true, ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1 && done) done (w->getTextEditorContents ("name").trim());
    }), true);
}

SolIntentEditor::SolIntentEditor (SolIntentProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);

    for (auto* b : { &libraryButton, &rescanButton, &stopButton, &presetButton, &prevPreset, &nextPreset, &savePreset,
                     &playTab, &mapTab, &fxTab, &rollTab, &newButton, &octDown, &octUp })
        addAndMakeVisible (b);

    libraryButton.setTooltip ("Choose the folder that holds your instrument folders");
    libraryButton.onClick = [this]
    {
        PopupMenu m;
        m.addItem (1, "Open library folder");
        m.addItem (2, "Choose a different library folder...");
        m.addSeparator();
        m.addItem (3, "Open presets folder");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&libraryButton), [this] (int r)
        {
            if (r == 1) sol::Library::getLibraryFolder().startAsProcess();
            if (r == 2) chooseLibraryFolder();
            if (r == 3) sol::Library::presetFolder().startAsProcess();
        });
    };
    rescanButton.onClick = [this] { proc.rescanLibrary(); refreshList(); };
    stopButton.onClick = [this] { proc.stopAllSound(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    savePreset.setTooltip ("Save every knob and both layers as a preset");
    savePreset.onClick = [this] { savePresetAs(); };
    playTab.setClickingTogglesState (false);
    mapTab.setClickingTogglesState (false);
    fxTab.setClickingTogglesState (false);
    playTab.onClick = [this] { showView (0); };
    mapTab.onClick  = [this] { showView (1); };
    fxTab.onClick   = [this] { showView (2); };
    rollTab.setClickingTogglesState (false);
    rollTab.onClick = [this] { showView (3); };
    rollTab.setTooltip ("Draw notes as lines and bend them - each note bends on its own (MPE). Plays along with Logic.");
    rollPage = std::make_unique<sol::gui::RollPage> (proc);
    addChildComponent (*rollPage);
    fxTab.setTooltip ("EQ, chorus, echo and space");
    mapTab.setTooltip ("Import recordings and set their root notes, key ranges, velocities and articulations");
    newButton.onClick = [this] { newInstrument(); };
    octDown.onClick = [this] { proc.padBaseNote = jmax (0, proc.padBaseNote - 12); pads.repaint(); };
    octUp.onClick   = [this] { proc.padBaseNote = jmin (112, proc.padBaseNote + 12); pads.repaint(); };

    for (int i = 0; i < SolIntentProcessor::numLayers; ++i)
    {
        cards[(size_t) i] = std::make_unique<LayerCard> (proc, i);
        cards[(size_t) i]->onActivate = [this, i] { setActiveLayer (i); };
        addAndMakeVisible (*cards[(size_t) i]);
    }

    browser.setRowHeight (26);
    addAndMakeVisible (browser);
    addAndMakeVisible (resonance);
    addAndMakeVisible (pads);
    addChildComponent (mapView);
    addChildComponent (fxView);
    addAndMakeVisible (morph);

    keyboard.setKeyWidth (16.0f);
    keyboard.setAvailableRange (12, 108);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xff34423a));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keyboard.setColour (MidiKeyboardComponent::upDownButtonBackgroundColourId, ui::panelHi);
    keyboard.setColour (MidiKeyboardComponent::upDownButtonArrowColourId, ui::gold);
    addAndMakeVisible (keyboard);

    statusLabel.setFont (FontOptions (12.0f));
    statusLabel.setColour (Label::textColourId, ui::textDim);
    addAndMakeVisible (statusLabel);
    padRangeLabel.setFont (FontOptions (11.0f));
    padRangeLabel.setColour (Label::textColourId, ui::textDim);
    padRangeLabel.setJustificationType (Justification::centred);
    addAndMakeVisible (padRangeLabel);

    struct Def { const char* id; const char* label; bool bipolar; };
    // 0-3 the selected layer's envelope (re-pointed when you pick another layer), 4-5 motion,
    // 6-9 macros, 10-14 overall sound
    const Def defs[] = {
        { "aAtk", "Attack", false }, { "aDec", "Decay", false }, { "aSus", "Sustain", false }, { "aRel", "Release", false },
        { "lfoRate", "Rate 1", false }, { "lfoDepth", "Amount 1", false },
        { "macro1", "Macro 1", false }, { "macro2", "Macro 2", false }, { "macro3", "Macro 3", false }, { "macro4", "Macro 4", false },
        { "volume", "Volume", false }, { "tune", "Tune", true }, { "fine", "Fine", true }, { "velSens", "Touch", false },
        { "bendRange", "Bend", false },
        { "lfoRate2", "Rate 2", false }, { "lfoDepth2", "Amount 2", false } };   // 15-16 Motion 2
    for (auto& d : defs)
        addAndMakeVisible (knobs.add (new ParamKnob (proc, d.id, d.label, d.bipolar)));

    letRing.setTooltip ("On (one-shot): this layer's recordings ring out fully after you let go, like the real instrument.\n"
                        "Off (sustained): they play while you hold the key, then fade by the Release time.");
    addAndMakeVisible (letRing);

    for (auto* zs : { &zoneLo, &zoneHi })
    {
        zs->setSliderStyle (Slider::LinearBar);
        zs->setTextBoxStyle (Slider::TextBoxBelow, false, 60, 18);
        zs->setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        zs->setColour (Slider::textBoxTextColourId, ui::text);
        zs->setVelocityBasedMode (true);
        zs->setVelocityModeParameters (0.5, 1, 0.0, false);
        addAndMakeVisible (zs);
    }
    zoneLo.setTooltip ("Lowest key this layer plays. Drag, or double-click and type a note like C2.");
    zoneHi.setTooltip ("Highest key this layer plays. Split the keyboard between layers, e.g. bowls low and chimes high.");
    zoneL.setText ("KEYS", dontSendNotification);
    zoneL.setFont (FontOptions (10.5f));
    zoneL.setColour (Label::textColourId, ui::goldDim.brighter (0.4f));
    addAndMakeVisible (zoneL);
    pointLayerControlsAt (0);

    auto setupCombo = [this] (ComboBox& box, Label& l, const String& id, const String& text)
    {
        if (auto* choice = dynamic_cast<AudioParameterChoice*> (proc.params.getParameter (id)))
            box.addItemList (choice->choices, 1);
        addAndMakeVisible (box);
        l.setText (text, dontSendNotification);
        l.setFont (FontOptions (10.5f));
        l.setJustificationType (Justification::centred);
        l.setColour (Label::textColourId, ui::goldDim.brighter (0.4f));
        addAndMakeVisible (l);
    };
    setupCombo (keyswitch, keyswitchL, "keyswitch", "KEYSWITCH");
    keyswitch.setTooltip ("Where the articulation keys start (violet on the keyboard). Only active when an instrument has more than one articulation.");
    keyswitchAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "keyswitch", keyswitch);

    for (auto* k : knobs) rightPanel.push_back (k);
    for (auto* c : std::initializer_list<Component*> { &letRing, &zoneLo, &zoneHi, &zoneL, &keyswitch, &keyswitchL })
        rightPanel.push_back (c);

    refreshList();
    showView (0);
    setSize (1200, 798);
    startTimerHz (30);
}

SolIntentEditor::~SolIntentEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------- painting
static Image makeStarField (int w, int h)
{
    Image img (Image::RGB, jmax (1, w), jmax (1, h), true);
    Graphics g (img);
    g.fillAll (ui::bg);

    // faint nebula clouds
    auto cloud = [&g] (float cx, float cy, float r, Colour c)
    {
        g.setGradientFill (ColourGradient (c, cx, cy, c.withAlpha (0.0f), cx + r, cy, true));
        g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    };
    cloud ((float) w * 0.18f, (float) h * 0.25f, (float) w * 0.45f, Colour (0x404b3bd6));
    cloud ((float) w * 0.80f, (float) h * 0.75f, (float) w * 0.40f, Colour (0x341f8a4a));
    cloud ((float) w * 0.55f, (float) h * 0.35f, (float) w * 0.30f, Colour (0x2a1c7a52));
    cloud ((float) w * 0.35f, (float) h * 0.90f, (float) w * 0.30f, Colour (0x26c28a2a));

    // stars
    Random rng (7);
    for (int i = 0; i < (w * h) / 1500; ++i)
    {
        float x = rng.nextFloat() * (float) w, y = rng.nextFloat() * (float) h;
        float b = std::pow (rng.nextFloat(), 3.0f);
        float size = 0.6f + 1.6f * b;
        auto tint = rng.nextFloat() < 0.15f ? ui::gold : rng.nextFloat() < 0.2f ? ui::teal : Colours::white;
        if (b > 0.6f)
        {
            g.setColour (tint.withAlpha (0.12f));
            g.fillEllipse (x - size * 2.5f, y - size * 2.5f, size * 5, size * 5);
        }
        g.setColour (tint.withAlpha (0.25f + 0.7f * b));
        g.fillEllipse (x - size * 0.5f, y - size * 0.5f, size, size);
    }
    return img;
}

void SolIntentEditor::paint (Graphics& g)
{
    if (starField.getWidth() != getWidth() || starField.getHeight() != getHeight())
        starField = makeStarField (getWidth(), getHeight());
    g.drawImageAt (starField, 0, 0);

    // logo, top left
    static const Image logo = ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    auto logoBox = Rectangle<float> (16.0f, 3.0f, 0.0f, 62.0f);
    if (logo.isValid())
    {
        logoBox.setWidth (logoBox.getHeight() * (float) logo.getWidth() / (float) logo.getHeight());
        g.drawImage (logo, logoBox, RectanglePlacement::centred);
    }
    g.setColour (ui::textDim);
    g.setFont (FontOptions (11.5f));
    g.drawText ("resonance sampler", (int) logoBox.getRight() + 12, 26, 130, 18, Justification::centredLeft);

    auto panelBox = [&g] (Rectangle<int> r)
    {
        g.setColour (ui::panel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (ui::goldDim.withAlpha (0.55f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    };
    panelBox (browserArea);

    auto heading = [&g] (Rectangle<int> r, const String& t)
    {
        g.setColour (ui::goldDim.brighter (0.3f));
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (t, r, Justification::centredLeft);
        g.setColour (ui::line);
        g.drawHorizontalLine (r.getBottom() - 1, (float) r.getX() + 90.0f, (float) r.getRight());
    };
    heading (browserArea.reduced (14, 0).withHeight (30), "INSTRUMENTS");
    if (view == 3) return;   // the ROLL page covers the right-hand side
    panelBox (knobArea);
    heading (sectionEnv, "LAYER " + SolIntentProcessor::layerLetter (proc.activeLayer));
    heading (sectionMotion, "MOTION  (right-click a knob > Modulate with)");
    heading (sectionMacros, "MACROS");
    heading (sectionSound, "OVERALL");
}

void SolIntentEditor::paintOverChildren (Graphics& g)
{
    if (! dragging) return;
    g.setColour (ui::bg.withAlpha (0.75f));
    g.fillRect (getLocalBounds());
    g.setColour (ui::gold);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 14.0f, 2.0f);
    g.setFont (FontOptions (22.0f, Font::bold));
    auto target = proc.getLayerInfo (proc.activeLayer);
    g.drawText (target.isDemo() ? "Drop to make a new instrument from these recordings"
                                : "Drop to add these recordings to " + target.name,
                getLocalBounds(), Justification::centred);
}

void SolIntentEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    // header
    auto top = area.removeFromTop (52).withTrimmedTop (8).withHeight (32);
    stopButton.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    rescanButton.setBounds (top.removeFromRight (72));
    top.removeFromRight (6);
    libraryButton.setBounds (top.removeFromRight (84));
    auto presetBar = top.withSizeKeepingCentre (380, 32).withX (getWidth() / 2 - 150);
    prevPreset.setBounds (presetBar.removeFromLeft (32));
    presetBar.removeFromLeft (4);
    savePreset.setBounds (presetBar.removeFromRight (62));
    presetBar.removeFromRight (4);
    nextPreset.setBounds (presetBar.removeFromRight (32));
    presetBar.removeFromRight (4);
    presetButton.setBounds (presetBar);
    area.removeFromTop (8);

    // bottom: keyboard + status
    keyboard.setBounds (area.removeFromBottom (82));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 57.0f);   // C0..C8 fills the width
    keyboard.setLowestVisibleKey (12);
    area.removeFromBottom (6);
    statusLabel.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);

    // left column: layers, library
    leftArea = area.removeFromLeft (262);
    area.removeFromLeft (12);
    auto left = leftArea;
    for (auto& card : cards)
    {
        card->setBounds (left.removeFromTop (76));
        left.removeFromTop (6);
    }
    left.removeFromTop (4);
    browserArea = left;
    auto br = browserArea.reduced (8);
    br.removeFromTop (24);
    newButton.setBounds (br.removeFromBottom (30));
    br.removeFromBottom (6);
    browser.setBounds (br);

    // right column: shaping
    knobArea = area.removeFromRight (340);
    area.removeFromRight (12);
    auto k = knobArea.reduced (14, 8);
    const int cw = k.getWidth() / 4, kh = 76;
    auto place = [&] (int i, Rectangle<int> cell) { knobs[i]->setBounds (cell.reduced (2, 0)); };

    // the selected layer: envelope, one-shot / sustained, key zone
    sectionEnv = k.removeFromTop (22);
    auto row = k.removeFromTop (kh);
    for (int i = 0; i < 4; ++i) place (i, row.removeFromLeft (cw));
    row = k.removeFromTop (30);
    letRing.setBounds (row.removeFromLeft (cw + 20).reduced (0, 2));
    zoneL.setBounds (row.removeFromLeft (38));
    zoneLo.setBounds (row.removeFromLeft ((row.getWidth() - 6) / 2).reduced (0, 3));
    row.removeFromLeft (6);
    zoneHi.setBounds (row.reduced (0, 3));
    k.removeFromTop (6);

    sectionMotion = k.removeFromTop (22);
    row = k.removeFromTop (kh);
    place (4, row.removeFromLeft (cw));
    place (5, row.removeFromLeft (cw));
    place (15, row.removeFromLeft (cw));
    place (16, row.removeFromLeft (cw));
    k.removeFromTop (4);

    sectionMacros = k.removeFromTop (22);
    row = k.removeFromTop (kh);
    for (int i = 0; i < 4; ++i) place (6 + i, row.removeFromLeft (cw));
    k.removeFromTop (4);

    sectionSound = k.removeFromTop (22);
    row = k.removeFromTop (kh);
    for (int i = 0; i < 4; ++i) place (10 + i, row.removeFromLeft (cw));
    row = k.removeFromTop (kh);
    place (14, row.removeFromLeft (cw));
    row.removeFromLeft (cw / 2);
    auto ksCell = row.reduced (4, 0);
    keyswitchL.setBounds (ksCell.removeFromTop (14));
    keyswitch.setBounds (ksCell.withSizeKeepingCentre (ksCell.getWidth(), 28).translated (0, -8));

    // centre
    centreArea = area;
    auto c = area;
    auto tabs = c.removeFromTop (30);
    playTab.setBounds (tabs.removeFromLeft (90));
    tabs.removeFromLeft (6);
    mapTab.setBounds (tabs.removeFromLeft (90));
    tabs.removeFromLeft (6);
    fxTab.setBounds (tabs.removeFromLeft (90));
    tabs.removeFromLeft (6);
    rollTab.setBounds (tabs.removeFromLeft (90));
    c.removeFromTop (8);
    if (rollPage != nullptr)   // the roll gets the centre and the right-hand column
        rollPage->setBounds (c.getX(), c.getY(), knobArea.getRight() - c.getX(), knobArea.getBottom() - c.getY());
    mapView.setBounds (c);
    fxView.setBounds (c);

    auto playTop = c.removeFromTop (artButtons.isEmpty() ? 236 : 210);
    morph.setBounds (playTop.removeFromRight (jmin (playTop.getHeight() + 20, playTop.getWidth() / 2)));
    playTop.removeFromRight (8);
    resonance.setBounds (playTop);
    c.removeFromTop (8);
    auto artRow = artButtons.isEmpty() ? Rectangle<int>() : c.removeFromTop (28);
    int bx = artRow.getX();
    for (auto* b : artButtons)
    {
        int w = jmin (150, jmax (80, b->getBestWidthForHeight (28) + 20));
        b->setBounds (bx, artRow.getY(), w, 28);
        bx += w + 6;
    }
    if (! artButtons.isEmpty()) c.removeFromTop (8);
    auto padHeader = c.removeFromTop (24);
    octDown.setBounds (padHeader.removeFromLeft (28));
    octUp.setBounds (padHeader.removeFromRight (28));
    padRangeLabel.setBounds (padHeader);
    c.removeFromTop (6);
    pads.setBounds (c);
}

// ---------------------------------------------------------------------------- modes & layers
void SolIntentEditor::showMap (bool on) { showView (on ? 1 : 0); }

void SolIntentEditor::showView (int v)
{
    view = v;
    mapMode = v == 1;
    const bool play = v == 0;
    mapView.setVisible (v == 1);
    fxView.setVisible (v == 2);
    if (rollPage != nullptr) rollPage->setVisible (v == 3);
    for (auto* c : rightPanel) c->setVisible (v != 3);
    for (auto* comp : std::initializer_list<Component*> { &resonance, &morph, &pads, &octDown, &octUp, &padRangeLabel })
        comp->setVisible (play);
    for (auto* b : artButtons) b->setVisible (play);
    playTab.setToggleState (v == 0, dontSendNotification);
    mapTab.setToggleState (v == 1, dontSendNotification);
    fxTab.setToggleState (v == 2, dontSendNotification);
    rollTab.setToggleState (v == 3, dontSendNotification);
    repaint();
    if (v == 1)
        mapView.setTarget (proc.getLayerInfo (proc.activeLayer), proc.getArticulation (proc.activeLayer));
}

// The LAYER section on the right always edits whichever layer card is selected.
void SolIntentEditor::pointLayerControlsAt (int layer)
{
    if (layer == controlsLayer) return;
    controlsLayer = layer;
    auto id = [layer] (const char* what) { return SolIntentProcessor::layerParam (layer, what); };
    knobs[0]->setParam (id ("Atk"), "Attack");
    knobs[1]->setParam (id ("Dec"), "Decay");
    knobs[2]->setParam (id ("Sus"), "Sustain");
    knobs[3]->setParam (id ("Rel"), "Release");
    letRingAtt.reset();
    letRingAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id ("Ring"), letRing);
    zoneLoAtt.reset(); zoneHiAtt.reset();
    zoneLoAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id ("KeyLo"), zoneLo);
    zoneHiAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.params, id ("KeyHi"), zoneHi);
    zoneLo.setDoubleClickReturnValue (true, 0.0);
    zoneHi.setDoubleClickReturnValue (true, 127.0);
    repaint (sectionEnv);
}

void SolIntentEditor::setActiveLayer (int i)
{
    proc.activeLayer = i;
    pointLayerControlsAt (i);
    for (auto& card : cards) card->repaint();
    browser.repaint();
    shownLayer = -1;   // forces a refresh
    if (mapMode) mapView.setTarget (proc.getLayerInfo (i), proc.getArticulation (i));
}

void SolIntentEditor::rebuildArticulationButtons()
{
    artButtons.clear();
    auto inst = proc.getDisplayInstrument (proc.activeLayer);
    if (inst == nullptr || inst->numArticulations() < 2) { resized(); return; }
    const int ks = proc.getKeyswitchBase();
    for (int i = 0; i < inst->numArticulations(); ++i)
    {
        auto label = inst->articulations[(size_t) i].name + (ks >= 0 ? "  " + sol::noteName (ks + i) : String());
        auto* b = artButtons.add (new TextButton (label));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { proc.setArticulation (proc.activeLayer, i); };
        addAndMakeVisible (b);
        b->setVisible (view == 0);
    }
    resized();
}

void SolIntentEditor::refreshInstrumentViews()
{
    const int layer = proc.activeLayer;
    auto inst = proc.getDisplayInstrument (layer);
    const int artIndex = proc.getArticulation (layer);
    const int ks = proc.getKeyswitchBase();

    const bool instChanged = inst != shown[layer] || layer != shownLayer;
    if (! instChanged && artIndex == shownArt && ks == shownKs)
        return;

    if (instChanged || ks != shownKs) rebuildArticulationButtons();
    shown[layer] = inst;
    shownLayer = layer; shownArt = artIndex; shownKs = ks;

    for (int i = 0; i < artButtons.size(); ++i)
        artButtons[i]->setToggleState (i == artIndex, dontSendNotification);

    const sol::Articulation* a = inst != nullptr ? inst->get (artIndex) : nullptr;
    pads.setArticulation (a, inst);
    keyboard.setArticulation (a, inst, inst != nullptr && inst->numArticulations() > 1 ? ks : -1,
                              inst != nullptr ? inst->numArticulations() : 0);
    if (a == nullptr) { resonance.title = "Loading..."; resonance.subtitle = {}; return; }

    // pads and keyboard follow the recordings
    if (instChanged && ! a->zones.empty())
    {
        if (a->lowestKey > proc.padBaseNote + 15 || a->highestKey < proc.padBaseNote)
        {
            int lowRoot = 127;
            for (auto& z : a->zones) lowRoot = jmin (lowRoot, z.rootNote);
            proc.padBaseNote = jlimit (0, 112, lowRoot - lowRoot % 12);
        }
    }

    std::vector<int> roots;
    for (auto& z : a->zones)
        if (std::find (roots.begin(), roots.end(), z.rootNote) == roots.end()) roots.push_back (z.rootNote);
    int lo = 127, hi = 0;
    for (int r : roots) { lo = jmin (lo, r); hi = jmax (hi, r); }

    resonance.title = inst->name;
    resonance.subtitle = a->zones.empty() ? String ("No recordings yet - open MAP to add some")
                       : (inst->numArticulations() > 1 ? a->name + ui::u8 ("  \xc2\xb7  ") : String())
                         + String ((int) a->zones.size()) + (a->zones.size() == 1 ? " recording" : " recordings")
                         + ui::u8 ("  \xc2\xb7  ") + (lo == hi ? sol::noteName (lo) : sol::noteName (lo) + " to " + sol::noteName (hi));
}

// ---------------------------------------------------------------------------- library list
int SolIntentEditor::getNumRows() { return (int) rows.size(); }

void SolIntentEditor::refreshList()
{
    rows.clear();
    String lastCat;
    const auto& list = proc.getInstrumentList();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (list[(size_t) i].category != lastCat)
        {
            lastCat = list[(size_t) i].category;
            rows.push_back ({ true, lastCat.toUpperCase(), -1 });
        }
        rows.push_back ({ false, list[(size_t) i].name, i });
    }
    if (list.size() <= 1)
        rows.push_back ({ true, "Drop WAV files on the plugin or press + New instrument", -1 });
    browser.updateContent();
    browser.repaint();
}

void SolIntentEditor::paintListBoxItem (int row, Graphics& g, int w, int h, bool)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];
    if (r.header)
    {
        g.setColour (ui::textDim);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawFittedText (r.label, 6, 0, w - 12, h, Justification::bottomLeft, 2);
        return;
    }
    const auto& info = proc.getInstrumentList()[(size_t) r.index];
    const bool inActive = proc.getLayerInfo (proc.activeLayer).folder == info.folder && proc.getLayerInfo (proc.activeLayer).name == info.name;
    String others;   // letters of the other layers using this instrument
    for (int k = 0; k < SolIntentProcessor::numLayers; ++k)
        if (k != proc.activeLayer && proc.getDisplayInstrument (k) != nullptr
            && proc.getLayerInfo (k).folder == info.folder && proc.getLayerInfo (k).name == info.name)
            others << SolIntentProcessor::layerLetter (k);
    const bool inOther = others.isNotEmpty();
    if (inActive)
    {
        g.setColour (ui::panelHi);
        g.fillRoundedRectangle (2.0f, 2.0f, (float) w - 4, (float) h - 4, 5.0f);
        g.setColour (ui::gold);
        g.fillRoundedRectangle (2.0f, 6.0f, 3.0f, (float) h - 12, 1.5f);
    }
    g.setColour (inActive ? ui::gold : ui::text);
    g.setFont (FontOptions (13.0f));
    g.drawText (r.label, 14, 0, w - 44, h, Justification::centredLeft, true);
    if (inOther)
    {
        g.setColour (ui::textDim);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (others, w - 40, 0, 34, h, Justification::centredRight);
    }
}

void SolIntentEditor::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].header && e.mods.isPopupMenu())
    {
        const int index = rows[(size_t) row].index;
        const auto info = proc.getInstrumentList()[(size_t) index];
        PopupMenu m;
        m.addSectionHeader (info.name);
        m.addItem (1, "Load into layer " + SolIntentProcessor::layerLetter (proc.activeLayer));
        m.addItem (2, "Show the folder", ! info.isDemo());
        m.addSeparator();
        m.addItem (3, "Remove from library...", ! info.isDemo());
        m.showMenuAsync (PopupMenu::Options(), [this, index, info] (int r)
        {
            if (r == 1) { proc.selectInstrument (proc.activeLayer, index); browser.repaint(); }
            if (r == 2) info.folder.revealToUser();
            if (r == 3) removeInstrument (index);
        });
        return;
    }
    if (row >= 0 && row < (int) rows.size() && ! rows[(size_t) row].header)
    {
        proc.selectInstrument (proc.activeLayer, rows[(size_t) row].index);
        proc.params.getParameter (SolIntentProcessor::layerParam (proc.activeLayer, "On"))->setValueNotifyingHost (1.0f);
        browser.repaint();
        if (mapMode) mapView.setTarget (proc.getLayerInfo (proc.activeLayer), 0);
    }
}

// Backspace / Delete on the selected instrument
void SolIntentEditor::deleteKeyPressed (int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < (int) rows.size() && ! rows[(size_t) lastRowSelected].header)
        removeInstrument (rows[(size_t) lastRowSelected].index);
}

void SolIntentEditor::removeInstrument (int index)
{
    if (index < 0 || index >= (int) proc.getInstrumentList().size()) return;
    const auto info = proc.getInstrumentList()[(size_t) index];
    if (info.isDemo()) { proc.setStatus ("The built-in demo can't be removed."); return; }

    auto* w = new AlertWindow ("Remove \"" + info.name + "\"?",
                               "Its folder and the recordings in it go to the Trash, so you can get them back from there if you change your mind.",
                               MessageBoxIconType::NoIcon);
    w->addButton ("Move to Trash", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (w);
    w->centreAroundComponent (this, w->getWidth(), w->getHeight());
    w->enterModalState (true, ModalCallbackFunction::create ([this, info] (int r)
    {
        if (r != 1) return;
        for (int k = 0; k < SolIntentProcessor::numLayers; ++k)   // stop using it first
            if (proc.getLayerInfo (k).folder == info.folder)
                proc.clearLayer (k);
        if (mapView.getTarget().folder == info.folder)
            mapView.setTarget (proc.getLayerInfo (proc.activeLayer), 0);
        const bool ok = info.folder.moveToTrash();
        proc.rescanLibrary();
        refreshList();
        proc.setStatus (ok ? "Moved \"" + info.name + "\" to the Trash."
                           : "Couldn't move \"" + info.name + "\" to the Trash - it may be open in another program.");
    }), true);
}

void SolIntentEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<FileChooser> ("Choose your library folder", sol::Library::getLibraryFolder());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this] (const FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.isDirectory())
                              {
                                  sol::Library::setLibraryFolder (f);
                                  proc.rescanLibrary();
                                  refreshList();
                              }
                          });
}

void SolIntentEditor::newInstrument (std::function<void (File)> then)
{
    askName (this, "Name the new instrument", "", [this, then] (String name)
    {
        if (name.isEmpty()) return;
        auto folder = sol::Library::createInstrument (name);
        proc.loadFolderIntoLayer (proc.activeLayer, folder);
        proc.params.getParameter (SolIntentProcessor::layerParam (proc.activeLayer, "On"))->setValueNotifyingHost (1.0f);
        refreshList();
        showMap (true);
        mapView.setTarget (proc.getLayerInfo (proc.activeLayer), 0);
        if (then) then (folder);
    });
}

// ---------------------------------------------------------------------------- file drops
bool SolIntentEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).isDirectory() || sol::Library::isAudioFile (File (f))) return true;
    return false;
}

void SolIntentEditor::filesDropped (const StringArray& files, int, int)
{
    dragging = false;
    repaint();
    auto target = proc.getLayerInfo (proc.activeLayer);
    File first (files[0]);
    auto suggested = first.isDirectory() ? first.getFileName() : first.getParentDirectory().getFileName();

    auto makeNew = [this, files, suggested]
    {
        askName (this, "Name the new instrument", suggested, [this, files] (String name)
        {
            if (name.isEmpty()) return;
            auto folder = sol::Library::createInstrument (name);
            proc.loadFolderIntoLayer (proc.activeLayer, folder);
            proc.params.getParameter (SolIntentProcessor::layerParam (proc.activeLayer, "On"))->setValueNotifyingHost (1.0f);
            refreshList();
            showMap (true);
            mapView.setTarget (proc.getLayerInfo (proc.activeLayer), 0);
            mapView.importWithChoice (files);
        });
    };

    if (target.isDemo() || ! target.folder.isDirectory()) { makeNew(); return; }

    // an instrument of yours is loaded: make sure the drop goes where it's meant to
    auto* w = new AlertWindow ("Where should these go?", {}, MessageBoxIconType::NoIcon);
    w->addButton ("New instrument", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Add to " + target.name, 2);
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    addAndMakeVisible (w);
    w->centreAroundComponent (this, w->getWidth(), w->getHeight());
    w->enterModalState (true, ModalCallbackFunction::create ([this, files, makeNew] (int r)
    {
        if (r == 1) makeNew();
        if (r == 2) { showMap (true); mapView.importWithChoice (files); }
    }), true);
}

// ---------------------------------------------------------------------------- presets
void SolIntentEditor::showPresetMenu()
{
    PopupMenu m;
    auto presets = proc.getPresets();
    m.addItem (1, "Init (reset the sound, keep the instruments)");
    m.addSeparator();
    for (int i = 0; i < presets.size(); ++i)
        m.addItem (100 + i, presets[i].getFileNameWithoutExtension(), true, presets[i].getFileNameWithoutExtension() == proc.currentPresetName);
    if (presets.isEmpty()) m.addItem (2, "(no presets saved yet)", false);
    m.addSeparator();
    m.addItem (3, "Save as new preset...");
    m.addItem (4, "Open presets folder");
    m.addSeparator();
    m.addItem (5, "Presets also change instruments", true, proc.presetsChangeInstruments());
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetButton), [this, presets] (int r)
    {
        if (r == 1)
        {
            for (auto* prm : proc.getParameters())
            {
                auto* ranged = dynamic_cast<RangedAudioParameter*> (prm);
                auto id = ranged != nullptr ? ranged->getParameterID() : String();
                // keep which layers are on and their key zones - those belong to the instruments
                if (id.length() > 1 && (id.substring (1) == "On" || id.substring (1) == "KeyLo" || id.substring (1) == "KeyHi")) continue;
                prm->setValueNotifyingHost (prm->getDefaultValue());
            }
            for (int k = 0; k < SolIntentProcessor::numMacros; ++k) proc.clearMacro (k);
            proc.currentPresetName = "Init";
        }
        if (r == 3) savePresetAs();
        if (r == 4) sol::Library::presetFolder().startAsProcess();
        if (r == 5)
        {
            proc.setPresetsChangeInstruments (! proc.presetsChangeInstruments());
            proc.setStatus (proc.presetsChangeInstruments() ? "Presets will now load their own instruments too."
                                                            : "Presets now change only the sound and keep your instruments.");
        }
        if (r >= 100 && r - 100 < presets.size()) proc.loadPreset (presets[r - 100], proc.presetsChangeInstruments());
    });
}

void SolIntentEditor::stepPreset (int delta)
{
    auto presets = proc.getPresets();
    if (presets.isEmpty()) return;
    int cur = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].getFileNameWithoutExtension() == proc.currentPresetName) cur = i;
    int next = cur < 0 ? (delta > 0 ? 0 : presets.size() - 1) : (cur + delta + presets.size()) % presets.size();
    proc.loadPreset (presets[next], proc.presetsChangeInstruments());
}

void SolIntentEditor::savePresetAs()
{
    auto initial = proc.currentPresetName == "Init" ? String() : proc.currentPresetName;
    askName (this, "Save preset as", initial, [this] (String name)
    {
        if (name.isNotEmpty() && proc.savePreset (name))
            proc.setStatus ("Saved preset \"" + name + "\".");
    });
}

// ---------------------------------------------------------------------------- refresh
void SolIntentEditor::timerCallback()
{
    refreshInstrumentViews();
    for (auto& card : cards) card->refresh();

    if (proc.currentPresetName != shownPreset)
    {
        shownPreset = proc.currentPresetName;
        presetButton.setButtonText (shownPreset);
    }

    for (auto* k : knobs) k->refreshLearnState();
    if (view == 2) fxView.refresh();
    if (view == 0) morph.repaint();

    String status;
    if (proc.getLearningParam().isNotEmpty())
        status = "MIDI learn: move a knob, fader or pedal on your controller to link it. Right-click the knob again to cancel.";
    else
        status = proc.getStatus();
    if (status.isEmpty())
        status = mapMode ? "Click a recording to edit it. Play a key, then Root = last key to set where it sits."
                         : ui::u8 ("Green dots mark keys with a real recording. Pitch bend reaches \xc2\xb1")
                           + String ((int) proc.params.getRawParameterValue ("bendRange")->load())
                           + " semitones. Right-click any knob to link it to your controller.";
    if (statusLabel.getText() != status) statusLabel.setText (status, dontSendNotification);

    padRangeLabel.setText ("PADS  " + sol::noteName (proc.padBaseNote) + " to " + sol::noteName (proc.padBaseNote + 15), dontSendNotification);

    if (mapMode)
    {
        mapView.refreshLastKey();
        mapView.repaint();
        if (proc.getLayerInfo (proc.activeLayer).folder != mapView.getTarget().folder
            || proc.getLayerInfo (proc.activeLayer).name != mapView.getTarget().name)
            mapView.setTarget (proc.getLayerInfo (proc.activeLayer), proc.getArticulation (proc.activeLayer));
    }
    else
    {
        resonance.tick();
        pads.repaint();
    }
}
