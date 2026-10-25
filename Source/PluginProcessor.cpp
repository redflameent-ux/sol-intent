#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr int voicesPerLayer = 48;   // bowls ring a long time, so plenty of voices
static constexpr int chunk = 32;           // modulation updates every 32 samples

SolIntentProcessor::SolIntentProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "SolIntent", createLayout())
{
    formats.registerBasicFormats();

    auto g = [this] (const char* id) { return params.getRawParameterValue (id); };
    P.volume = g ("volume"); P.tune = g ("tune"); P.fine = g ("fine");
    P.velSens = g ("velSens"); P.bend = g ("bendRange");
    P.eqLow = g ("eqLow"); P.eqMid = g ("eqMid"); P.eqHigh = g ("eqHigh");
    P.chorusMix = g ("chorusMix"); P.chorusDepth = g ("chorusDepth"); P.chorusRate = g ("chorusRate");
    P.delayTime = g ("delayTime"); P.delayFb = g ("delayFeedback"); P.delayMix = g ("delayMix");
    P.morphOn = g ("morphOn"); P.morphX = g ("morphX"); P.morphY = g ("morphY");
    for (int m = 0; m < numMacros; ++m)
    {
        P.macro[m] = params.getRawParameterValue ("macro" + juce::String (m + 1));
        macroNames[(size_t) m] = "Macro " + juce::String (m + 1);
    }
    P.tone = g ("tone"); P.width = g ("width"); P.revMix = g ("reverbMix"); P.revSize = g ("reverbSize");
    P.lfoRate = g ("lfoRate"); P.lfoDepth = g ("lfoDepth"); P.lfoRate2 = g ("lfoRate2"); P.lfoDepth2 = g ("lfoDepth2");
    P.keyswitch = g ("keyswitch");
    for (int i = 0; i < numLayers; ++i)
    {
        P.on[i]  = params.getRawParameterValue (layerParam (i, "On"));
        P.vol[i] = params.getRawParameterValue (layerParam (i, "Vol"));
        P.tun[i] = params.getRawParameterValue (layerParam (i, "Tune"));
        P.pan[i] = params.getRawParameterValue (layerParam (i, "Pan"));
        P.atk[i] = params.getRawParameterValue (layerParam (i, "Atk"));
        P.dec[i] = params.getRawParameterValue (layerParam (i, "Dec"));
        P.sus[i] = params.getRawParameterValue (layerParam (i, "Sus"));
        P.rel[i] = params.getRawParameterValue (layerParam (i, "Rel"));
        P.ring[i] = params.getRawParameterValue (layerParam (i, "Ring"));
        P.keyLo[i] = params.getRawParameterValue (layerParam (i, "KeyLo"));
        P.keyHi[i] = params.getRawParameterValue (layerParam (i, "KeyHi"));
    }

    for (auto& l : layers)
    {
        for (int i = 0; i < voicesPerLayer; ++i)
            l.synth.addVoice (new sol::SamplerVoice (l.settings));
        l.synth.addSound (new sol::AnySound());
        l.synth.setMinimumRenderingSubdivisionSize (chunk, false);
    }

    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            learnable.push_back (r);
    for (auto& c : ccToParam) c = -1;
    for (auto& glowValue : noteGlow) glowValue = 0.0f;
    loadDefaultMidiMap();   // your controller setup carries over to new copies of the plugin

    rescanLibrary();
    queueLoad (0, instrumentList.front());   // the built-in demo until a project says otherwise
    startTimerHz (30);
}

