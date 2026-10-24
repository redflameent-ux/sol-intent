// The MAP screen: import recordings, set root notes, key ranges, velocities and articulations.
// Every change is written to the instrument's instrument.json and the sound reloads a moment later.
#include "PluginEditor.h"

using namespace juce;

static void askTextBox (Component* from, const String& title, const String& initial, std::function<void (String)> done)
{
    auto* top = from->findParentComponentOfClass<AudioProcessorEditor>();
    auto* w = new AlertWindow (title, {}, MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    if (top != nullptr) { top->addAndMakeVisible (w); w->centreAroundComponent (top, w->getWidth(), w->getHeight()); }
    w->enterModalState (true, ModalCallbackFunction::create ([w, done] (int r)
    {
        if (r == 1 && done) done (w->getTextEditorContents ("name").trim());
    }), true);
}

MapView::MapView (SolIntentProcessor& p) : proc (p)
{
    addAndMakeVisible (artBox);
    artBox.onChange = [this]
    {
        if (updating) return;
        int idx = artBox.getSelectedItemIndex();
        if (idx >= 0) { art = idx; selected = currentArt() != nullptr && ! currentArt()->zones.empty() ? 0 : -1; proc.setArticulation (proc.activeLayer, art); refreshControls(); }
    };

    for (auto* b : { &addArt, &renameArt, &deleteArt, &importButton, &setRoot, &playZone, &autoRanges, &removeZone })
        addAndMakeVisible (b);

    addArt.onClick = [this]
    {
        askTextBox (this, "Name the new articulation (e.g. Swell, Rim, Roll)", "", [this] (String name)
        {
            if (name.isEmpty()) return;
            spec.articulations.push_back ({ name, {} });
            art = (int) spec.articulations.size() - 1;
            selected = -1;
            changed();
            proc.setArticulation (proc.activeLayer, art);
            refreshArticulationBox();
            refreshControls();
        });
    };
    renameArt.onClick = [this]
    {
        if (auto* a = currentArt())
            askTextBox (this, "Rename articulation", a->name, [this] (String name)
            {
                if (auto* a2 = currentArt(); a2 != nullptr && name.isNotEmpty()) { a2->name = name; changed(); refreshArticulationBox(); }
            });
    };
    deleteArt.onClick = [this]
    {
        if (spec.articulations.size() <= 1) return;
        auto* top = findParentComponentOfClass<AudioProcessorEditor>();
        auto* w = new AlertWindow ("Remove \"" + currentArt()->name + "\"?",
                                   "Its mapping is removed. The recordings stay in the folder.", MessageBoxIconType::NoIcon);
        w->addButton ("Remove", 1); w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        if (top != nullptr) { top->addAndMakeVisible (w); w->centreAroundComponent (top, w->getWidth(), w->getHeight()); }
        w->enterModalState (true, ModalCallbackFunction::create ([this] (int r)
        {
            if (r != 1) return;
            spec.articulations.erase (spec.articulations.begin() + art);
            art = 0; selected = -1;
            proc.setArticulation (proc.activeLayer, 0);
            changed(); refreshArticulationBox(); refreshControls();
        }), true);
    };
    importButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Pick recordings to add", File::getSpecialLocation (File::userMusicDirectory),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
                              {
                                  StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  if (! paths.isEmpty()) addFiles (paths);
                              });
    };

    zoneList.setRowHeight (22);
    addAndMakeVisible (zoneList);

    auto setupSlider = [this] (Slider& s, Label& l, const String& name, double minV, double maxV, bool isNote)
    {
        s.setSliderStyle (Slider::LinearBar);
        s.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        s.setColour (Slider::textBoxTextColourId, ui::text);
        s.setRange (minV, maxV, isNote ? 1.0 : 0.1);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 60, 18);
        s.setVelocityBasedMode (true);
        s.setVelocityModeParameters (0.6, 1, 0.0, false);
        if (isNote)
        {
            s.textFromValueFunction = [] (double v) { return sol::noteName ((int) v); };
            s.valueFromTextFunction = [] (const String& t)
            {
                int n = sol::Library::parseNoteFromName (t.trim());
                return n >= 0 ? (double) n : t.getDoubleValue();
            };
        }
        addAndMakeVisible (s);
        l.setText (name, dontSendNotification);
        l.setFont (FontOptions (10.5f));
        l.setJustificationType (Justification::centred);
        l.setColour (Label::textColourId, ui::goldDim.brighter (0.4f));
        addAndMakeVisible (l);
    };
    setupSlider (root,  rootL,  "ROOT", 0, 127, true);
    setupSlider (lo,    loL,    "LOWEST KEY", 0, 127, true);
    setupSlider (hi,    hiL,    "HIGHEST KEY", 0, 127, true);
    setupSlider (velLo, velLoL, "SOFTEST HIT", 0, 127, false);
    setupSlider (velHi, velHiL, "HARDEST HIT", 0, 127, false);
    setupSlider (gain,  gainL,  "LEVEL dB", -24, 12, false);
    velLo.setRange (0, 127, 1); velHi.setRange (0, 127, 1);
    root.setTooltip ("The note this recording was played at. On that key it plays back exactly as recorded.");
    lo.setTooltip ("Lowest key that uses this recording (repitched down from the root).");
    hi.setTooltip ("Highest key that uses this recording (repitched up from the root).");
    velLo.setTooltip ("Velocity layers: this recording plays for hits between the softest and hardest values (1-127).");

    auto edit = [this] (Slider& s, std::function<void (sol::ZoneSpec&, int)> apply)
    {
        s.onValueChange = [this, &s, apply]
        {
            if (updating) return;
            if (auto* z = currentZone()) { apply (*z, (int) std::round (s.getValue())); changed(); zoneList.repaint(); repaint(); }
        };
    };
    edit (root,  [] (sol::ZoneSpec& z, int v) { int d = v - z.root; z.root = v; z.lo = jlimit (0, 127, z.lo + d); z.hi = jlimit (0, 127, z.hi + d); });
    edit (lo,    [] (sol::ZoneSpec& z, int v) { z.lo = jmin (v, z.hi); });
    edit (hi,    [] (sol::ZoneSpec& z, int v) { z.hi = jmax (v, z.lo); });
    edit (velLo, [] (sol::ZoneSpec& z, int v) { z.velLo = jmin (v, z.velHi); });
    edit (velHi, [] (sol::ZoneSpec& z, int v) { z.velHi = jmax (v, z.velLo); });
    gain.onValueChange = [this]
    {
        if (updating) return;
        if (auto* z = currentZone()) { z->gainDb = (float) gain.getValue(); changed(); }
    };

    setRoot.setTooltip ("Play a key on your controller, then press this to make it this recording's root note.");
    setRoot.onClick = [this]
    {
        int n = proc.lastNote.load();
        if (n >= 0 && currentZone() != nullptr) root.setValue (n);   // moves the range with it
    };
    playZone.onClick = [this]
    {
        if (auto* z = currentZone())
        {
            int n = z->root;
            proc.keyboardState.noteOn (1, n, 0.8f);
            Timer::callAfterDelay (300, [this, n] { proc.keyboardState.noteOff (1, n, 0.0f); });
        }
    };
    autoRanges.setTooltip ("Spread each recording across the keys up to halfway to its neighbours.");
    autoRanges.onClick = [this]
    {
        if (auto* a = currentArt())
        {
            for (auto& z : a->zones) z.lo = z.hi = -1;
            sol::Library::fillAutoRanges (*a);
            changed(); refreshControls();
        }
    };
    removeZone.onClick = [this]
    {
        if (auto* a = currentArt(); a != nullptr && currentZone() != nullptr)
        {
            a->zones.erase (a->zones.begin() + selected);
            selected = jmin (selected, (int) a->zones.size() - 1);
            changed(); refreshControls();
        }
    };

    refreshControls();
}

