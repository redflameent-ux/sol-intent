#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ui
{
    // Palette: black star field with a faint violet nebula, amber-gold light, cyan glow, cream lettering.
    // Panels are almost solid so the stars sit around them, not on top of the controls.
    const juce::Colour bg       { 0xff05060c };
    const juce::Colour panel    { 0xf40b0e1a };
    const juce::Colour panelHi  { 0xff151a2d };
    const juce::Colour line     { 0xff262c44 };
    const juce::Colour gold     { 0xffe8a64c };   // amber gold
    const juce::Colour goldDim  { 0xff9a6a35 };
    const juce::Colour violet   { 0xffb45cff };   // magenta-violet
    const juce::Colour teal     { 0xff3fe6f2 };   // cyan glow
    const juce::Colour text     { 0xfff3e6cc };   // cream
    const juce::Colour textDim  { 0xff8a8fa8 };

    juce::String u8 (const char*);   // UTF-8 literal helper

    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }
        juce::Font getComboBoxFont (juce::ComboBox&) override        { return juce::Font (juce::FontOptions (13.0f)); }
        juce::Font getPopupMenuFont() override                         { return juce::Font (juce::FontOptions (14.0f)); }
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}

// ---------------------------------------------------------------------------- a knob with MIDI learn
class ParamKnob : public juce::Component
{
public:
    ParamKnob (SolIntentProcessor&, const juce::String& paramID, const juce::String& label, bool bipolar, bool small = false);
    void resized() override;
    void refreshLearnState();
    juce::String getParamID() const { return paramID; }

private:
    struct LearnSlider : juce::Slider
    {
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick) onRightClick();
            else Slider::mouseDown (e);
        }
    };

    SolIntentProcessor& proc;
    juce::String paramID, baseLabel;
    bool small;
    LearnSlider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// ---------------------------------------------------------------------------- ripple display
class ResonanceView : public juce::Component
{
public:
    explicit ResonanceView (SolIntentProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick() { phase += 0.035f; repaint(); }
    juce::String title, subtitle;
private:
    SolIntentProcessor& proc;
    float phase = 0.0f;
};

// ---------------------------------------------------------------------------- 4x4 pads
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (SolIntentProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setArticulation (const sol::Articulation* a, sol::InstrumentPtr keepAlive) { art = a; inst = std::move (keepAlive); repaint(); }
private:
    int padAt (juce::Point<float>) const;
    juce::Rectangle<float> padBounds (int pad) const;
    void release();
    SolIntentProcessor& proc;
    sol::InstrumentPtr inst;
    const sol::Articulation* art = nullptr;
    int heldNote = -1;
};

// ---------------------------------------------------------------------------- keyboard
class TintedKeyboard : public juce::MidiKeyboardComponent
{
public:
    TintedKeyboard (juce::MidiKeyboardState& s) : MidiKeyboardComponent (s, horizontalKeyboard) {}
    void setArticulation (const sol::Articulation* a, sol::InstrumentPtr keepAlive, int ksBase, int ksCount)
    {
        art = a; inst = std::move (keepAlive); keyswitchBase = ksBase; keyswitchCount = ksCount; repaint();
    }
protected:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour) override;
private:
    bool inRange (int n) const { return art != nullptr && n >= art->lowestKey && n <= art->highestKey; }
    bool isKeyswitch (int n) const { return keyswitchBase >= 0 && n >= keyswitchBase && n < keyswitchBase + keyswitchCount; }
    sol::InstrumentPtr inst;
    const sol::Articulation* art = nullptr;
    int keyswitchBase = -1, keyswitchCount = 0;
};

// ---------------------------------------------------------------------------- layer slot
class LayerCard : public juce::Component
{
public:
    LayerCard (SolIntentProcessor&, int layerIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onActivate;
private:
    SolIntentProcessor& proc;
    int index;
    juce::ToggleButton onButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    ParamKnob level, tune, pan;
    juce::String name, articulation;
};

// ---------------------------------------------------------------------------- MAP screen
class MapView : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit MapView (SolIntentProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void setTarget (const sol::InstrumentInfo&, int articulation);
    const sol::InstrumentInfo& getTarget() const { return info; }
    void addFiles (const juce::StringArray& paths);
    void refreshLastKey();

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    sol::ArticulationSpec* currentArt();
    sol::ZoneSpec* currentZone();
    void selectZone (int);
    void changed();                 // saves + reloads shortly after the last edit
    void refreshControls();
    void refreshArticulationBox();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    juce::Rectangle<int> mapArea() const;
    int keyAtX (float x) const;
    float xForKey (int key) const;

    SolIntentProcessor& proc;
    sol::InstrumentInfo info;
    sol::InstrumentSpec spec;
    int art = 0, selected = -1;
    bool editable = false, updating = false;
    int viewLo = 36, viewHi = 84;

    juce::ComboBox artBox;
    juce::TextButton addArt { "+ Articulation" }, renameArt { "Rename" }, deleteArt { "Delete" }, importButton { "Import files..." };
    juce::ListBox zoneList { "Zones", this };
    juce::Slider root, lo, hi, velLo, velHi, gain;
    juce::Label rootL, loL, hiL, velLoL, velHiL, gainL;
    juce::TextButton setRoot { "Root = last key" }, playZone { "Play" }, autoRanges { "Auto ranges" }, removeZone { "Remove" };
    std::unique_ptr<juce::FileChooser> chooser;
};

// ---------------------------------------------------------------------------- editor
class SolIntentEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget,
                        private juce::ListBoxModel, private juce::Timer
{
public:
    explicit SolIntentEditor (SolIntentProcessor&);
    ~SolIntentEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; repaint(); }

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;
    void refreshList();
    void refreshInstrumentViews();
    void showMap (bool);
    void chooseLibraryFolder();
    void newInstrument (std::function<void (juce::File)> then = nullptr);
    void showPresetMenu();
    void stepPreset (int delta);
    void savePresetAs();
    void rebuildArticulationButtons();
    void setActiveLayer (int);

    struct Row { bool header; juce::String label; int index; };
    std::vector<Row> rows;

    SolIntentProcessor& proc;
    ui::Look look;

    juce::TextButton libraryButton { "Library..." }, rescanButton { "Rescan" }, stopButton { "Stop all" },
                     presetButton, prevPreset { "<" }, nextPreset { ">" }, savePreset { "Save" },
                     playTab { "PLAY" }, mapTab { "MAP" }, newButton { "+ New instrument" },
                     octDown { "-" }, octUp { "+" };
    juce::ListBox browser { "Instruments", this };
    std::array<std::unique_ptr<LayerCard>, SolIntentProcessor::numLayers> cards;
    ResonanceView resonance { proc };
    PadGrid pads { proc };
    MapView mapView { proc };
    TintedKeyboard keyboard { proc.keyboardState };
    juce::Label statusLabel, padRangeLabel;
    juce::OwnedArray<juce::TextButton> artButtons;

    juce::OwnedArray<ParamKnob> knobs;
    juce::ToggleButton letRing { "Let ring" };
    juce::ComboBox lfoTarget, keyswitch;
    juce::Label lfoTargetL, keyswitchL;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> letRingAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoTargetAtt, keyswitchAtt;

    sol::InstrumentPtr shown[SolIntentProcessor::numLayers];
    int shownArt = -1, shownLayer = -1, shownKs = -2;
    juce::String shownPreset;
    bool mapMode = false, dragging = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> knobArea, leftArea, centreArea, browserArea;
    juce::Rectangle<int> sectionEnv, sectionMotion, sectionSound;
    juce::Image starField;   // painted once per size

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolIntentEditor)
};