SolIntentProcessor::~SolIntentProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout SolIntentProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; });
    auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
    auto ms  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                   { return v < 1000.0f ? String (roundToInt (v)) + " ms" : String (v / 1000.0f, 2) + " s"; });
    auto sec = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " s"; });
    auto panTxt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                   { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R"); });
    auto st = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; });
    auto note = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return sol::noteName (v); })
                                             .withValueFromStringFunction ([] (const String& t) { int n = sol::Library::parseNoteFromName (t.trim()); return n >= 0 ? n : t.getIntValue(); });
    auto add = [&l] (auto p) { l.add (std::move (p)); };

    // sound
    add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterInt>   (ParameterID { "tune", 1 }, "Tune", -24, 24, 0, st));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "fine", 1 }, "Fine", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ct"; })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "velSens", 1 }, "Touch", NormalisableRange<float> (0.0f, 1.0f), 0.7f, pct));
    add (std::make_unique<AudioParameterInt>   (ParameterID { "bendRange", 1 }, "Bend", 0, 24, 12, st));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMix", 1 }, "Space", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));   // starts dry
    add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbSize", 1 }, "Room", NormalisableRange<float> (0.0f, 1.0f), 0.8f, pct));

    // effects rack
    add (std::make_unique<AudioParameterFloat> (ParameterID { "eqLow", 1 }, "EQ Low", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "eqMid", 1 }, "EQ Mid", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "eqHigh", 1 }, "EQ High", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "chorusMix", 1 }, "Chorus", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "chorusDepth", 1 }, "Chorus Depth", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "chorusRate", 1 }, "Chorus Rate", NormalisableRange<float> (0.05f, 3.0f, 0.01f, 0.5f), 0.4f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "delayTime", 1 }, "Echo Time", NormalisableRange<float> (40.0f, 2500.0f, 1.0f, 0.5f), 450.0f, ms));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "delayFeedback", 1 }, "Echo Repeats", NormalisableRange<float> (0.0f, 0.92f), 0.35f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "delayMix", 1 }, "Echo", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));

    // morph pad: blends the four layers (A top-left, B top-right, C bottom-left, D bottom-right)
    add (std::make_unique<AudioParameterBool>  (ParameterID { "morphOn", 1 }, "Morph On", false));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "morphX", 1 }, "Morph X", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "morphY", 1 }, "Morph Y", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));

    // macros
    for (int m = 1; m <= numMacros; ++m)
        add (std::make_unique<AudioParameterFloat> (ParameterID { "macro" + String (m), 1 }, "Macro " + String (m), NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));

    // motion
    // two slow waves; what they move is set by right-clicking knobs ("Modulate with")
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; });
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 1 }, "Motion 1 Rate", NormalisableRange<float> (0.02f, 12.0f, 0.01f, 0.35f), 0.25f, hz));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth", 1 }, "Motion 1 Amount", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate2", 1 }, "Motion 2 Rate", NormalisableRange<float> (0.02f, 12.0f, 0.01f, 0.35f), 0.07f, hz));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth2", 1 }, "Motion 2 Amount", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    add (std::make_unique<AudioParameterChoice> (ParameterID { "keyswitch", 1 }, "Keyswitch", StringArray { "Off", "C-1", "C0", "C1", "C6" }, 2));

    // layers
    // layers A-D (ids aOn, aVol ... dPan)
    for (int i = 0; i < numLayers; ++i)
    {
        auto L = layerLetter (i);
        add (std::make_unique<AudioParameterBool>  (ParameterID { layerParam (i, "On"), 1 }, "Layer " + L + " On", i == 0));
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Vol"), 1 }, L + " Level", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f, db));
        add (std::make_unique<AudioParameterInt>   (ParameterID { layerParam (i, "Tune"), 1 }, L + " Tune", -24, 24, 0, st));
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Pan"), 1 }, L + " Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, panTxt));
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Atk"), 1 }, L + " Attack", NormalisableRange<float> (0.0f, 20000.0f, 1.0f, 0.3f), 0.0f, ms));     // up to 20 s swell
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Dec"), 1 }, L + " Decay", NormalisableRange<float> (0.0f, 60000.0f, 1.0f, 0.3f), 500.0f, ms));   // up to 60 s
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Sus"), 1 }, L + " Sustain", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
        add (std::make_unique<AudioParameterFloat> (ParameterID { layerParam (i, "Rel"), 1 }, L + " Release", NormalisableRange<float> (0.02f, 60.0f, 0.01f, 0.3f), 3.0f, sec));     // up to 60 s
        add (std::make_unique<AudioParameterBool>  (ParameterID { layerParam (i, "Ring"), 1 }, L + " Let Ring", true));
        add (std::make_unique<AudioParameterInt>   (ParameterID { layerParam (i, "KeyLo"), 1 }, L + " Lowest Key", 0, 127, 0, note));
        add (std::make_unique<AudioParameterInt>   (ParameterID { layerParam (i, "KeyHi"), 1 }, L + " Highest Key", 0, 127, 127, note));
    }
    return l;
}

bool SolIntentProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SolIntentProcessor::prepareToPlay (double sampleRate, int block)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& l : layers)
    {
        l.synth.setCurrentPlaybackSampleRate (sampleRate);
        l.buffer.setSize (2, juce::jmax (block, 512));
    }
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    eq.prepare (rate);
    roll.prepare (rate);
    lfoPhase = lfoPhase2 = 0.0f;
    chorus.prepare (rate);
    echo.prepare (rate);
    toneL.reset(); toneR.reset();
    lastTone = 99.0f;
    limGain = 1.0f;
    lastMasterGain = juce::Decibels::decibelsToGain (P.volume->load());
    for (int i = 0; i < numLayers; ++i)
        layers[(size_t) i].gainL = layers[(size_t) i].gainR = juce::Decibels::decibelsToGain (P.vol[i]->load());
}

int SolIntentProcessor::getKeyswitchBase() const
{
    static const int bases[] = { -1, 12, 24, 36, 96 };
    auto idx = juce::jlimit (0, 4, (int) P.keyswitch->load());
    return bases[idx];
}

// ------------------------------------------------------------------------------ MIDI in
void SolIntentProcessor::handleMidi (juce::MidiBuffer& in, juce::MidiBuffer& out)
{
    out.clear();

    // keyswitches only exist while a playing layer actually has more than one articulation
    int maxArts = 0;
    for (int i = 0; i < numLayers; ++i)
        if (auto* inst = layers[(size_t) i].current.get(); inst != nullptr && P.on[i]->load() > 0.5f)
            maxArts = juce::jmax (maxArts, inst->numArticulations());
    const int ksBase = maxArts > 1 ? getKeyswitchBase() : -1;

    for (const auto meta : in)
    {
        auto m = meta.getMessage();

        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 1) modWheel = m.getControllerValue() / 127.0f;   // a source you can assign (Modulate with > Mod wheel)

            if (int learn = learningIndex.load(); learn >= 0 && cc != 64)   // sustain pedal can't be learned
            {
                for (auto& c : ccToParam) if (c.load() == learn) c = -1;
                ccToParam[(size_t) cc] = learn;
                learningIndex = -1;
                midiMapChanged = true;
            }
            if (int idx = ccToParam[(size_t) cc].load(); idx >= 0 && idx < (int) learnable.size())
            {
                learnable[(size_t) idx]->setValueNotifyingHost (m.getControllerValue() / 127.0f);
                continue;   // a learned knob, not a sound control
            }
        }

        // the Aftertouch mod source follows a normal keyboard (channel 1); pressure on
        // MPE channels 2-16 shapes each note on its own instead
        if (m.isChannelPressure() && m.getChannel() == 1) aftertouch = m.getChannelPressureValue() / 127.0f;
        if (m.isAftertouch() && m.getChannel() == 1)      aftertouch = m.getAfterTouchValue() / 127.0f;

        if (ksBase >= 0 && (m.isNoteOn() || m.isNoteOff()) && m.getNoteNumber() >= ksBase && m.getNoteNumber() < ksBase + maxArts)
        {
            if (m.isNoteOn())
            {
                const int art = m.getNoteNumber() - ksBase;
                for (auto& l : layers)
                    if (l.current != nullptr && art < l.current->numArticulations())
                        l.articulation = art;
            }
            continue;
        }

        if (m.isNoteOn()) lastNote = m.getNoteNumber();
        out.addEvent (m, meta.samplePosition);
    }
}