// ---- data
sol::ArticulationSpec* MapView::currentArt()
{
    if (spec.articulations.empty()) return nullptr;
    art = jlimit (0, (int) spec.articulations.size() - 1, art);
    return &spec.articulations[(size_t) art];
}

sol::ZoneSpec* MapView::currentZone()
{
    auto* a = currentArt();
    if (a == nullptr || selected < 0 || selected >= (int) a->zones.size()) return nullptr;
    return &a->zones[(size_t) selected];
}

void MapView::setTarget (const sol::InstrumentInfo& newInfo, int articulation)
{
    info = newInfo;
    editable = ! info.isDemo() && info.folder.isDirectory();
    spec = sol::Library::describe (info);
    if (spec.articulations.empty() && editable) spec.articulations.push_back ({ "Strike", {} });
    for (auto& a : spec.articulations) sol::Library::fillAutoRanges (a);
    art = articulation;
    selected = (currentArt() != nullptr && ! currentArt()->zones.empty()) ? 0 : -1;
    refreshArticulationBox();
    refreshControls();
}

void MapView::changed()
{
    if (! editable) return;
    sol::Library::writeSpec (info.folder, spec);
    startTimer (700);   // reload once the edits pause
}

void MapView::timerCallback()
{
    stopTimer();
    proc.reloadLayersUsing (info.folder);
}

