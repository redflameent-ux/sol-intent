// Offline checks: file-name mapping, library scanning, bit-exact playback, bends and swaps.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <map>

using namespace juce;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::cout << "  ok   " << msg << "\n"; \
                              else { std::cout << "  FAIL " << msg << "\n"; ++failures; } } while (0)

static void writeTone (const File& f, double freq, double seconds = 1.0)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream());
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0));
    os.release();
    AudioBuffer<float> b (1, (int) (48000 * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, 0.5f * (float) std::sin (MathConstants<double>::twoPi * freq * i / 48000.0) * std::exp (-i / 30000.0f));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void render (SolIntentProcessor& p, AudioBuffer<float>& out, MidiBuffer& m, int blocks, int bs, AudioBuffer<float>* capture = nullptr)
{
    int written = 0;
    for (int i = 0; i < blocks; ++i)
    {
        p.processBlock (out, m);
        m.clear();
        if (std::getenv ("SOL_DEBUG") && ! std::isfinite (out.getSample (0, 0))) { std::cout << "    NaN at render block " << i << "\n"; std::exit (3); }
        if (capture != nullptr)
            for (int c = 0; c < 2; ++c)
                capture->copyFrom (c, written, out, c, 0, bs);
        written += bs;
    }
}

static int snapshot (const File& png)
{
    sol::Library::setLibraryFolder (File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_test_lib"));
    SolIntentProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.setModulation (SolIntentProcessor::motion1, "eqHigh", 0.3f);        // show what assigned modulation looks like
    p.setModulation (SolIntentProcessor::modWheelSource, "reverbMix", 0.5f);
    p.setModulation (SolIntentProcessor::motion2, "aPan", -0.5f);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
    AudioBuffer<float> out (2, 512);
    MidiBuffer m;
    p.processBlock (out, m);
    for (int n : { 48, 55, 60, 64, 71 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
    for (int b = 0; b < 60; ++b) { p.processBlock (out, m); m.clear(); }
    p.keyboardState.noteOn (1, 64, 1.0f);
    MessageManager::getInstance()->runDispatchLoopUntil (600);
    auto save = [&] (const File& f)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        f.deleteFile();
        FileOutputStream os (f);
        PNGImageFormat().writeImageToStream (img, os);
        std::cout << "saved " << f.getFullPathName() << "\n";
    };
    save (png);

    // breathing: hold a chord (breath in), then let go (breath out)
    {
        auto runFor = [&] (double seconds)
        {
            const int frames = (int) (seconds * 30.0);
            for (int f = 0; f < frames; ++f)
            {
                for (int b = 0; b < 3; ++b) { p.processBlock (out, m); m.clear(); }
                MessageManager::getInstance()->runDispatchLoopUntil (33);
            }
        };
        p.keyboardState.allNotesOff (1);
        runFor (2.5);
        for (int n : { 48, 55, 60, 64 }) p.keyboardState.noteOn (1, n, 0.9f);
        runFor (3.5);
        save (png.getSiblingFile ("ui_breath_in.png"));
        for (int n : { 48, 55, 60, 64 }) p.keyboardState.noteOff (1, n, 0.0f);
        runFor (0.35);
        save (png.getSiblingFile ("ui_breath_out.png"));
        runFor (1.2);
        save (png.getSiblingFile ("ui_breath_out2.png"));
    }

    // second shot: an instrument with articulations
    int drum = p.findInList (File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_test_lib/Frame Drum"));
    if (drum > 0) p.selectInstrument (0, drum);
    for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
    p.processBlock (out, m);
    MessageManager::getInstance()->runDispatchLoopUntil (400);
    save (png.getSiblingFile ("ui_arts.png"));

    // third: the MAP screen on Test Bowls
    int bowlsIdx = p.findInList (File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_test_lib/Test Bowls"));
    if (bowlsIdx > 0) p.selectInstrument (0, bowlsIdx);
    for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
    p.processBlock (out, m);
    for (auto* c : ed->getChildren())
        if (auto* b = dynamic_cast<TextButton*> (c); b != nullptr && b->getButtonText() == "MAP") b->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (500);
    if (auto* lb = ed->findChildWithID ("x")) (void) lb;
    save (png.getSiblingFile ("ui_map.png"));

    // drag the first block 5 keys to the right with the mouse, then undo it
    MapView* mv = nullptr;
    for (auto* c : ed->getChildren()) if (auto* m2 = dynamic_cast<MapView*> (c)) mv = m2;
    if (mv != nullptr)
    {
        auto bowls = sol::InstrumentInfo { "Test Bowls", "My instruments",
                                           File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_test_lib/Test Bowls") };
        auto rootOf = [&bowls] (const String& file)
        {
            auto spec2 = sol::Library::describe (bowls);
            for (auto& z : spec2.articulations[0].zones) if (z.file.contains (file)) return z.root;
            return -1;
        };
        const int rootBefore = rootOf ("Bowl C3");
        auto zb = mv->getZoneBounds (0);
        auto src = Desktop::getInstance().getMainMouseSource();
        auto start = zb.getCentre();
        auto end = start.translated (mv->xForKeyPublic (60 + 5) - mv->xForKeyPublic (60), 0.0f);
        auto ev = [&] (Point<float> pos, bool drag)
        {
            return MouseEvent (src, pos, ModifierKeys (ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, mv, mv,
                               Time::getCurrentTime(), start, Time::getCurrentTime(), 1, drag);
        };
        mv->mouseDown (ev (start, false));
        for (int s2 = 1; s2 <= 10; ++s2) mv->mouseDrag (ev (start + (end - start) * ((float) s2 / 10.0f), true));
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        save (png.getSiblingFile ("ui_map_drag.png"));
        if (std::getenv ("SOL_DEBUG")) std::cout << "    zone " << zb.toString() << " start " << start.toString() << " end " << end.toString()
                                                 << " k60x " << mv->xForKeyPublic (60) << " k61x " << mv->xForKeyPublic (61) << "\n";
        mv->mouseUp (ev (end, true));
        const int rootAfter = rootOf ("Bowl C3");
        std::cout << (rootAfter == rootBefore + 5 ? "  ok   " : "  FAIL ") << "dragging a block moves it 5 keys (" << rootBefore << " -> " << rootAfter << ")\n";
        mv->undo();
        const int rootUndone = rootOf ("Bowl C3");
        std::cout << (rootUndone == rootBefore ? "  ok   " : "  FAIL ") << "Undo puts it back (" << rootUndone << ")\n";
        mv->redo();
        const int rootRedone = rootOf ("Bowl C3");
        std::cout << (rootRedone == rootBefore + 5 ? "  ok   " : "  FAIL ") << "Redo moves it again (" << rootRedone << ")\n";
        mv->undo();
    }
    for (auto* c : ed->getChildren())
        if (auto* b = dynamic_cast<TextButton*> (c); b != nullptr && b->getButtonText() == "FX") b->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (300);
    save (png.getSiblingFile ("ui_fx.png"));

    // the ROLL page with an example: a chord that fans out, swells and drifts
    {
        sol::roll::Pattern pat;
        pat.lengthBeats = 16.0;
        const float notes[] = { 48, 55, 60, 64, 67, 72 };
        const float targets[] = { 43, 53, 60, 65, 70, 79 };
        for (int i = 0; i < 6; ++i)
        {
            sol::roll::Line l;
            l.id = pat.nextId++;
            l.velocity = 0.45f + 0.09f * (float) i;
            l.nodes = { { 0.5 + i * 0.25, notes[i], 0.0f }, { 6.0, notes[i], 0.6f }, { 10.0, targets[i], -0.4f }, { 14.0, targets[i], 0.0f } };
            if (i % 2 == 0) l.pressure = { { 0.5, 0.1f }, { 6.0, 0.8f }, { 10.0, 0.3f }, { 14.0, 0.9f } };
            if (i % 3 == 1) l.slide = { { 0.5, 0.5f }, { 7.0, 0.85f }, { 12.0, 0.2f }, { 14.0, 0.5f } };
            pat.lines.push_back (l);
        }
        p.roll.setPattern (pat);
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<TextButton*> (c); b != nullptr && b->getButtonText() == "ROLL") b->triggerClick();
        p.selectInstrument (0, 0);   // the long-ringing demo bowl, so the ripples show
        for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
        p.processBlock (out, m);
        p.roll.play();
        for (int b = 0; b < (int) (48000.0 / 512.0 * 4.2); ++b) { p.processBlock (out, m); m.clear(); }   // into bar 3
        MessageManager::getInstance()->runDispatchLoopUntil (400);
        save (png.getSiblingFile ("ui_roll.png"));
    }
    return 0;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc > 2 && String (argv[1]) == "--snapshot")
        return snapshot (File (argv[2]));

    std::cout << "File-name note reading\n";
    struct { const char* name; int want; } names[] = {
        { "C3", 60 }, { "Crystal Bowl F#2", 54 }, { "Sol Intent - Tibetan Bowls Bb4", 82 },
        { "bowl_Cs3_rr2", 61 }, { "Koshi Terra 1", -1 }, { "Aqua2", -1 }, { "G3_v2", 67 },
        { "Bowls (Chromatic) A2", 57 }, { "strike rr3", -1 }, { "Eb-1", 15 } };
    for (auto& n : names)
    {
        int got = sol::Library::parseNoteFromName (n.name, 3);
        CHECK (got == n.want, String (n.name) + " -> " + String (got) + " (want " + String (n.want) + ")");
    }

    std::cout << "Library scanning\n";
    auto root = File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_test_lib");
    root.deleteRecursively();
    writeTone (root.getChildFile ("Test Bowls/Bowl C3.wav"), 261.63);
    writeTone (root.getChildFile ("Test Bowls/Bowl E3 rr1.wav"), 329.63);
    writeTone (root.getChildFile ("Test Bowls/Bowl E3 rr2.wav"), 329.63);
    writeTone (root.getChildFile ("Test Bowls/Bowl G3_v1.wav"), 392.0);
    writeTone (root.getChildFile ("Test Bowls/Bowl G3_v2.wav"), 392.0);
    writeTone (root.getChildFile ("Koshi Chimes/Koshi Terra/strike a.wav"), 500.0);
    writeTone (root.getChildFile ("Koshi Chimes/Koshi Terra/strike b.wav"), 600.0);
    root.getChildFile ("Empty Folder").createDirectory();

    auto list = sol::Library::scan (root);
    CHECK (list.size() == 3, "found demo + 2 instruments (" + String ((int) list.size()) + ")");
    int iBowls = -1, iKoshi = -1;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (list[(size_t) i].name == "Test Bowls") iBowls = i;
        if (list[(size_t) i].name == "Koshi Terra") iKoshi = i;
    }
    CHECK (iBowls > 0 && iKoshi > 0, "both test instruments listed");
    CHECK (iKoshi > 0 && list[(size_t) iKoshi].category == "Koshi Chimes", "pack folder becomes a category");

    AudioFormatManager fm; fm.registerBasicFormats();
    String err;
    auto bowls = sol::Library::load (list[(size_t) iBowls], fm, err);
    CHECK (bowls != nullptr && bowls->get (0)->zones.size() == 5, "Test Bowls loads 5 recordings");
    if (bowls != nullptr)
    {
        auto* zc = bowls->get (0)->findZone (60, 100, 0);
        CHECK (zc != nullptr && zc->rootNote == 60, "C3 key plays the C3 recording");
        auto* zd = bowls->get (0)->findZone (62, 100, 0);
        CHECK (zd != nullptr && zd->rootNote == 60, "D3 (closer to C3) repitches the C3 recording");
        auto* zds = bowls->get (0)->findZone (63, 100, 0);
        CHECK (zds != nullptr && zds->rootNote == 64, "D#3 (closer to E3) repitches the E3 recording");
        auto* r1 = bowls->get (0)->findZone (64, 100, 0); auto* r2 = bowls->get (0)->findZone (64, 100, 1);
        CHECK (r1 != r2 && r1 != nullptr && r2 != nullptr, "E3 round-robins take turns");
        auto* soft = bowls->get (0)->findZone (67, 20, 0); auto* hard = bowls->get (0)->findZone (67, 120, 0);
        CHECK (soft != nullptr && hard != nullptr && soft->fileName.contains ("v1") && hard->fileName.contains ("v2"), "G3 velocity layers split soft/hard");
        CHECK (bowls->get (0)->lowestKey == 48 && bowls->get (0)->highestKey == 79, "playable range C2..G4 (" + String (bowls->get (0)->lowestKey) + ".." + String (bowls->get (0)->highestKey) + ")");
    }
    auto koshi = sol::Library::load (list[(size_t) iKoshi], fm, err);
    CHECK (koshi != nullptr && koshi->get (0)->hasRootAt (60) && koshi->get (0)->hasRootAt (61), "unnamed one-shots go on C3, C#3");
    if (koshi != nullptr)
    {
        CHECK (koshi->get (0)->findZone (62, 100, 0) == nullptr && koshi->get (0)->findZone (59, 100, 0) == nullptr,
               "unnamed one-shots stay on their own key (no re-pitched copies on neighbours)");
        auto spread = sol::Library::autoMapFiles ("x", root.getChildFile ("Koshi Chimes/Koshi Terra"),
                                                  sol::Library::audioFilesIn (root.getChildFile ("Koshi Chimes/Koshi Terra")), {},
                                                  sol::Library::ImportMode::Spread);
        CHECK (spread.zones.size() == 2 && spread.zones[0].lo == -1, "Spread mode lets them cover nearby keys");
        auto named = sol::Library::autoMapFiles ("x", root.getChildFile ("Test Bowls"),
                                                 sol::Library::audioFilesIn (root.getChildFile ("Test Bowls")), {},
                                                 sol::Library::ImportMode::OnePerKey);
        bool allSingle = true;
        for (auto& z : named.zones) allSingle = allSingle && z.lo == z.root && z.hi == z.root;
        CHECK (allSingle, "One-key-each mode keeps even named notes on one key");
    }

    std::cout << "Playback\n";
    sol::Library::setLibraryFolder (root);
    SolIntentProcessor p;
    const int bs = 512;
    const double sr = 48000.0;
    p.prepareToPlay (sr, bs);
    p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);
    p.params.getParameter ("velSens")->setValueNotifyingHost (0.0f);
    p.loadNow (0, list[(size_t) iBowls]);

    AudioBuffer<float> out (2, bs), capture (2, bs * 40);
    MidiBuffer m;
    render (p, out, m, 2, bs);   // lets the instrument swap in
    m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
    render (p, out, m, 40, bs, &capture);

    // At its own key, dry, it should be the recording sample-for-sample.
    auto* z = bowls->get (0)->findZone (60, 100, 0);
    float maxDiff = 0.0f;
    for (int i = 0; i < bs * 40; ++i)
        { float d = std::abs (capture.getSample (0, i) - z->audio.getSample (0, i)); maxDiff = std::isfinite (d) ? jmax (maxDiff, d) : 1.0f; }
    CHECK (maxDiff < 1.0e-5f, "C3 plays back exactly as recorded (max difference " + String (maxDiff, 8) + ")");

    // Let ring: releasing the key keeps it sounding.
    m.addEvent (MidiMessage::noteOff (1, 60), 0);
    for (int b = 0; b < 10; ++b)
    {
        p.processBlock (out, m); m.clear();
        if (std::getenv ("SOL_DEBUG")) std::cout << "    block " << b << " mag " << out.getMagnitude (0, bs) << " first " << out.getSample (0, 0) << "\n";
    }
    CHECK (out.getMagnitude (0, bs) > 0.01f, "let ring keeps sounding after key up");

    // Turn let ring off: next released note fades out.
    p.params.getParameter ("aRing")->setValueNotifyingHost (0.0f);
    p.params.getParameter ("aRel")->setValueNotifyingHost (0.0f);   // shortest
    m.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 0);
    render (p, out, m, 4, bs);
    m.addEvent (MidiMessage::noteOff (1, 64), 0);
    render (p, out, m, 20, bs);
    CHECK (out.getMagnitude (0, bs) < 0.001f, "with let ring off, notes stop after release");
    p.params.getParameter ("aRing")->setValueNotifyingHost (1.0f);

    // Pitch bend: full up with 12 st range should double the frequency of C3.
    p.stopAllSound();
    render (p, out, m, 1, bs);
    m.addEvent (MidiMessage::pitchWheel (1, 16383), 0);
    m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
    capture.clear();
    render (p, out, m, 20, bs, &capture);
    int crossings = 0;
    for (int i = 2000; i < bs * 20; ++i)
        if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++crossings;
    double hz = crossings / ((bs * 20 - 2000) / sr);
    CHECK (std::abs (hz - 523.25) < 15.0, "full bend up = one octave (" + String (hz, 1) + " Hz, want ~523)");
    m.addEvent (MidiMessage::pitchWheel (1, 8192), 0);

    // Swapping instruments while notes ring must not crash.
    m.addEvent (MidiMessage::noteOn (1, 62, (uint8) 90), 0);
    render (p, out, m, 2, bs);
    p.loadNow (0, list[0]);
    m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 90), 0);
    render (p, out, m, 2, bs);
    p.selectInstrument (0, iKoshi);   // background load
    for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
    render (p, out, m, 2, bs);
    m.addEvent (MidiMessage::noteOn (1, 61, (uint8) 90), 0);
    render (p, out, m, 4, bs);
    CHECK (p.getDisplayInstrument (0) != nullptr && p.getDisplayInstrument (0)->name == "Koshi Terra", "background load swaps in Koshi Terra");
    CHECK (out.getMagnitude (0, bs) > 0.001f, "Koshi Terra plays");

    // Demo bowl with the full effects chain, saved so it can be listened to.
    p.loadNow (0, list[0]);
    p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.35f);
    p.params.getParameter ("velSens")->setValueNotifyingHost (0.7f);
    render (p, out, m, 2, bs);
    const int total = (int) (sr * 14) / bs;
    AudioBuffer<float> demo (2, total * bs);
    int notes[] = { 48, 55, 60, 64, 67, 72 };
    for (int b = 0; b < total; ++b)
    {
        for (int k = 0; k < 6; ++k)
            if (b == k * 30) m.addEvent (MidiMessage::noteOn (1, notes[k], (uint8) (70 + k * 8)), 0);
        if (b == 220) m.addEvent (MidiMessage::pitchWheel (1, 12000), 0);
        p.processBlock (out, m); m.clear();
        for (int c = 0; c < 2; ++c) demo.copyFrom (c, b * bs, out, c, 0, bs);
    }
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < demo.getNumSamples(); ++i)
            if (! std::isfinite (demo.getSample (c, i))) finite = false;
    CHECK (finite, "no NaN / inf in the output");
    CHECK (demo.getMagnitude (0, demo.getNumSamples()) < 1.0f, "no clipping (peak " + String (demo.getMagnitude (0, demo.getNumSamples()), 3) + ")");

    auto wavFile = File::getCurrentWorkingDirectory().getChildFile ("demo_render.wav");
    wavFile.deleteFile();
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (wavFile.createOutputStream());
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        os.release();
        w->writeFromAudioSampleBuffer (demo, 0, demo.getNumSamples());
    }

    // Saving / restoring with a project
    MemoryBlock state;
    p.selectInstrument (0, iBowls);
    for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
    p.getStateInformation (state);
    SolIntentProcessor p2;
    p2.setStateInformation (state.getData(), (int) state.getSize());
    for (int i = 0; i < 200 && p2.isLoading(); ++i) Thread::sleep (10);
    CHECK (p2.getDisplayInstrument (0) != nullptr && p2.getDisplayInstrument (0)->name == "Test Bowls", "project reopens with the same instrument");


    std::cout << "Articulations, layers, mapping, MIDI learn, presets\n";
    {
        // an instrument with a Swells articulation in a subfolder
        writeTone (root.getChildFile ("Frame Drum/C3.wav"), 100.0);
        writeTone (root.getChildFile ("Frame Drum/Rim/C3.wav"), 900.0);
        writeTone (root.getChildFile ("Frame Drum/Roll/C3.wav"), 300.0);
        p.rescanLibrary();
        int iDrum = p.findInList (root.getChildFile ("Frame Drum"));
        CHECK (iDrum > 0, "instrument with articulation folders is listed once (not as a pack)");
        auto spec = sol::Library::describe (p.getInstrumentList()[(size_t) iDrum]);
        CHECK (spec.articulations.size() == 3 && spec.articulations[1].name == "Rim" && spec.articulations[2].name == "Roll",
               "subfolders become articulations Main / Rim / Roll");

        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);
        p.params.getParameter ("aRing")->setValueNotifyingHost (1.0f);
        p.loadNow (0, p.getInstrumentList()[(size_t) iDrum]);
        m.addEvent (MidiMessage::pitchWheel (1, 8192), 0);
        render (p, out, m, 2, bs);
        // keyswitch C0 + 1 = C#0 (25) selects Rim
        m.addEvent (MidiMessage::noteOn (1, 25, (uint8) 100), 0);
        render (p, out, m, 1, bs);
        CHECK (p.getArticulation (0) == 1, "keyswitch C#0 picks the 2nd articulation (Rim)");
        CHECK (out.getMagnitude (0, bs) < 1.0e-6f, "keyswitch notes make no sound");
        capture.clear();
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 20, bs, &capture);
        int zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        double hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 900.0) < 20.0, "C3 now plays the Rim recording (" + String (hz, 0) + " Hz)");
        p.stopAllSound(); render (p, out, m, 2, bs);

        // MAP screen round trip: change a root, save, reload
        spec.articulations[0].zones[0].root = 62;
        sol::Library::fillAutoRanges (spec.articulations[0]);
        sol::Library::writeSpec (root.getChildFile ("Frame Drum"), spec);
        auto again = sol::Library::describe (p.getInstrumentList()[(size_t) iDrum]);
        CHECK (again.articulations.size() == 3 && again.articulations[0].zones[0].root == 62, "instrument.json saves and reads back");

        // brand-new instrument from the plugin, then files added like a drop
        auto made = sol::Library::createInstrument ("My Koshi");
        CHECK (made.getChildFile ("instrument.json").existsAsFile(), "+ New instrument makes a folder with a mapping file");
        p.rescanLibrary();
        CHECK (p.findInList (made) > 0, "new (empty) instrument shows in the library");
        String err; AudioFormatManager fm2; fm2.registerBasicFormats();
        auto empty = sol::Library::load ({ "My Koshi", "Library", made }, fm2, err);
        CHECK (empty != nullptr && empty->numArticulations() == 1, "empty instrument loads without errors");

        // layer B
        p.loadNow (0, p.getInstrumentList()[(size_t) iBowls]);
        p.loadNow (1, p.getInstrumentList()[(size_t) iKoshi]);
        p.params.getParameter ("bOn")->setValueNotifyingHost (1.0f);
        render (p, out, m, 2, bs);
        capture.clear();
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 10, bs, &capture);
        auto* zA = bowls->get (0)->findZone (60, 100, 0);
        float diff = 0;
        for (int i = 0; i < 2000; ++i) diff = jmax (diff, std::abs (capture.getSample (0, i) - zA->audio.getSample (0, i)));
        CHECK (diff > 0.01f, "with layer B on, both layers sound together");
        p.params.getParameter ("bOn")->setValueNotifyingHost (0.0f);

        // layers C and D join in too
        p.loadNow (2, p.getInstrumentList()[(size_t) iDrum]);
        p.loadNow (3, p.getInstrumentList()[0]);
        p.params.getParameter ("cOn")->setValueNotifyingHost (1.0f);
        p.params.getParameter ("dOn")->setValueNotifyingHost (1.0f);
        p.params.getParameter ("dPan")->setValueNotifyingHost (1.0f);   // hard right
        render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 4, bs);
        CHECK (out.getMagnitude (0, bs) > 0.001f && out.getMagnitude (1, bs) > out.getMagnitude (0, bs) * 0.5f, "four layers play together");
        for (auto id : { "cOn", "dOn" }) p.params.getParameter (id)->setValueNotifyingHost (0.0f);
        p.params.getParameter ("dPan")->setValueNotifyingHost (0.5f);
        p.stopAllSound(); render (p, out, m, 2, bs);
        p.stopAllSound(); render (p, out, m, 2, bs);

        // MIDI learn: CC 21 -> Space
        p.startLearning ("reverbMix");
        m.addEvent (MidiMessage::controllerEvent (1, 21, 100), 0);
        render (p, out, m, 1, bs);
        CHECK (p.getLearnedCC ("reverbMix") == 21, "MIDI learn links CC 21 to Space");
        m.addEvent (MidiMessage::controllerEvent (1, 21, 127), 0);
        render (p, out, m, 1, bs);
        CHECK (p.params.getParameter ("reverbMix")->getValue() > 0.99f, "moving CC 21 turns the Space knob");

        // presets
        p.params.getParameter ("tone")->setValueNotifyingHost (0.2f);
        CHECK (p.savePreset ("Test Preset"), "preset saves");
        p.params.getParameter ("tone")->setValueNotifyingHost (0.9f);
        auto presetFile = sol::Library::presetFolder().getChildFile ("Test Preset.solpreset");
        CHECK (p.loadPreset (presetFile) && std::abs (p.params.getParameter ("tone")->getValue() - 0.2f) < 0.01f, "preset loads the knob back");
        CHECK (p.getLearnedCC ("reverbMix") == 21, "loading a preset keeps your MIDI controller links");
        presetFile.deleteFile();

        // removing an instrument: layers using it are emptied, and an empty slot reopens empty
        p.loadNow (2, { "My Koshi", "Library", made });
        p.clearLayer (2);
        CHECK (p.getDisplayInstrument (2) != nullptr && p.getDisplayInstrument (2)->name == "Empty"
               && p.params.getParameter ("cOn")->getValue() < 0.5f, "clearing a layer empties it and switches it off");
        MemoryBlock st; p.getStateInformation (st);
        SolIntentProcessor p3; p3.setStateInformation (st.getData(), (int) st.getSize());
        for (int i = 0; i < 200 && p3.isLoading(); ++i) Thread::sleep (10);
        CHECK (p3.getLayerInfo (2).name.isEmpty(), "an empty layer stays empty when the project reopens");
       #if ! JUCE_LINUX   // the test machine has no Trash; Mac and Windows use the real Trash / Recycle Bin
        CHECK (made.moveToTrash() || ! made.exists(), "instrument folder can be moved to the Trash");
       #endif
        made.deleteRecursively();
    }

    std::cout << "Cody's list: layer zones, envelopes, macros, morph, effects, saving\n";
    {
        auto setP = [&p] (const char* id, float plain)
        {
            auto* prm = p.params.getParameter (id);
            prm->setValueNotifyingHost (p.params.getParameterRange (id).convertTo0to1 (plain));
        };
        p.stopAllSound(); render (p, out, m, 2, bs);
        p.rescanLibrary();
        auto byFolder = [&p] (const File& f) { return p.getInstrumentList()[(size_t) jmax (0, p.findInList (f))]; };
        p.loadNow (0, byFolder (root.getChildFile ("Test Bowls")));
        p.loadNow (1, byFolder (root.getChildFile ("Koshi Chimes/Koshi Terra")));
        p.setArticulation (0, 0); p.setArticulation (1, 0);
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);   // dry, so tails don't blur the checks
        setP ("bOn", 1);
        setP ("aKeyHi", 59);   // A plays below middle C
        setP ("bKeyLo", 60);   // B plays from middle C up
        render (p, out, m, 2, bs);
        if (std::getenv ("SOL_DEBUG"))
            for (int L = 0; L < 4; ++L)
                std::cout << "    layer " << L << " " << (p.getDisplayInstrument (L) ? p.getDisplayInstrument (L)->name : String ("-"))
                          << " on=" << p.params.getRawParameterValue (SolIntentProcessor::layerParam (L, "On"))->load()
                          << " lo=" << p.params.getRawParameterValue (SolIntentProcessor::layerParam (L, "KeyLo"))->load()
                          << " hi=" << p.params.getRawParameterValue (SolIntentProcessor::layerParam (L, "KeyHi"))->load()
                          << " vol=" << p.params.getRawParameterValue (SolIntentProcessor::layerParam (L, "Vol"))->load()
                          << " art=" << p.getArticulation (L) << "\n";
        // play C3 (60): only layer B (Koshi, 500 Hz on C3) should sound
        capture.clear();
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 20, bs, &capture);
        int zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        double hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 500.0) < 20.0, "key zones: middle C goes only to layer B (" + String (hz, 0) + " Hz)");
        p.stopAllSound(); render (p, out, m, 2, bs);

        // per-layer playback mode: B sustained (stops on key up), A one-shot (keeps ringing)
        setP ("bRing", 0); setP ("bRel", 0.02f);
        m.addEvent (MidiMessage::noteOn (1, 61, (uint8) 100), 0);   // B (Koshi's second chime)
        render (p, out, m, 4, bs);
        const float heldLevel = out.getMagnitude (0, bs);
        m.addEvent (MidiMessage::noteOff (1, 61), 0);
        render (p, out, m, 20, bs);
        if (std::getenv ("SOL_DEBUG")) std::cout << "    held " << heldLevel << " after " << out.getMagnitude (0, bs)
                                                 << " space " << p.params.getRawParameterValue ("reverbMix")->load() << "\n";
        CHECK (out.getMagnitude (0, bs) < 0.001f, "layer B set to sustained stops when the key is let go");
        m.addEvent (MidiMessage::noteOn (1, 55, (uint8) 100), 0);   // A
        render (p, out, m, 4, bs);
        m.addEvent (MidiMessage::noteOff (1, 55), 0);
        render (p, out, m, 20, bs);
        CHECK (out.getMagnitude (0, bs) > 0.005f, "layer A (one-shot) keeps ringing after key up at the same time");
        p.stopAllSound(); render (p, out, m, 2, bs);
        setP ("aKeyHi", 127); setP ("bKeyLo", 0); setP ("bRing", 1);

        // macros
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.1f);
        p.linkToMacro (0, "reverbMix");
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.9f);
        p.setMacroEnd (0, "reverbMix");
        p.params.getParameter ("macro1")->setValueNotifyingHost (0.0f); p.applyMacros (true);
        CHECK (std::abs (p.params.getParameter ("reverbMix")->getValue() - 0.1f) < 0.01f, "macro at 0 puts Space at its start setting");
        p.params.getParameter ("macro1")->setValueNotifyingHost (0.5f); p.applyMacros();
        CHECK (std::abs (p.params.getParameter ("reverbMix")->getValue() - 0.5f) < 0.01f, "macro halfway puts Space halfway");
        p.linkToMacro (0, "eqHigh");
        CHECK (p.getMacroLinks (0).size() == 2 && p.getMacroFor ("eqHigh") == 0, "one macro can move several knobs");
        p.setMacroName (0, "Bloom");

        // morph pad
        setP ("cOn", 0); setP ("dOn", 0);
        setP ("morphOn", 1); setP ("morphX", 0); setP ("morphY", 1);   // top-left = layer A only
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);
        render (p, out, m, 2, bs);
        capture.clear();
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 20, bs, &capture);
        zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 261.6) < 15.0, "morph dot in A's corner: only layer A heard (" + String (hz, 0) + " Hz)");
        setP ("morphX", 1);   // top-right = layer B
        render (p, out, m, 4, bs);
        capture.clear();
        render (p, out, m, 20, bs, &capture);
        zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 500.0) < 25.0, "moving the dot to B's corner fades over to layer B while it rings (" + String (hz, 0) + " Hz)");
        p.setOrbit (true); setP ("lfoDepth", 0.8f); setP ("lfoRate", 2.0f);
        float minX = 1, minY = 1;
        for (int b = 0; b < 40; ++b)
        {
            p.processBlock (out, m); m.clear();
            minX = jmin (minX, p.morphXNow.load()); minY = jmin (minY, p.morphYNow.load());
        }
        CHECK (minX < 0.8f && minY < 0.8f, "Orbit with Motion 1 moves the dot on its own, both ways");
        p.setOrbit (false); setP ("lfoDepth", 1); setP ("morphOn", 0);
        p.stopAllSound(); render (p, out, m, 2, bs);

        // effects rack
        setP ("eqLow", 6); setP ("eqHigh", -6); setP ("chorusMix", 0.6f); setP ("delayMix", 0.5f); setP ("delayTime", 200);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::allNotesOff (1), 0);
        bool ok = true; float peakFx = 0;
        for (int b = 0; b < 80; ++b)
        {
            p.processBlock (out, m); m.clear();
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { float v = out.getSample (c, i); ok = ok && std::isfinite (v); peakFx = jmax (peakFx, std::abs (v)); }
        }
        CHECK (ok && peakFx < 1.0f && peakFx > 0.001f, "EQ + chorus + echo run cleanly (peak " + String (peakFx, 3) + ")");
        setP ("eqLow", 0); setP ("eqHigh", 0); setP ("chorusMix", 0); setP ("delayMix", 0);
        p.stopAllSound(); render (p, out, m, 2, bs);

        // everything comes back when the project reopens
        setP ("bAtk", 1500); setP ("cKeyLo", 72);
        MemoryBlock st; p.getStateInformation (st);
        SolIntentProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
        for (int i = 0; i < 200 && q.isLoading(); ++i) Thread::sleep (10);
        CHECK (std::abs (q.params.getRawParameterValue ("bAtk")->load() - 1500.0f) < 1.0f
               && (int) q.params.getRawParameterValue ("cKeyLo")->load() == 72, "per-layer envelope and key zone reopen with the project");
        CHECK (q.getMacroLinks (0).size() == 2 && q.getMacroName (0) == "Bloom", "macro links and names reopen with the project");
        CHECK (q.getLearnedCC ("reverbMix") == 21, "MIDI learn reopens with the project");

        // a brand-new copy (new track) already knows the controller
        MessageManager::getInstance()->runDispatchLoopUntil (200);   // lets the plugin save its controller setup
        SolIntentProcessor fresh;
        CHECK (fresh.getLearnedCC ("reverbMix") == 21, "a new copy of the plugin remembers your controller setup");
        p.clearMacro (0);
        p.forgetLearning ("reverbMix");
    }

    std::cout << "Cody's patch list: presets keep instruments, dry start, map undo\n";
    {
        SolIntentProcessor fresh2;
        CHECK (fresh2.params.getRawParameterValue ("reverbMix")->load() < 0.0001f
               && fresh2.params.getRawParameterValue ("chorusMix")->load() < 0.0001f
               && fresh2.params.getRawParameterValue ("delayMix")->load() < 0.0001f, "a new copy starts with all effects off");

        p.rescanLibrary();
        auto byFolder = [&p] (const File& f) { return p.getInstrumentList()[(size_t) jmax (0, p.findInList (f))]; };
        p.loadNow (0, byFolder (root.getChildFile ("Test Bowls")));
        p.params.getParameter ("tone")->setValueNotifyingHost (0.3f);
        CHECK (p.savePreset ("Bowl Preset"), "save a preset with Test Bowls loaded");
        p.loadNow (0, byFolder (root.getChildFile ("Koshi Chimes/Koshi Terra")));
        p.params.getParameter ("tone")->setValueNotifyingHost (0.9f);
        auto pf = sol::Library::presetFolder().getChildFile ("Bowl Preset.solpreset");
        p.loadPreset (pf);   // default: sound only
        for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
        CHECK (p.getLayerInfo (0).name == "Koshi Terra" && std::abs (p.params.getParameter ("tone")->getValue() - 0.3f) < 0.01f,
               "preset arrows change the sound but keep the loaded instrument");
        p.loadPreset (pf, true);
        for (int i = 0; i < 200 && p.isLoading(); ++i) Thread::sleep (10);
        CHECK (p.getLayerInfo (0).name == "Test Bowls", "with 'presets also change instruments' on, the preset's instrument comes back");
        pf.deleteFile();

        // the mapping file a drag writes reads back exactly (drag and undo themselves live in the window)
        auto info = byFolder (root.getChildFile ("Test Bowls"));
        auto spec = sol::Library::describe (info);
        auto before = spec;
        spec.articulations[0].zones[0].root = 72; spec.articulations[0].zones[0].lo = 70; spec.articulations[0].zones[0].hi = 74;
        sol::Library::writeSpec (info.folder, spec);
        auto back = sol::Library::describe (info);
        CHECK (back.articulations[0].zones[0].lo == 70 && back.articulations[0].zones[0].hi == 74, "a moved block saves to the instrument");
        sol::Library::writeSpec (info.folder, before);
        auto undone = sol::Library::describe (info);
        CHECK (undone.articulations[0].zones[0].root == before.articulations[0].zones[0].root, "writing the earlier mapping back restores it (undo)");
    }

    std::cout << "Assignable modulation (Motion 1/2, mod wheel, aftertouch)\n";
    {
        auto setP = [&p] (const char* id, float plain)
        {
            p.params.getParameter (id)->setValueNotifyingHost (p.params.getParameterRange (id).convertTo0to1 (plain));
        };
        p.stopAllSound(); render (p, out, m, 2, bs);
        CHECK (p.getModulations().empty(), "nothing is modulated until you assign it");
        CHECK (! SolIntentProcessor::canModulate (p.params.getParameter ("aOn")) && ! SolIntentProcessor::canModulate (p.params.getParameter ("macro1"))
               && SolIntentProcessor::canModulate (p.params.getParameter ("tone")), "switches and macros can't be modulated; knobs can");

        // Motion 1 swinging layer A's pan: left and right channels trade places over time
        p.rescanLibrary();
        p.loadNow (0, p.getInstrumentList()[(size_t) jmax (0, p.findInList (root.getChildFile ("Test Bowls")))]);
        for (auto id : { "bOn", "cOn", "dOn" }) setP (id, 0);
        setP ("aOn", 1); setP ("reverbMix", 0); setP ("lfoRate", 4.0f); setP ("lfoDepth", 1.0f);
        p.setModulation (SolIntentProcessor::motion1, "aPan", 1.0f);
        CHECK (p.getModulations ("aPan").size() == 1, "Motion 1 assigned to A Pan");
        render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        float minRatio = 99, maxRatio = 0;
        for (int b = 0; b < 60; ++b)
        {
            p.processBlock (out, m); m.clear();
            float l = out.getRMSLevel (0, 0, bs), r = out.getRMSLevel (1, 0, bs);
            if (l > 1e-4f && r > 1e-4f) { minRatio = jmin (minRatio, l / r); maxRatio = jmax (maxRatio, l / r); }
        }
        CHECK (minRatio < 0.5f && maxRatio > 2.0f, "Motion 1 swings the sound left and right (L/R " + String (minRatio, 2) + " to " + String (maxRatio, 2) + ")");
        p.setModulation (SolIntentProcessor::motion1, "aPan", 0.0f);
        CHECK (p.getModulations ("aPan").empty(), "setting it to Off removes it");
        p.stopAllSound(); render (p, out, m, 2, bs);

        // mod wheel opening up the echo
        p.setModulation (SolIntentProcessor::modWheelSource, "delayMix", 1.0f);
        m.addEvent (MidiMessage::controllerEvent (1, 1, 0), 0);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::allNotesOff (1), 0);
        p.stopAllSound(); render (p, out, m, 1, bs);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        m.addEvent (MidiMessage::controllerEvent (1, 1, 0), 0);
        render (p, out, m, 3, bs);
        p.stopAllSound();
        auto tailPeak = [&] { float pk = 0; for (int b = 0; b < 30; ++b) { p.processBlock (out, m); m.clear(); pk = jmax (pk, out.getMagnitude (0, bs)); } return pk; };
        render (p, out, m, 1, bs);
        const float tailDry = tailPeak();
        m.addEvent (MidiMessage::controllerEvent (1, 1, 127), 0);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        render (p, out, m, 3, bs);
        p.stopAllSound();
        render (p, out, m, 1, bs);
        const float tailWet = tailPeak();
        CHECK (tailWet > tailDry + 0.001f, "mod wheel up brings in the echo it's assigned to (" + String (tailDry, 4) + " -> " + String (tailWet, 4) + ")");
        CHECK (p.params.getRawParameterValue ("delayMix")->load() < 0.001f, "the knob itself stays where you left it - modulation sits on top");

        // aftertouch and Motion 2 can be assigned too, and everything saves with the project
        p.setModulation (SolIntentProcessor::aftertouchSource, "tone", 0.5f);
        p.setModulation (SolIntentProcessor::motion2, "bVol", -0.25f);
        MemoryBlock st; p.getStateInformation (st);
        SolIntentProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
        for (int i = 0; i < 200 && q.isLoading(); ++i) Thread::sleep (10);
        auto qm = q.getModulations();
        CHECK (qm.size() == 3, "all three modulations reopen with the project (" + String ((int) qm.size()) + ")");
        bool hasNeg = false; for (auto& l : qm) if (l.paramID == "bVol" && l.depth < 0) hasNeg = true;
        CHECK (hasNeg, "negative amounts (moving a knob down) are kept");
        for (auto id : { "delayMix", "tone", "bVol" }) p.clearModulations (id);
        m.addEvent (MidiMessage::controllerEvent (1, 1, 0), 0);
        p.stopAllSound(); render (p, out, m, 2, bs);
    }

    std::cout << "ROLL page: MPE lines that bend\n";
    {
        p.stopAllSound(); render (p, out, m, 2, bs);
        writeTone (root.getChildFile ("Long Tone/C3.wav"), 261.63, 6.0);   // long enough to hear the whole bend
        p.rescanLibrary();
        p.loadNow (0, p.getInstrumentList()[(size_t) jmax (0, p.findInList (root.getChildFile ("Long Tone")))]);
        for (auto id : { "bOn", "cOn", "dOn", "morphOn" }) p.params.getParameter (id)->setValueNotifyingHost (0.0f);
        p.params.getParameter ("aOn")->setValueNotifyingHost (1.0f);
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);
        render (p, out, m, 2, bs);

        // one line: starts on C3, bends up an octave over 2 beats, then holds (beats 0..6 at 120 bpm = 3 s)
        sol::roll::Pattern pat;
        sol::roll::Line line;
        line.id = pat.nextId++;
        line.nodes = { { 0.0, 60.0f, 0.0f }, { 2.0, 72.0f, 0.0f }, { 6.0, 72.0f, 0.0f } };
        line.velocity = 0.9f;
        pat.lines.push_back (line);
        pat.lengthBeats = 8.0;
        pat.loop = false;
        p.roll.setPattern (pat);
        p.roll.stop();
        render (p, out, m, 1, bs);
        p.roll.play();

        // listen at about 1.5 s (3 beats): well after the bend has arrived
        const int blocksPerSec = (int) (sr / bs);
        render (p, out, m, blocksPerSec * 3 / 2, bs);
        capture.clear();
        render (p, out, m, 20, bs, &capture);
        int zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        double hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 523.25) < 20.0, "a drawn line bending C3 up an octave plays C4 (" + String (hz, 0) + " Hz)");
        p.roll.stop(); render (p, out, m, 2, bs);
        p.stopAllSound(); render (p, out, m, 2, bs);

        // two lines at once: each gets its own MIDI channel and only the bent one gets pitch bend
        sol::roll::Pattern two;
        sol::roll::Line flat, bent;
        flat.id = two.nextId++; flat.nodes = { { 0.0, 60.0f, 0.0f }, { 4.0, 60.0f, 0.0f } };
        bent.id = two.nextId++; bent.nodes = { { 0.0, 64.0f, 0.0f }, { 2.0, 67.0f, 0.0f }, { 4.0, 67.0f, 0.0f } };
        two.lines = { flat, bent }; two.lengthBeats = 8.0; two.loop = false;
        sol::roll::RollEngine eng;
        eng.prepare (sr);
        eng.setPattern (two);
        eng.play();
        std::map<int, int> notesOn, bendsAfterStart;
        std::map<int, bool> started;
        for (int b = 0; b < blocksPerSec * 2; ++b)
        {
            MidiBuffer mb;
            eng.process (mb, bs, nullptr, 12);
            for (const auto meta : mb)
            {
                auto msg = meta.getMessage();
                if (msg.isNoteOn()) { notesOn[msg.getChannel()] = msg.getNoteNumber(); started[msg.getChannel()] = true; }
                else if (msg.isPitchWheel() && started[msg.getChannel()]) ++bendsAfterStart[msg.getChannel()];
            }
        }
        int flatCh = -1, bentCh = -1;
        for (auto& [ch, note] : notesOn) { if (note == 60) flatCh = ch; if (note == 64) bentCh = ch; }
        CHECK (flatCh > 1 && bentCh > 1 && flatCh != bentCh, "each drawn note gets its own MIDI channel (MPE)");
        CHECK (bendsAfterStart[bentCh] > 10 && bendsAfterStart[flatCh] == 0, "only the bent note receives pitch bend ("
               + String (bendsAfterStart[bentCh]) + " vs " + String (bendsAfterStart[flatCh]) + ")");

        // pressure swells a note
        auto levelWith = [&] (float pressureValue)
        {
            sol::roll::Pattern pp;
            sol::roll::Line pl;
            pl.id = pp.nextId++;
            pl.nodes = { { 0.0, 60.0f, 0.0f }, { 4.0, 60.0f, 0.0f } };
            pl.pressure = { { 0.0, pressureValue }, { 4.0, pressureValue } };
            pp.lines.push_back (pl); pp.lengthBeats = 8.0; pp.loop = false;
            p.roll.setPattern (pp);
            p.roll.stop(); render (p, out, m, 1, bs);
            p.stopAllSound(); render (p, out, m, 1, bs);
            p.roll.play();
            render (p, out, m, 20, bs);
            float rms = 0;
            for (int b = 0; b < 10; ++b) { p.processBlock (out, m); m.clear(); rms += out.getRMSLevel (0, 0, bs); }
            p.roll.stop(); render (p, out, m, 2, bs);
            return rms;
        };
        const float soft = levelWith (0.0f), pressed = levelWith (1.0f);
        CHECK (pressed > soft * 1.6f, "pressure swells the note (" + String (soft, 3) + " -> " + String (pressed, 3) + ")");

        // a normal keyboard still uses the Bend knob, not the MPE range
        p.stopAllSound(); render (p, out, m, 2, bs);
        p.params.getParameter ("bendRange")->setValueNotifyingHost (p.params.getParameterRange ("bendRange").convertTo0to1 (2.0f));
        m.addEvent (MidiMessage::pitchWheel (1, 16383), 0);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
        render (p, out, m, 10, bs);
        capture.clear();
        render (p, out, m, 20, bs, &capture);
        zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 293.7) < 12.0, "keyboard pitch bend still follows the Bend knob (+2 st: " + String (hz, 0) + " Hz)");
        m.addEvent (MidiMessage::pitchWheel (1, 8192), 0);
        p.params.getParameter ("bendRange")->setValueNotifyingHost (p.params.getParameterRange ("bendRange").convertTo0to1 (12.0f));
        p.stopAllSound(); render (p, out, m, 2, bs);

        // the roll saves with the project, but not in presets
        p.roll.setPattern (two);
        MemoryBlock st; p.getStateInformation (st);
        SolIntentProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
        CHECK (q.roll.getPattern()->lines.size() == 2 && q.roll.getPattern()->lines[1].nodes.size() == 3, "the drawn roll reopens with the project");
        p.savePreset ("Roll Check");
        auto pf = sol::Library::presetFolder().getChildFile ("Roll Check.solpreset");
        CHECK (! pf.loadFileAsString().contains ("<Roll"), "presets don't carry the roll");
        pf.deleteFile();
        p.roll.setPattern (sol::roll::Pattern());
    }

    std::cout << "MPE MIDI export (what Logic receives)\n";
    {
        // a pattern with bends, a Pitch Mod wobble, pressure and slide - and more notes than channels,
        // so channels get reused and must be reset each time
        sol::roll::Pattern pat;
        pat.lengthBeats = 32.0; pat.loop = false;
        pat.modShape = 1; pat.modAmount = 40.0f; pat.modRateBeats = 0.5; pat.modFade = 0.25f;
        for (int i = 0; i < 18; ++i)
        {
            sol::roll::Line l;
            l.id = pat.nextId++;
            l.velocity = 0.3f + 0.03f * (float) i;
            const double st = i * 1.5;
            l.nodes = { { st, 50.0f + (float) i, 0.0f }, { st + 1.0, 50.0f + (float) i + (i % 2 ? 7.0f : -5.0f), 0.5f }, { st + 2.0, 50.0f + (float) i, 0.0f } };
            if (i % 3 == 0) l.pressure = { { st, 1.0f }, { st + 2.0, 0.6f } };   // others: none, so they must start at 0
            if (i % 4 == 0) l.slide = { { st, 0.9f }, { st + 2.0, 0.1f } };      // others: centre
            pat.lines.push_back (l);
        }
        auto seq = sol::gui::RollPage::buildMidi (pat, 120.0);

        // round trip through a real .mid file
        auto mf = File::getSpecialLocation (File::tempDirectory).getChildFile ("sol_mpe_test.mid");
        {
            MidiFile out2; out2.setTicksPerQuarterNote (960); out2.addTrack (seq);
            mf.deleteFile();
            FileOutputStream os (mf); out2.writeTo (os);
        }
        MidiFile in2;
        { FileInputStream is (mf); in2.readFrom (is); }
        const auto* track = in2.getTrack (0);
        auto musical = [] (const MidiMessageSequence& sq) { int n = 0; for (int i = 0; i < sq.getNumEvents(); ++i) if (! sq.getEventPointer (i)->message.isMetaEvent()) ++n; return n; };
        CHECK (track != nullptr && musical (*track) == musical (seq), "the .mid file reads back with every note, bend, pressure and slide event ("
               + String (musical (seq)) + ")");

        // MPE setup at the start: lower zone with 14 member channels, +/-48 st bend on each
        bool mcm = false; int bendRanges = 0;
        {
            MidiRPNDetector det;
            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                auto msg = track->getEventPointer (i)->message;
                if (! msg.isController() || msg.getTimeStamp() > 0) continue;
                if (auto rpn = det.tryParse (msg.getChannel(), msg.getControllerNumber(), msg.getControllerValue()))
                {
                    if (rpn->parameterNumber == 6 && rpn->channel == 1 && rpn->value == 14) mcm = true;
                    if (rpn->parameterNumber == 0 && rpn->channel >= 2 && rpn->value == 48) ++bendRanges;
                }
            }
        }
        CHECK (mcm, "starts with the MPE Configuration Message (master channel 1, 14 member channels)");
        CHECK (bendRanges == 14, "sets +/-48 semitone bend on all 14 member channels (" + String (bendRanges) + ")");

        // replay the file the way an MPE instrument would, and compare with the drawn lines
        struct ChanState { int note = -1; float bend = 0, press = 0, slide = 0.5f; int lineIdx = -1; };
        std::array<ChanState, 17> chs {};
        std::vector<std::pair<double, int>> starts;   // beat, line index (by order of start)
        std::vector<int> order (pat.lines.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int) i;
        std::sort (order.begin(), order.end(), [&] (int a2, int b2) { return pat.lines[(size_t) a2].start() < pat.lines[(size_t) b2].start(); });
        int nextLine = 0, notesSeen = 0, resetsOk = 0, chanReuse = 0;
        float worstPitch = 0, worstPress = 0, worstSlide = 0;
        std::array<int, 17> usedCount {};
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            auto msg = track->getEventPointer (i)->message;
            const double beat = msg.getTimeStamp() / 960.0;
            const int ch = msg.getChannel();
            if (ch < 1 || ch > 16) continue;
            auto& c = chs[(size_t) ch];
            if (msg.isPitchWheel()) c.bend = (msg.getPitchWheelValue() - 8192) / 8191.0f * 48.0f;
            else if (msg.isChannelPressure()) c.press = msg.getChannelPressureValue() / 127.0f;
            else if (msg.isController() && msg.getControllerNumber() == 74) c.slide = msg.getControllerValue() / 127.0f;
            else if (msg.isNoteOn())
            {
                c.note = msg.getNoteNumber();
                c.lineIdx = nextLine < (int) order.size() ? order[(size_t) nextLine++] : -1;
                ++notesSeen;
                if (++usedCount[(size_t) ch] > 1) ++chanReuse;
                auto& l = pat.lines[(size_t) c.lineIdx];
                // the channel must already be reset to THIS note's own state when it starts
                if (std::abs (c.press - l.pressureAt (l.start())) < 0.01f && std::abs (c.slide - l.slideAt (l.start())) < 0.01f) ++resetsOk;
            }
            else if (msg.isNoteOff()) { c.note = -1; c.lineIdx = -1; }

            // after each event, compare every sounding channel with its line
            for (int k = 2; k <= 15; ++k)
            {
                auto& s2 = chs[(size_t) k];
                if (s2.note < 0 || s2.lineIdx < 0) continue;
                auto& l = pat.lines[(size_t) s2.lineIdx];
                if (beat < l.start() + 1.0 / 192.0 || beat > l.end() - 1.0 / 192.0) continue;
                const float want = l.pitchAt (beat) + sol::roll::RollEngine::wobble (pat, l, beat);
                worstPitch = jmax (worstPitch, std::abs ((float) s2.note + s2.bend - want));
                worstPress = jmax (worstPress, std::abs (s2.press - l.pressureAt (beat)));
                worstSlide = jmax (worstSlide, std::abs (s2.slide - l.slideAt (beat)));
            }
        }
        CHECK (notesSeen == 18, "all 18 notes are in the file (" + String (notesSeen) + ")");
        CHECK (chanReuse > 0, "channels get reused when there are more notes than channels");
        CHECK (resetsOk == 18, "every note starts with its own pressure and slide - nothing left over from the last note (" + String (resetsOk) + "/18)");
        CHECK (worstPitch < 0.09f, "bends in the file match the drawn lines, wobble included (worst " + String (worstPitch, 3) + " st)");
        CHECK (worstPress < 0.06f && worstSlide < 0.06f, "pressure and slide match (worst " + String (worstPress, 3) + " / " + String (worstSlide, 3) + ")");

        // and played back into Sol Intent, the bent note sounds where it should
        p.stopAllSound(); p.roll.stop(); render (p, out, m, 2, bs);
        p.rescanLibrary();
        p.loadNow (0, p.getInstrumentList()[(size_t) jmax (0, p.findInList (root.getChildFile ("Long Tone")))]);
        for (auto id : { "bOn", "cOn", "dOn", "morphOn" }) p.params.getParameter (id)->setValueNotifyingHost (0.0f);
        render (p, out, m, 2, bs);
        sol::roll::Pattern one;
        sol::roll::Line ln; ln.id = one.nextId++;
        ln.nodes = { { 0.0, 60.0f, 0.0f }, { 2.0, 72.0f, 0.0f }, { 6.0, 72.0f, 0.0f } };
        one.lines.push_back (ln); one.lengthBeats = 8.0;
        auto seq1 = sol::gui::RollPage::buildMidi (one, 120.0);
        const double samplesPerTick = sr * 0.5 / 960.0;   // 120 bpm
        int ev = 0;
        const int total = (int) (sr * 1.5 / bs) + 20;
        capture.clear();
        for (int b = 0; b < total; ++b)
        {
            while (ev < seq1.getNumEvents())
            {
                auto msg = seq1.getEventPointer (ev)->message;
                const int at = (int) (msg.getTimeStamp() * samplesPerTick) - b * bs;
                if (at >= bs) break;
                if (! msg.isMetaEvent()) m.addEvent (msg, jmax (0, at));
                ++ev;
            }
            p.processBlock (out, m); m.clear();
            if (b >= total - 20) for (int c2 = 0; c2 < 2; ++c2) capture.copyFrom (c2, (b - (total - 20)) * bs, out, c2, 0, bs);
        }
        int zc = 0;
        for (int i = 1000; i < bs * 20; ++i) if (capture.getSample (0, i - 1) < 0.0f && capture.getSample (0, i) >= 0.0f) ++zc;
        const double hz = zc / ((bs * 20 - 1000) / sr);
        CHECK (std::abs (hz - 523.25) < 20.0, "the exported MIDI played back into Sol Intent bends C3 up to C4 (" + String (hz, 0) + " Hz)");
        m.addEvent (MidiMessage::allNotesOff (2), 0);
        p.stopAllSound(); render (p, out, m, 2, bs);
        mf.deleteFile();
    }

    std::cout << "Longer rings and wider limits\n";
    {
        auto demo = sol::Library::makeDemoBowl();
        auto* z = demo->get (0)->findZone (60, 100, 0);
        const double secs = z != nullptr ? z->audio.getNumSamples() / z->sampleRate : 0.0;
        CHECK (secs > 20.0, "the demo bowl rings for over 20 seconds (" + String (secs, 1) + " s)");
        if (z != nullptr)
        {
            const int n = z->audio.getNumSamples();
            const float midLevel = z->audio.getMagnitude (0, (int) (z->sampleRate * 8.0), (int) z->sampleRate);
            const float endLevel = z->audio.getMagnitude (0, n - 200, 200);
            CHECK (midLevel > 0.005f, "it's still clearly ringing at 8-9 seconds (" + String (midLevel, 4) + ")");
            CHECK (endLevel < midLevel * 0.02f, "and fades out to nothing instead of stopping (" + String (endLevel, 6) + ")");
        }
        auto maxOf = [&p] (const char* id) { return p.params.getParameterRange (id).end; };
        CHECK (maxOf ("aAtk") >= 20000.0f && maxOf ("aDec") >= 60000.0f && maxOf ("aRel") >= 60.0f,
               "Attack reaches 20 s, Decay 60 s, Release 60 s");

        // a long one-shot plays all the way through (let ring), with no click at the very end
        {   // a steady 30-second tone (no fade), so anything cutting it short would show
            auto f = root.getChildFile ("Long Gong/C3.wav");
            f.getParentDirectory().createDirectory(); f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> os (f.createOutputStream());
            std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0));
            os.release();
            AudioBuffer<float> tone (1, 48000 * 30);
            for (int i = 0; i < tone.getNumSamples(); ++i) tone.setSample (0, i, 0.3f * (float) std::sin (MathConstants<double>::twoPi * 110.0 * i / 48000.0));
            w->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
        }
        p.rescanLibrary();
        p.loadNow (0, p.getInstrumentList()[(size_t) jmax (0, p.findInList (root.getChildFile ("Long Gong")))]);
        p.params.getParameter ("aRing")->setValueNotifyingHost (1.0f);
        p.params.getParameter ("reverbMix")->setValueNotifyingHost (0.0f);
        p.stopAllSound(); render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        render (p, out, m, 2, bs);
        m.addEvent (MidiMessage::noteOff (1, 60), 0);
        const int blocks25s = (int) (sr * 25.0 / bs);
        render (p, out, m, blocks25s, bs);
        bool stillThere = false;
        for (int i = 0; i < bs; ++i) if (std::abs (out.getSample (0, i)) > 1.0e-6f) stillThere = true;
        CHECK (stillThere, "a 30-second recording is still playing 25 seconds after the key was let go");
        p.stopAllSound(); render (p, out, m, 2, bs);
    }

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