// ------------------------------------------------------------------------------ audio
void SolIntentProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const double sr = rate;
    buffer.clear();

    // new instruments ready? silence the old one and swap in
    for (auto& l : layers)
        if (l.hasPending.load() && l.swapLock.tryEnter())
        {
            l.synth.allNotesOff (0, false);
            if (l.current != nullptr) l.retired.push_back (std::move (l.current));
            l.current = std::move (l.pending);
            l.pending = nullptr;
            l.hasPending = false;
            l.swapLock.exit();
        }

    if (panicRequested.exchange (false))
    {
        for (auto& l : layers) l.synth.allNotesOff (0, false);
        reverb.reset();
    }

    roll.process (midi, n, getPlayHead(), (int) P.bend->load());   // the ROLL page's notes, in time with the host
    keyboardState.processNextMidiBuffer (midi, 0, n, true);   // pads and on-screen keys join the incoming MIDI
    handleMidi (midi, filtered);

    // pick up modulation changes from the screen
    if (modsChanged.load() && modSwap.tryEnter())
    {
        activeMods = pendingMods;
        numActiveMods = numPendingMods;
        modsChanged = false;
        modSwap.exit();
    }
    updateModSources (0);   // where the waves are at the start of this block

    // per-layer settings that don't change within the block
    bool on[numLayers];
    for (int i = 0; i < numLayers; ++i)
    {
        auto& l = layers[(size_t) i];
        auto& s = l.settings;
        on[i] = P.on[i]->load() > 0.5f;
        if (! on[i] && l.wasOn) l.synth.allNotesOff (0, true);
        l.wasOn = on[i];
        s.instrument = l.current.get();
        s.articulation = l.articulation.load();
        s.attackSec  = mod (P.atk[i]) / 1000.0f;
        s.decaySec   = mod (P.dec[i]) / 1000.0f;
        s.sustain    = mod (P.sus[i]);
        s.releaseSec = mod (P.rel[i]);
        s.letRing    = P.ring[i]->load() > 0.5f;

        // this layer only hears notes inside its key zone (note-offs always pass, so nothing hangs)
        const int lo = (int) P.keyLo[i]->load(), hi = (int) P.keyHi[i]->load();
        l.midi.clear();
        for (const auto meta : filtered)
        {
            auto m = meta.getMessage();
            if (m.isNoteOn() && (m.getNoteNumber() < juce::jmin (lo, hi) || m.getNoteNumber() > juce::jmax (lo, hi)))
                continue;
            l.midi.addEvent (m, meta.samplePosition);
        }
        s.velSens    = mod (P.velSens);
        s.bendRange  = mod (P.bend);
        if (l.buffer.getNumSamples() < n) l.buffer.setSize (2, n, false, false, true);
        l.buffer.clear (0, n);
    }

    const bool stereo = buffer.getNumChannels() > 1;
    auto* L = buffer.getWritePointer (0);
    auto* R = stereo ? buffer.getWritePointer (1) : nullptr;

    const bool morphOn = P.morphOn->load() > 0.5f;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        updateModSources (len);   // move the waves on, so modulation follows them through the block
        const float baseTune = mod (P.tune) + mod (P.fine) / 100.0f;

        // morph pad: equal-power blend of the four corners
        float morphGain[numLayers] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (morphOn)
        {
            float x = mod (P.morphX), y = mod (P.morphY);
            x = juce::jlimit (0.0f, 1.0f, x); y = juce::jlimit (0.0f, 1.0f, y);
            morphXNow = x; morphYNow = y;
            const float w[numLayers] = { (1 - x) * y, x * y, (1 - x) * (1 - y), x * (1 - y) };   // y = 1 is the top
            for (int i = 0; i < numLayers; ++i) morphGain[i] = std::sqrt (w[i]);
        }
        else
        {
            morphXNow = mod (P.morphX); morphYNow = mod (P.morphY);
        }

        for (int i = 0; i < numLayers; ++i)
        {
            auto& l = layers[(size_t) i];
            l.settings.tuneSemis = baseTune + mod (P.tun[i]);
            if (on[i])
                l.synth.renderNextBlock (l.buffer, l.midi, start, len);

            // layer level and pan (constant power), LFO pan / volume on top
            float pan = juce::jlimit (-1.0f, 1.0f, mod (P.pan[i]));
            float g = juce::Decibels::decibelsToGain (mod (P.vol[i])) * morphGain[i];
            float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            float gl = g * std::cos (a) * juce::MathConstants<float>::sqrt2, gr = g * std::sin (a) * juce::MathConstants<float>::sqrt2;
            if (std::abs (pan) < 1.0e-4f) gl = gr = g;   // centred = untouched

            const float* bl = l.buffer.getReadPointer (0, start);
            const float* br = l.buffer.getReadPointer (1, start);
            for (int k = 0; k < len; ++k)
            {
                float t = (float) (k + 1) / (float) len;
                float cl = l.gainL + (gl - l.gainL) * t, cr = l.gainR + (gr - l.gainR) * t;
                if (R != nullptr) { L[start + k] += bl[k] * cl; R[start + k] += br[k] * cr; }
                else              { L[start + k] += 0.5f * (bl[k] * cl + br[k] * cr); }
            }
            l.gainL = gl; l.gainR = gr;
        }

        // tone (with optional LFO sweep)
        float t = juce::jlimit (-1.0f, 1.0f, mod (P.tone));
        if (std::abs (t - lastTone) > 0.002f) { toneL.set (t, sr); toneR.set (t, sr); lastTone = t; }
        for (int k = start; k < start + len; ++k)
        {
            L[k] = toneL.process (L[k]);
            if (R != nullptr) R[k] = toneR.process (R[k]);
        }
    }

    // effects rack: EQ -> chorus -> echo, then width and reverb below
    eq.process (L, R, n, mod (P.eqLow), mod (P.eqMid), mod (P.eqHigh));
    chorus.process (L, R, n, mod (P.chorusMix), mod (P.chorusDepth), mod (P.chorusRate));
    echo.process (L, R, n, mod (P.delayTime), mod (P.delayFb), mod (P.delayMix));

    if (R != nullptr)   // stereo width
    {
        const float width = mod (P.width);
        if (std::abs (width - 1.0f) > 1.0e-4f)
            for (int i = 0; i < n; ++i)
            {
                float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * width;
                L[i] = m + s; R[i] = m - s;
            }
    }

    juce::Reverb::Parameters rp;
    const float mix = mod (P.revMix);
    rp.roomSize = 0.5f + 0.49f * mod (P.revSize);
    rp.damping  = 0.35f;
    rp.wetLevel = mix * 0.5f;                          // (JUCE's reverb doubles the dry level internally,
    rp.dryLevel = 0.5f * (1.0f - mix * 0.45f);         //  so 0.5 = untouched)
    rp.width    = 1.0f;
    reverb.setParameters (rp);
    if (mix > 0.0001f)   // fully dry = the recording exactly as it was
    {
        if (R != nullptr) reverb.processStereo (L, R, n);
        else              reverb.processMono (L, n);
    }

    // master volume, then a safety limiter just under full scale. Below the ceiling it does
    // nothing; it only catches big stacks of bowls ringing together.
    const float master = juce::Decibels::decibelsToGain (mod (P.volume));
    const float ceiling = 0.944f;   // -0.5 dB
    const float limRelease = std::exp (-1.0f / (0.25f * (float) sr));
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float g = lastMasterGain + (master - lastMasterGain) * (float) (i + 1) / (float) n;
        float l = L[i] * g, r = R != nullptr ? R[i] * g : 0.0f;
        float pk = juce::jmax (std::abs (l), std::abs (r));
        limGain = juce::jmin (pk > ceiling ? ceiling / pk : 1.0f, 1.0f - (1.0f - limGain) * limRelease);
        L[i] = l * limGain;
        if (R != nullptr) R[i] = r * limGain;
        peak = juce::jmax (peak, pk * limGain);
    }
    lastMasterGain = master;
    outputLevel = juce::jmax (peak, outputLevel.load() * 0.85f);

    // for the ripple display and the pads: how loud each note is still ringing
    float glow[128] = {};
    for (auto& l : layers)
        for (int v = 0; v < l.synth.getNumVoices(); ++v)
            if (auto* voice = dynamic_cast<sol::SamplerVoice*> (l.synth.getVoice (v)); voice != nullptr && voice->isVoiceActive())
            {
                auto note = voice->getCurrentlyPlayingNote();
                if (note >= 0 && note < 128) glow[note] = juce::jmax (glow[note], voice->getLevel());
            }
    for (int k = 0; k < 128; ++k)
        noteGlow[(size_t) k] = glow[k];
}