void MapView::addFiles (const StringArray& paths)
{
    if (! editable) return;

    // Folders dropped in: their recordings go into the current articulation, and any folders
    // inside them become new articulations named after the folder.
    Array<File> loose;
    std::vector<std::pair<String, Array<File>>> newArts;
    for (auto& p : paths)
    {
        File f (p);
        if (f.isDirectory())
        {
            loose.addArray (sol::Library::audioFilesIn (f));
            for (const auto& e : RangedDirectoryIterator (f, false, "*", File::findDirectories))
                if (auto files = sol::Library::audioFilesIn (e.getFile()); ! files.isEmpty())
                    newArts.push_back ({ e.getFile().getFileName(), files });
        }
        else if (sol::Library::isAudioFile (f))
            loose.add (f);
    }

    auto copyIn = [this] (const String& artName, const Array<File>& files)
    {
        Array<File> out;
        auto dest = info.folder.getChildFile (File::createLegalFileName (artName));
        for (auto& f : files)
        {
            if (f.isAChildOf (info.folder)) { out.add (f); continue; }
            dest.createDirectory();
            auto target = dest.getChildFile (f.getFileName());
            if (target.existsAsFile() && target.getSize() != f.getSize())
                target = dest.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false);
            if (! target.existsAsFile()) f.copyFileTo (target);
            out.add (target);
        }
        return out;
    };

    auto merge = [] (sol::ArticulationSpec& a, const sol::ArticulationSpec& added)
    {
        for (auto z : added.zones) { z.lo = z.hi = -1; a.zones.push_back (z); }
        std::stable_sort (a.zones.begin(), a.zones.end(), [] (auto& x, auto& y) { return x.root < y.root; });
        sol::Library::fillAutoRanges (a);
    };

    int added = 0;
    if (! loose.isEmpty())
    {
        auto* a = currentArt();
        std::vector<int> taken;
        for (auto& z : a->zones) taken.push_back (z.root);
        auto files = copyIn (a->name, loose);
        merge (*a, sol::Library::autoMapFiles (a->name, info.folder, files, taken));
        added += files.size();
    }
    for (auto& [name, files] : newArts)
    {
        auto copied = copyIn (name, files);
        sol::ArticulationSpec a { name, {} };
        merge (a, sol::Library::autoMapFiles (name, info.folder, copied));
        spec.articulations.push_back (a);
        added += copied.size();
    }

    proc.setStatus ("Added " + String (added) + (added == 1 ? " recording" : " recordings")
                    + ". Check the root notes - click a recording, play its note, press Root = last key.");
    changed();
    refreshArticulationBox();
    refreshControls();
}

// ---- view
void MapView::refreshArticulationBox()
{
    const ScopedValueSetter<bool> s (updating, true);
    artBox.clear (dontSendNotification);
    int id = 1;
    for (auto& a : spec.articulations) artBox.addItem (a.name, id++);
    artBox.setSelectedItemIndex (jlimit (0, jmax (0, (int) spec.articulations.size() - 1), art), dontSendNotification);
}

void MapView::selectZone (int i)
{
    selected = i;
    zoneList.selectRow (i, false, true);
    refreshControls();
}

void MapView::refreshControls()
{
    const ScopedValueSetter<bool> s (updating, true);
    auto* a = currentArt();
    auto* z = currentZone();

    // view range around the mapped keys
    viewLo = 127; viewHi = 0;
    if (a != nullptr) for (auto& zz : a->zones) { viewLo = jmin (viewLo, zz.lo); viewHi = jmax (viewHi, zz.hi); }
    if (viewLo > viewHi) { viewLo = 48; viewHi = 84; }
    viewLo = jmax (0, viewLo - 4); viewHi = jmin (127, viewHi + 4);
    while (viewHi - viewLo < 36) { viewLo = jmax (0, viewLo - 1); viewHi = jmin (127, viewHi + 1); }

    for (auto* c : std::initializer_list<Component*> { &root, &lo, &hi, &velLo, &velHi, &gain, &setRoot, &playZone, &removeZone })
        c->setEnabled (editable && z != nullptr);
    for (auto* c : std::initializer_list<Component*> { &artBox, &addArt, &importButton, &autoRanges })
        c->setEnabled (editable);
    renameArt.setEnabled (editable && a != nullptr);
    deleteArt.setEnabled (editable && spec.articulations.size() > 1);

    for (auto* c : std::initializer_list<Component*> { &root, &lo, &hi, &velLo, &velHi, &gain, &rootL, &loL, &hiL, &velLoL, &velHiL, &gainL })
        c->setVisible (z != nullptr);

    if (z != nullptr)
    {
        root.setValue (z->root, dontSendNotification);
        lo.setValue (z->lo, dontSendNotification);
        hi.setValue (z->hi, dontSendNotification);
        velLo.setValue (z->velLo, dontSendNotification);
        velHi.setValue (z->velHi, dontSendNotification);
        gain.setValue (z->gainDb, dontSendNotification);
    }
    zoneList.updateContent();
    zoneList.selectRow (selected, false, true);
    refreshLastKey();
    repaint();
}

