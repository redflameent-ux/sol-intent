#pragma once
// The ROLL page: draw notes as lines, bend them with curved points, and give every note
// its own velocity, pressure and slide. Plays in sync with the host (or its own Play).
#include <juce_audio_utils/juce_audio_utils.h>
#include "RollStyle.h"
#include "../roll/RollEngine.h"
#include <set>

class SolIntentProcessor;

namespace sol::gui
{
class RollPage;

// ---------------------------------------------------------------------------- the drawing area
class RollCanvas : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit RollCanvas (RollPage& owner);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // view
    double viewStart = 0.0, viewBeats = 8.0;   // horizontal
    float viewLowPitch = 48.0f, viewPitches = 30.0f;   // vertical (bottom pitch, how many rows)
    void fitToPattern();

private:
    friend class RollPage;
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<float> legendArea() const;
    float xForBeat (double b) const;
    double beatForX (float x) const;
    float yForPitch (float p) const;
    float pitchForY (float y) const;
    float rowHeight() const;

    struct Hit { int lineId = -1; int node = -1; int segment = -1; };
    Hit hitTest (juce::Point<float>) const;
    juce::Path linePath (const roll::Line&, bool withWobble) const;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBars();

    RollPage& page;
    juce::ScrollBar hBar { false }, vBar { true };

    enum class Drag { None, NewLine, Node, Curve, MoveLines, Marquee, Paint, Velocity };
    Drag drag = Drag::None;
    int dragLine = -1, dragNode = -1;
    juce::Point<float> downPos;
    roll::Pattern beforeDrag;
    float downCurve = 0.0f;
    double lastPaintBeat = -1.0;
    juce::Rectangle<float> marquee;
    Hit hover;
};

// ---------------------------------------------------------------------------- the page
class RollPage : public juce::Component, private juce::Timer
{
public:
    explicit RollPage (SolIntentProcessor&);
    ~RollPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    // editing (used by the canvas)
    roll::Pattern model;
    std::set<int> selection;
    enum class Tool { Draw, Select };
    Tool tool = Tool::Draw;
    enum class Lane { Pitch, Velocity, Pressure, Slide };
    Lane lane = Lane::Pitch;
    bool snapPitch = true;
    bool showVelocity = true, showPressure = true, showSlide = true, showGlitter = true;   // which automation ribbons are drawn
    double gridBeats() const;
    double snapBeat (double b) const;
    void beginEdit();                    // remember for undo
    void publish();                      // send to the audio engine
    void audition (float pitch, float vel = 0.8f);
    void stopAudition();
    void setHint (const juce::String& h) { hint = h; repaint (statusArea); }
    roll::RollEngine& engine;
    float ringLevel (int note) const;     // how loud that note is ringing right now (0..1), like the ring display

private:
    void timerCallback() override;
    void undo();
    void redo();
    void copySelection (bool cut);
    void paste();
    void deleteSelection();
    void selectAll();
    void quantise();
    void openPattern();
    void savePattern();
    void exportMidi();
    void clearAll();
    void drainRecording();
    void refreshControls();
    void setTool (Tool t);

    SolIntentProcessor& proc;
    RollCanvas canvas { *this };

    juce::TextButton drawBtn { "Draw" }, selectBtn { "Select" }, cutBtn { "Cut" }, copyBtn { "Copy" }, pasteBtn { "Paste" },
                     undoBtn { "Undo" }, redoBtn { "Redo" }, openBtn { "Open" }, saveBtn { "Save" }, loopBtn { "Loop" },
                     quantBtn { "Quantise" }, exportBtn { "Export MIDI" }, clearBtn { "Clear" },
                     playBtn { "Play" }, stopBtn { "Stop" }, recBtn { "Rec" }, keysBtn { "Keys" }, polarityBtn { "Unipolar" },
                     snapBtn { "Snap" };
    juce::ComboBox gridBox, modShapeBox, modSpeedBox, laneBox;
    juce::Slider barsSlider, amountSlider, fadeSlider;
    juce::Label barsL, modL, amountL, speedL, fadeL, gridL, laneL;

    std::vector<roll::Pattern> undoStack, redoStack;
    std::vector<roll::Line> clipboard;
    juce::String hint;
    juce::Rectangle<int> toolbarArea, statusArea;
    std::unique_ptr<juce::FileChooser> chooser;

    struct RecLine { int lineId = -1; int note = 60; double lastNodeBeat = 0; float bend = 0; };
    std::array<RecLine, 17> recLines {};
    std::array<float, 17> recBend {};
    bool updating = false;
};

} // namespace sol::gui