// ------------------------------------------------------------------------------ loading
void SolIntentProcessor::rescanLibrary()
{
    auto root = sol::Library::getLibraryFolder();
    sol::Library::ensureLibraryExists (root);
    instrumentList = sol::Library::scan (root);
}

int SolIntentProcessor::findInList (const juce::File& folder) const
{
    for (int i = 0; i < (int) instrumentList.size(); ++i)
        if (instrumentList[(size_t) i].folder == folder)
            return i;
    return -1;
}

void SolIntentProcessor::selectInstrument (int layer, int index)
{
    if (index >= 0 && index < (int) instrumentList.size())
        queueLoad (layer, instrumentList[(size_t) index]);
}

void SolIntentProcessor::loadFolderIntoLayer (int layer, const juce::File& folder)
{
    rescanLibrary();
    if (int i = findInList (folder); i >= 0) selectInstrument (layer, i);
    else queueLoad (layer, { folder.getFileName(), "Library", folder });
}

void SolIntentProcessor::reloadLayersUsing (const juce::File& folder)
{
    for (int i = 0; i < numLayers; ++i)
        if (layers[(size_t) i].info.folder == folder)
            queueLoad (i, layers[(size_t) i].info);
}

void SolIntentProcessor::clearLayer (int layerIndex)
{
    auto& l = layers[(size_t) layerIndex];
    l.info = {};
    l.articulation = 0;
    auto empty = std::make_shared<sol::Instrument>();
    empty->name = "Empty";
    hand (layerIndex, empty, {}, ++l.generation);
    params.getParameter (layerParam (layerIndex, "On"))->setValueNotifyingHost (0.0f);
}

sol::InstrumentInfo SolIntentProcessor::getLayerInfo (int layer) const { return layers[(size_t) layer].info; }

void SolIntentProcessor::queueLoad (int layerIndex, const sol::InstrumentInfo& info)
{
    auto& l = layers[(size_t) layerIndex];
    if (l.info.folder != info.folder || l.info.name != info.name)
        l.articulation = 0;
    l.info = info;
    const int gen = ++l.generation;
    ++loadsInFlight;
    setStatus ("Loading " + info.name + "...");

    loader.addJob ([this, layerIndex, info, gen]
    {
        juce::String err;
        auto inst = sol::Library::load (info, formats, err);
        hand (layerIndex, inst, err, gen);
        --loadsInFlight;
    });
}

void SolIntentProcessor::loadNow (int layerIndex, const sol::InstrumentInfo& info)
{
    auto& l = layers[(size_t) layerIndex];
    l.info = info;
    juce::String err;
    hand (layerIndex, sol::Library::load (info, formats, err), err, ++l.generation);
}

