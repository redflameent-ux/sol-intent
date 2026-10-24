// Offline checks: file-name mapping, library scanning, bit-exact playback, bends and swaps.
#include "../Source/PluginProcessor.h"
#include <iostream>

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
    p.params.getParameter ("letRing")->setValueNotifyingHost (0.0f);
    p.params.getParameter ("release")->setValueNotifyingHost (0.0f);   // shortest
    m.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 0);
    render (p, out, m, 4, bs);
    m.addEvent (MidiMessage::noteOff (1, 64), 0);
    render (p, out, m, 20, bs);
    CHECK (out.getMagnitude (0, bs) < 0.001f, "with let ring off, notes stop after release");
    p.params.getParameter ("letRing")->setValueNotifyingHost (1.0f);

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
        p.params.getParameter ("letRing")->setValueNotifyingHost (1.0f);
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
        made.deleteRecursively();
    }

    std::cout << (failures == 0 ? "\nALL PASSED\n" : "\nFAILURES: " + std::to_string (failures) + "\n");
    return failures == 0 ? 0 : 1;
}