void MapView::refreshLastKey()
{
    int n = proc.lastNote.load();
    auto t = n >= 0 ? "Root = last key (" + sol::noteName (n) + ")" : String ("Root = last key");
    if (setRoot.getButtonText() != t) setRoot.setButtonText (t);
}

int MapView::getNumRows()
{
    auto* a = currentArt();
    return a != nullptr ? (int) a->zones.size() : 0;
}

void MapView::paintListBoxItem (int row, Graphics& g, int w, int h, bool sel)
{
    auto* a = currentArt();
    if (a == nullptr || row >= (int) a->zones.size()) return;
    auto& z = a->zones[(size_t) row];
    if (sel)
    {
        g.setColour (ui::panelHi.brighter (0.05f));
        g.fillRoundedRectangle (1.0f, 1.0f, (float) w - 2, (float) h - 2, 4.0f);
    }
    g.setFont (FontOptions (12.0f));
    g.setColour (sel ? ui::gold : ui::text);
    g.drawText (sol::noteName (z.root), 8, 0, 40, h, Justification::centredLeft);
    g.setColour (sel ? ui::text : ui::textDim.brighter (0.2f));
    g.drawFittedText (File::createFileWithoutCheckingPath (z.file).getFileName(), 50, 0, w - 230, h, Justification::centredLeft, 1);
    g.setColour (ui::textDim);
    g.drawText ("keys " + sol::noteName (z.lo) + "-" + sol::noteName (z.hi), w - 175, 0, 95, h, Justification::centredLeft);
    g.drawText ("vel " + String (z.velLo) + "-" + String (z.velHi), w - 80, 0, 75, h, Justification::centredLeft);
}

void MapView::listBoxItemClicked (int row, const MouseEvent&) { selected = row; refreshControls(); }

Rectangle<int> MapView::mapArea() const
{
    return getLocalBounds().withTrimmedTop (44).withHeight (150).reduced (2, 0);
}

float MapView::xForKey (int key) const
{
    auto r = mapArea().toFloat();
    return r.getX() + r.getWidth() * (float) (key - viewLo) / (float) (viewHi - viewLo + 1);
}

int MapView::keyAtX (float x) const
{
    auto r = mapArea().toFloat();
    return viewLo + (int) std::floor ((x - r.getX()) / r.getWidth() * (float) (viewHi - viewLo + 1));
}

void MapView::mouseDown (const MouseEvent& e)
{
    auto r = mapArea();
    auto* a = currentArt();
    if (a == nullptr || ! r.contains (e.getPosition())) return;
    int key = keyAtX ((float) e.x);
    int vel = jlimit (0, 127, (int) (127.0f * (float) (r.getBottom() - 14 - e.y) / (float) (r.getHeight() - 14)));
    // prefer the smallest zone under the mouse
    int best = -1, bestSize = 1 << 30;
    for (int i = 0; i < (int) a->zones.size(); ++i)
    {
        auto& z = a->zones[(size_t) i];
        if (key >= z.lo && key <= z.hi && vel >= z.velLo && vel <= z.velHi)
        {
            int size = (z.hi - z.lo + 1) * (z.velHi - z.velLo + 1);
            if (size < bestSize) { best = i; bestSize = size; }
        }
    }
    if (best >= 0) selectZone (best);
}