void SolIntentProcessor::hand (int layerIndex, sol::InstrumentPtr inst, const juce::String& err, int gen)
{
    auto& l = layers[(size_t) layerIndex];
    if (gen != l.generation.load())   // something newer was picked meanwhile
        return;

    std::lock_guard<std::mutex> g (displayMutex);
    if (inst == nullptr)
    {
        status = "Couldn't load that instrument. " + err;
        return;
    }
    {
        const juce::SpinLock::ScopedLockType sl (l.swapLock);
        l.pending = inst;
        l.hasPending = true;
    }
    l.display = inst;
    status = err.isEmpty() ? juce::String() : ("Some files were skipped: " + err.replace ("\n", " "));
}

sol::InstrumentPtr SolIntentProcessor::getDisplayInstrument (int layer)
{
    std::lock_guard<std::mutex> g (displayMutex);
    return layers[(size_t) layer].display;
}

juce::String SolIntentProcessor::getStatus()
{
    std::lock_guard<std::mutex> g (displayMutex);
    return status;
}

void SolIntentProcessor::setStatus (const juce::String& s)
{
    std::lock_guard<std::mutex> g (displayMutex);
    status = s;
}

void SolIntentProcessor::timerCallback()
{
    // free instruments the audio thread has finished with, here rather than on the audio thread
    for (auto& l : layers)
    {
        std::vector<sol::InstrumentPtr> toFree;
        const juce::SpinLock::ScopedLockType sl (l.swapLock);
        toFree.swap (l.retired);
    }

    applyMacros();
    if (midiMapChanged.exchange (false))
        saveDefaultMidiMap();
}

// ------------------------------------------------------------------------------ modulation
juce::String SolIntentProcessor::modSourceName (int s)
{
    static const char* names[] = { "Motion 1", "Motion 2", "Mod wheel", "Aftertouch" };
    return names[juce::jlimit (0, (int) numModSources - 1, s)];
}

bool SolIntentProcessor::canModulate (const juce::AudioProcessorParameter* p)
{
    auto* r = dynamic_cast<const juce::RangedAudioParameter*> (p);
    if (r == nullptr || dynamic_cast<const juce::AudioParameterBool*> (p) != nullptr || dynamic_cast<const juce::AudioParameterChoice*> (p) != nullptr)
        return false;
    auto id = r->getParameterID();
    return ! id.startsWith ("macro") && ! id.endsWith ("KeyLo") && ! id.endsWith ("KeyHi");
}

void SolIntentProcessor::setModulation (int source, const juce::String& id, float depth)
{
    {
        std::lock_guard<std::mutex> g (modMutex);
        modLinks.erase (std::remove_if (modLinks.begin(), modLinks.end(),
                                        [&] (auto& l) { return l.source == source && l.paramID == id && ! l.quarter; }), modLinks.end());
        if (std::abs (depth) > 0.0001f && canModulate (params.getParameter (id)))
            modLinks.push_back ({ source, id, juce::jlimit (-1.0f, 1.0f, depth), false });
    }
    publishMods();
}

std::vector<SolIntentProcessor::ModLink> SolIntentProcessor::getModulations (const juce::String& id) const
{
    std::lock_guard<std::mutex> g (modMutex);
    std::vector<ModLink> out;
    for (auto& l : modLinks) if (id.isEmpty() || l.paramID == id) out.push_back (l);
    return out;
}

void SolIntentProcessor::clearModulations (const juce::String& id)
{
    {
        std::lock_guard<std::mutex> g (modMutex);
        modLinks.erase (std::remove_if (modLinks.begin(), modLinks.end(), [&] (auto& l) { return l.paramID == id; }), modLinks.end());
    }
    publishMods();
}

// Motion 1 moves the dot left/right and, a quarter-turn later, up/down - so it circles.
void SolIntentProcessor::setOrbit (bool on)
{
    {
        std::lock_guard<std::mutex> g (modMutex);
        modLinks.erase (std::remove_if (modLinks.begin(), modLinks.end(), [] (auto& l)
                                        { return l.source == motion1 && (l.paramID == "morphX" || l.paramID == "morphY"); }), modLinks.end());
        if (on)
        {
            modLinks.push_back ({ motion1, "morphX", 0.45f, true });
            modLinks.push_back ({ motion1, "morphY", 0.45f, false });
        }
    }
    publishMods();
}

bool SolIntentProcessor::isOrbiting() const
{
    std::lock_guard<std::mutex> g (modMutex);
    for (auto& l : modLinks) if (l.source == motion1 && l.paramID == "morphX" && l.quarter) return true;
    return false;
}

void SolIntentProcessor::publishMods()
{
    std::array<ActiveMod, maxMods> fresh {};
    int count = 0;
    {
        std::lock_guard<std::mutex> g (modMutex);
        for (auto& l : modLinks)
        {
            auto* prm = dynamic_cast<juce::RangedAudioParameter*> (params.getParameter (l.paramID));
            if (prm == nullptr || count >= maxMods) continue;
            fresh[(size_t) count++] = { l.source, prm, params.getRawParameterValue (l.paramID), l.depth, l.quarter };
        }
    }
    const juce::SpinLock::ScopedLockType sl (modSwap);
    pendingMods = fresh;
    numPendingMods = count;
    modsChanged = true;
}

void SolIntentProcessor::updateModSources (int numSamples)
{
    // the Amount knobs can themselves be modulated (e.g. mod wheel brings Motion 1 in)
    const float amt1 = mod (P.lfoDepth), amt2 = mod (P.lfoDepth2);
    if (numSamples > 0)
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        lfoPhase  = std::fmod (lfoPhase  + twoPi * mod (P.lfoRate)  * (float) numSamples / (float) rate, twoPi);
        lfoPhase2 = std::fmod (lfoPhase2 + twoPi * mod (P.lfoRate2) * (float) numSamples / (float) rate, twoPi);
    }
    srcVal[motion1] = std::sin (lfoPhase) * amt1;
    srcVal[motion2] = std::sin (lfoPhase2) * amt2;
    srcQuarter[0] = std::cos (lfoPhase) * amt1;
    srcQuarter[1] = std::cos (lfoPhase2) * amt2;
    srcVal[modWheelSource] = modWheel;
    srcVal[aftertouchSource] = aftertouch;
}

float SolIntentProcessor::mod (std::atomic<float>* raw) const
{
    const float base = raw->load();
    const juce::RangedAudioParameter* prm = nullptr;
    float offset = 0.0f;
    for (int i = 0; i < numActiveMods; ++i)
    {
        const auto& m = activeMods[(size_t) i];
        if (m.raw != raw) continue;
        prm = m.prm;
        const float v = (m.quarter && m.source <= motion2) ? srcQuarter[m.source] : srcVal[m.source];
        offset += m.depth * v;
    }
    if (prm == nullptr) return base;
    const auto& range = prm->getNormalisableRange();
    const float norm = juce::jlimit (0.0f, 1.0f, range.convertTo0to1 (base) + offset);
    return range.convertFrom0to1 (norm);
}

juce::ValueTree SolIntentProcessor::modsTree() const
{
    juce::ValueTree t ("Mods");
    std::lock_guard<std::mutex> g (modMutex);
    for (auto& l : modLinks)
        t.appendChild (juce::ValueTree ("Mod", { { "source", l.source }, { "param", l.paramID }, { "depth", l.depth }, { "quarter", l.quarter } }), nullptr);
    return t;
}

void SolIntentProcessor::applyModsTree (const juce::ValueTree& state)
{
    {
        std::lock_guard<std::mutex> g (modMutex);
        modLinks.clear();
        for (auto c : state.getChildWithName ("Mods"))
        {
            int src = c["source"];
            auto id = c["param"].toString();
            if (src >= 0 && src < numModSources && canModulate (params.getParameter (id)))
                modLinks.push_back ({ src, id, (float) c["depth"], (bool) c["quarter"] });
        }
    }
    publishMods();
}

// ------------------------------------------------------------------------------ macros
void SolIntentProcessor::applyMacros (bool force)
{
    std::vector<MacroLink> links;
    {
        std::lock_guard<std::mutex> g (macroMutex);
        links = macroLinks;
    }
    for (int m = 0; m < numMacros; ++m)
    {
        const float v = params.getParameter ("macro" + juce::String (m + 1))->getValue();
        if (! force && std::abs (v - lastMacro[(size_t) m]) < 1.0e-4f) continue;
        lastMacro[(size_t) m] = v;
        for (auto& link : links)
            if (link.macro == m)
                if (auto* prm = params.getParameter (link.paramID))
                    prm->setValueNotifyingHost (link.from + (link.to - link.from) * v);
    }
}

void SolIntentProcessor::linkToMacro (int macro, const juce::String& id)
{
    auto* prm = params.getParameter (id);
    if (prm == nullptr || macro < 0 || macro >= numMacros || id.startsWith ("macro")) return;
    const float now = prm->getValue();
    {
        std::lock_guard<std::mutex> g (macroMutex);
        macroLinks.erase (std::remove_if (macroLinks.begin(), macroLinks.end(), [&] (auto& l) { return l.paramID == id; }), macroLinks.end());
        // starts where the knob is now and sweeps toward the far end; "Macro ends here" changes the end
        macroLinks.push_back ({ macro, id, now, now < 0.5f ? 1.0f : 0.0f });
    }
    // line the macro up so the knob doesn't jump: macro at zero = where the knob is
    params.getParameter ("macro" + juce::String (macro + 1))->setValueNotifyingHost (0.0f);
    lastMacro[(size_t) macro] = 0.0f;
}

void SolIntentProcessor::setMacroEnd (int macro, const juce::String& id)
{
    auto* prm = params.getParameter (id);
    if (prm == nullptr) return;
    std::lock_guard<std::mutex> g (macroMutex);
    for (auto& l : macroLinks)
        if (l.paramID == id && l.macro == macro) l.to = prm->getValue();
}

void SolIntentProcessor::unlinkFromMacro (const juce::String& id)
{
    std::lock_guard<std::mutex> g (macroMutex);
    macroLinks.erase (std::remove_if (macroLinks.begin(), macroLinks.end(), [&] (auto& l) { return l.paramID == id; }), macroLinks.end());
}

int SolIntentProcessor::getMacroFor (const juce::String& id) const
{
    std::lock_guard<std::mutex> g (macroMutex);
    for (auto& l : macroLinks) if (l.paramID == id) return l.macro;
    return -1;
}

std::vector<SolIntentProcessor::MacroLink> SolIntentProcessor::getMacroLinks (int macro) const
{
    std::lock_guard<std::mutex> g (macroMutex);
    std::vector<MacroLink> out;
    for (auto& l : macroLinks) if (l.macro == macro) out.push_back (l);
    return out;
}

void SolIntentProcessor::clearMacro (int macro)
{
    std::lock_guard<std::mutex> g (macroMutex);
    macroLinks.erase (std::remove_if (macroLinks.begin(), macroLinks.end(), [&] (auto& l) { return l.macro == macro; }), macroLinks.end());
}

juce::String SolIntentProcessor::getMacroName (int macro) const
{
    std::lock_guard<std::mutex> g (macroMutex);
    return macroNames[(size_t) juce::jlimit (0, numMacros - 1, macro)];
}