void MapView::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (b, 10.0f);

    auto r = mapArea().toFloat();
    auto grid = r.withTrimmedBottom (14.0f);
    g.setColour (ui::bg);
    g.fillRoundedRectangle (r, 6.0f);

    // octave lines
    for (int k = viewLo; k <= viewHi + 1; ++k)
        if (k % 12 == 0)
        {
            g.setColour (ui::line);
            g.drawVerticalLine ((int) xForKey (k), grid.getY(), grid.getBottom());
            g.setColour (ui::textDim);
            g.setFont (FontOptions (9.0f));
            g.drawText (sol::noteName (k), (int) xForKey (k) + 2, (int) grid.getY() + 2, 30, 10, Justification::left);
        }

    auto* a = currentArt();
    if (! editable)
    {
        g.setColour (ui::textDim);
        g.setFont (FontOptions (13.0f));
        g.drawFittedText ("The built-in demo can't be edited.\nPress  + New instrument  (or drop WAV files anywhere on the plugin)\nto start mapping your own recordings.",
                          grid.toNearestInt().reduced (10), Justification::centred, 4);
    }
    else if (a == nullptr || a->zones.empty())
    {
        g.setColour (ui::textDim);
        g.setFont (FontOptions (14.0f));
        g.drawFittedText ("Drop WAV files or a folder of recordings here\n(or press Import files...)",
                          grid.toNearestInt().reduced (10), Justification::centred, 3);
    }
    else
    {
        for (int i = 0; i < (int) a->zones.size(); ++i)
        {
            auto& z = a->zones[(size_t) i];
            float x1 = xForKey (z.lo), x2 = xForKey (z.hi + 1);
            float y1 = grid.getBottom() - grid.getHeight() * (float) (z.velHi + 1) / 128.0f;
            float y2 = grid.getBottom() - grid.getHeight() * (float) z.velLo / 128.0f;
            auto zr = Rectangle<float> (x1, y1, x2 - x1, y2 - y1).reduced (0.5f);
            auto col = ui::violet.interpolatedWith (ui::gold, (float) (z.root % 12) / 11.0f);
            g.setColour (col.withAlpha (i == selected ? 0.55f : 0.22f));
            g.fillRect (zr);
            g.setColour (i == selected ? ui::gold : col.withAlpha (0.6f));
            g.drawRect (zr, i == selected ? 1.5f : 0.8f);
            g.setColour (ui::teal);
            float rx = (xForKey (z.root) + xForKey (z.root + 1)) * 0.5f;
            g.fillEllipse (Rectangle<float> (5, 5).withCentre ({ rx, zr.getBottom() - 5 }));
        }
    }

    // mini keyboard strip
    auto kb = r.removeFromBottom (12.0f);
    for (int k = viewLo; k <= viewHi; ++k)
    {
        static const bool black[] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
        auto kr = Rectangle<float> (xForKey (k), kb.getY(), xForKey (k + 1) - xForKey (k), kb.getHeight()).reduced (0.3f, 0);
        g.setColour (black[k % 12] ? Colour (0xff2a2532) : Colour (0xffbdb5a6));
        if (proc.keyboardState.isNoteOnForChannels (0xffff, k)) g.setColour (ui::gold);
        g.fillRect (kr);
    }

    g.setColour (ui::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText (editable ? "Changes save automatically to the instrument folder." : "", getLocalBounds().removeFromBottom (20).reduced (12, 0),
                Justification::centredLeft);
}

void MapView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (28);
    artBox.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    addArt.setBounds (top.removeFromLeft (104));
    top.removeFromLeft (6);
    renameArt.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (6);
    deleteArt.setBounds (top.removeFromLeft (56));
    importButton.setBounds (top.removeFromRight (110));

    r.removeFromTop (8 + 150 + 8);   // the map drawing
    r.removeFromBottom (20);         // footer note

    auto buttons = r.removeFromBottom (28);
    const int bw = (buttons.getWidth() - 18) / 4;
    setRoot.setBounds (buttons.removeFromLeft (bw + 30)); buttons.removeFromLeft (6);
    playZone.setBounds (buttons.removeFromLeft (bw - 30)); buttons.removeFromLeft (6);
    autoRanges.setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (6);
    removeZone.setBounds (buttons);
    r.removeFromBottom (6);

    auto fields = r.removeFromBottom (40);
    const int fw = fields.getWidth() / 6;
    Slider* ss[] = { &root, &lo, &hi, &velLo, &velHi, &gain };
    Label* ls[] = { &rootL, &loL, &hiL, &velLoL, &velHiL, &gainL };
    for (int i = 0; i < 6; ++i)
    {
        auto c = fields.removeFromLeft (fw).reduced (3, 0);
        ls[i]->setBounds (c.removeFromTop (14));
        ss[i]->setBounds (c);
    }
    r.removeFromBottom (6);
    zoneList.setBounds (r);
}