void SolIntentProcessor::setMacroName (int macro, const juce::String& name)
{
    std::lock_guard<std::mutex> g (macroMutex);
    macroNames[(size_t) juce::jlimit (0, numMacros - 1, macro)] = name.trim().isEmpty() ? "Macro " + juce::String (macro + 1) : name.trim();
}

// ------------------------------------------------------------------------------ MIDI learn
static int indexOf (const std::vector<juce::RangedAudioParameter*>& v, const juce::String& id)
{
    for (int i = 0; i < (int) v.size(); ++i)
        if (v[(size_t) i]->getParameterID() == id) return i;
    return -1;
}

void SolIntentProcessor::startLearning (const juce::String& id) { learningIndex = indexOf (learnable, id); }

void SolIntentProcessor::forgetLearning (const juce::String& id)
{
    int idx = indexOf (learnable, id);
    for (auto& c : ccToParam) if (c.load() == idx) c = -1;
    saveDefaultMidiMap();
}

juce::ValueTree SolIntentProcessor::midiMapTree() const
{
    juce::ValueTree mm ("MidiMap");
    for (int c = 0; c < 128; ++c)
        if (int idx = ccToParam[(size_t) c].load(); idx >= 0 && idx < (int) learnable.size())
            mm.appendChild (juce::ValueTree ("CC", { { "cc", c }, { "param", learnable[(size_t) idx]->getParameterID() } }), nullptr);
    return mm;
}

void SolIntentProcessor::applyMidiMapTree (const juce::ValueTree& mm)
{
    for (auto& c : ccToParam) c = -1;
    for (auto cc : mm)
    {
        int idx = indexOf (learnable, cc["param"].toString());
        int num = cc["cc"];
        if (idx >= 0 && num >= 0 && num < 128) ccToParam[(size_t) num] = idx;
    }
}

// The controller setup is also kept on this computer, so a fresh copy of the plugin
// (a new track, a new project) already knows your knobs and faders.
void SolIntentProcessor::saveDefaultMidiMap()
{
    if (auto xml = midiMapTree().createXml())
        sol::Library::setSetting ("midiMap", xml->toString (juce::XmlElement::TextFormat().singleLine()));
}

void SolIntentProcessor::loadDefaultMidiMap()
{
    if (auto xml = juce::parseXML (sol::Library::getSetting ("midiMap")))
        applyMidiMapTree (juce::ValueTree::fromXml (*xml));
}

int SolIntentProcessor::getLearnedCC (const juce::String& id) const
{
    int idx = indexOf (learnable, id);
    for (int c = 0; c < 128; ++c) if (idx >= 0 && ccToParam[(size_t) c].load() == idx) return c;
    return -1;
}

juce::String SolIntentProcessor::getLearningParam() const
{
    int i = learningIndex.load();
    return i >= 0 && i < (int) learnable.size() ? learnable[(size_t) i]->getParameterID() : juce::String();
}

// ------------------------------------------------------------------------------ state & presets
juce::ValueTree SolIntentProcessor::makeState()
{
    auto state = params.copyState();
    auto root = sol::Library::getLibraryFolder();
    for (int i = 0; i < numLayers; ++i)
    {
        auto& l = layers[(size_t) i];
        juce::ValueTree lt ("Layer");
        lt.setProperty ("index", i, nullptr);
        lt.setProperty ("name", l.info.name, nullptr);
        lt.setProperty ("folder", l.info.folder.getFullPathName(), nullptr);
        lt.setProperty ("relative", l.info.folder.isAChildOf (root) ? l.info.folder.getRelativePathFrom (root) : juce::String(), nullptr);
        lt.setProperty ("articulation", l.articulation.load(), nullptr);
        state.removeChild (state.getChildWithProperty ("index", i), nullptr);
        state.appendChild (lt, nullptr);
    }
    state.removeChild (state.getChildWithName ("MidiMap"), nullptr);
    state.appendChild (midiMapTree(), nullptr);

    juce::ValueTree macros ("Macros");
    {
        std::lock_guard<std::mutex> g (macroMutex);
        for (int m = 0; m < numMacros; ++m)
            macros.appendChild (juce::ValueTree ("Name", { { "index", m }, { "text", macroNames[(size_t) m] } }), nullptr);
        for (auto& l : macroLinks)
            macros.appendChild (juce::ValueTree ("Link", { { "macro", l.macro }, { "param", l.paramID }, { "from", l.from }, { "to", l.to } }), nullptr);
    }
    state.removeChild (state.getChildWithName ("Macros"), nullptr);
    state.appendChild (macros, nullptr);
    state.removeChild (state.getChildWithName ("Mods"), nullptr);
    state.appendChild (modsTree(), nullptr);
    state.removeChild (state.getChildWithName ("Roll"), nullptr);
    state.appendChild (roll.getPattern()->toTree(), nullptr);
    state.setProperty ("padBase", padBaseNote, nullptr);
    state.setProperty ("preset", currentPresetName, nullptr);
    return state;
}

void SolIntentProcessor::applyState (const juce::ValueTree& state)
{
    params.replaceState (state);
    padBaseNote = state.getProperty ("padBase", 48);
    currentPresetName = state.getProperty ("preset", "Init").toString();

    if (auto mm = state.getChildWithName ("MidiMap"); mm.isValid() && mm.getNumChildren() > 0)
        applyMidiMapTree (mm);   // a project with its own controller setup; otherwise keep the computer's default

    {
        std::lock_guard<std::mutex> g (macroMutex);
        macroLinks.clear();
        for (int m = 0; m < numMacros; ++m) macroNames[(size_t) m] = "Macro " + juce::String (m + 1);
        for (auto c : state.getChildWithName ("Macros"))
        {
            if (c.hasType ("Name")) { int m = c["index"]; if (m >= 0 && m < numMacros) macroNames[(size_t) m] = c["text"].toString(); }
            if (c.hasType ("Link"))
            {
                int m = c["macro"];
                if (m >= 0 && m < numMacros && params.getParameter (c["param"].toString()) != nullptr)
                    macroLinks.push_back ({ m, c["param"].toString(), (float) c["from"], (float) c["to"] });
            }
        }
    }
    applyModsTree (state);
    if (auto rt = state.getChildWithName ("Roll"); rt.isValid())
        roll.setPattern (sol::roll::Pattern::fromTree (rt));
    for (auto& v : lastMacro) v = -1.0f;   // linked knobs follow the macros again
    for (int m = 0; m < numMacros; ++m) lastMacro[(size_t) m] = params.getParameter ("macro" + juce::String (m + 1))->getValue();

    rescanLibrary();
    auto root = sol::Library::getLibraryFolder();
    for (auto lt : state)
    {
        if (! lt.hasType ("Layer")) continue;
        int i = lt["index"];
        if (i < 0 || i >= numLayers) continue;
        auto name = lt["name"].toString();
        if (name.isEmpty() && lt["folder"].toString().isEmpty())   // an empty slot
        {
            if (layers[(size_t) i].info.name.isNotEmpty()) clearLayer (i);
            continue;
        }
        juce::File folder (lt["folder"].toString());
        auto rel = lt["relative"].toString();

        // look in the current library first (the project may come from another computer)
        int found = -1;
        if (rel.isNotEmpty()) found = findInList (root.getChildFile (rel));
        if (found < 0 && lt["folder"].toString().isNotEmpty()) found = findInList (folder);
        if (found < 0 && lt["folder"].toString().isEmpty()) found = 0;   // built-in demo
        if (found < 0)
            for (int k = 1; k < (int) instrumentList.size(); ++k)
                if (instrumentList[(size_t) k].name == name) { found = k; break; }

        if (found >= 0)                selectInstrument (i, found);
        else if (folder.isDirectory()) queueLoad (i, { name, "Library", folder });
        else { setStatus ("This sound uses \"" + name + "\", which isn't in your library."); continue; }
        layers[(size_t) i].articulation = (int) lt["articulation"];
    }
}

void SolIntentProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = makeState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SolIntentProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml != nullptr && xml->hasTagName (params.state.getType()))
        applyState (juce::ValueTree::fromXml (*xml));
}

juce::Array<juce::File> SolIntentProcessor::getPresets() const
{
    auto files = sol::Library::presetFolder().findChildFiles (juce::File::findFiles, true, "*.solpreset");
    files.sort();
    return files;
}

bool SolIntentProcessor::savePreset (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    currentPresetName = clean;
    auto state = makeState();
    state.removeChild (state.getChildWithName ("MidiMap"), nullptr);   // controller setup stays with the project
    state.removeChild (state.getChildWithName ("Roll"), nullptr);      // so does the drawn roll
    auto xml = state.createXml();
    return xml != nullptr && xml->writeTo (sol::Library::presetFolder().getChildFile (clean + ".solpreset"));
}

// By default a preset changes the sound (knobs, effects, macros) and leaves the instruments you
// have loaded alone. "Presets also change instruments" brings back each preset's own instruments.
bool SolIntentProcessor::loadPreset (const juce::File& f, bool withInstruments)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType())) return false;
    auto state = juce::ValueTree::fromXml (*xml);
    state.setProperty ("preset", f.getFileNameWithoutExtension(), nullptr);
    auto current = makeState();
    state.appendChild (current.getChildWithName ("MidiMap").createCopy(), nullptr);

    if (! withInstruments)
    {
        // keep the loaded instruments, their articulations, which layers are on, and their key zones
        for (int i = 0; i < numLayers; ++i)
        {
            state.removeChild (state.getChildWithProperty ("index", i), nullptr);
            if (auto lt = current.getChildWithProperty ("index", i); lt.isValid() && lt.hasType ("Layer"))
                state.appendChild (lt.createCopy(), nullptr);
            for (auto what : { "On", "KeyLo", "KeyHi" })
            {
                auto id = layerParam (i, what);
                for (auto p : state)
                    if (p.hasType ("PARAM") && p["id"].toString() == id)
                        p.setProperty ("value", params.getRawParameterValue (id)->load(), nullptr);
            }
        }
        // only the sound changes, so the instruments don't need reloading
        params.replaceState (state);
        currentPresetName = f.getFileNameWithoutExtension();
        {
            std::lock_guard<std::mutex> g (macroMutex);
            macroLinks.clear();
            for (int m = 0; m < numMacros; ++m) macroNames[(size_t) m] = "Macro " + juce::String (m + 1);
            for (auto c : state.getChildWithName ("Macros"))
            {
                if (c.hasType ("Name")) { int m = c["index"]; if (m >= 0 && m < numMacros) macroNames[(size_t) m] = c["text"].toString(); }
                if (c.hasType ("Link"))
                {
                    int m = c["macro"];
                    if (m >= 0 && m < numMacros && params.getParameter (c["param"].toString()) != nullptr)
                        macroLinks.push_back ({ m, c["param"].toString(), (float) c["from"], (float) c["to"] });
                }
            }
        }
        for (int m = 0; m < numMacros; ++m) lastMacro[(size_t) m] = params.getParameter ("macro" + juce::String (m + 1))->getValue();
        applyModsTree (state);
        return true;
    }

    stopAllSound();
    applyState (state);
    return true;
}

bool SolIntentProcessor::presetsChangeInstruments() const { return sol::Library::getSetting ("presetsChangeInstruments") == "1"; }
void SolIntentProcessor::setPresetsChangeInstruments (bool b) { sol::Library::setSetting ("presetsChangeInstruments", b ? "1" : "0"); }

juce::AudioProcessorEditor* SolIntentProcessor::createEditor() { return new SolIntentEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SolIntentProcessor(); }
