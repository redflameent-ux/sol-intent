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
    P.attack = g ("attack"); P.decay = g ("decay"); P.sustain = g ("sustain"); P.release = g ("release");
    P.letRing = g ("letRing"); P.velSens = g ("velSens"); P.bend = g ("bendRange");
    P.tone = g ("tone"); P.width = g ("width"); P.revMix = g ("reverbMix"); P.revSize = g ("reverbSize");
    P.lfoRate = g ("lfoRate"); P.lfoDepth = g ("lfoDepth"); P.lfoTarget = g ("lfoTarget"); P.keyswitch = g ("keyswitch");
    P.on[0] = g ("aOn"); P.vol[0] = g ("aVol"); P.tun[0] = g ("aTune"); P.pan[0] = g ("aPan");
    P.on[1] = g ("bOn"); P.vol[1] = g ("bVol"); P.tun[1] = g ("bTune"); P.pan[1] = g ("bPan");

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

    rescanLibrary();
    queueLoad (0, instrumentList.front());   // the built-in demo until a project says otherwise
    startTimerHz (10);
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
    add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMix", 1 }, "Space", NormalisableRange<float> (0.0f, 1.0f), 0.25f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbSize", 1 }, "Room", NormalisableRange<float> (0.0f, 1.0f), 0.8f, pct));

    // envelope
    add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", NormalisableRange<float> (0.0f, 4000.0f, 1.0f, 0.35f), 0.0f, ms));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "decay", 1 }, "Decay", NormalisableRange<float> (0.0f, 10000.0f, 1.0f, 0.35f), 500.0f, ms));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", NormalisableRange<float> (0.02f, 20.0f, 0.01f, 0.4f), 3.0f, sec));
    add (std::make_unique<AudioParameterBool>  (ParameterID { "letRing", 1 }, "Let Ring", true));

    // motion
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 1 }, "Rate", NormalisableRange<float> (0.05f, 12.0f, 0.01f, 0.4f), 0.5f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth", 1 }, "Depth", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoTarget", 1 }, "Motion", StringArray { "Pitch", "Volume", "Tone", "Pan" }, 1));
    add (std::make_unique<AudioParameterChoice> (ParameterID { "keyswitch", 1 }, "Keyswitch", StringArray { "Off", "C-1", "C0", "C1", "C6" }, 2));

    // layers
    add (std::make_unique<AudioParameterBool>  (ParameterID { "aOn", 1 }, "Layer A On", true));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "aVol", 1 }, "A Level", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterInt>   (ParameterID { "aTune", 1 }, "A Tune", -24, 24, 0, st));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "aPan", 1 }, "A Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, panTxt));
    add (std::make_unique<AudioParameterBool>  (ParameterID { "bOn", 1 }, "Layer B On", false));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "bVol", 1 }, "B Level", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), 0.0f, db));
    add (std::make_unique<AudioParameterInt>   (ParameterID { "bTune", 1 }, "B Tune", -24, 24, 0, st));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "bPan", 1 }, "B Pan", NormalisableRange<float> (-1.0f, 1.0f), 0.0f, panTxt));
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
            if (cc == 1) modWheel = m.getControllerValue() / 127.0f;

            if (int learn = learningIndex.load(); learn >= 0 && cc != 64)   // sustain pedal can't be learned
            {
                for (auto& c : ccToParam) if (c.load() == learn) c = -1;
                ccToParam[(size_t) cc] = learn;
                learningIndex = -1;
            }
            if (int idx = ccToParam[(size_t) cc].load(); idx >= 0 && idx < (int) learnable.size())
            {
                learnable[(size_t) idx]->setValueNotifyingHost (m.getControllerValue() / 127.0f);
                continue;   // a learned knob, not a sound control
            }
        }

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

    keyboardState.processNextMidiBuffer (midi, 0, n, true);   // pads and on-screen keys join the incoming MIDI
    handleMidi (midi, filtered);

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
        s.attackSec  = P.attack->load() / 1000.0f;
        s.decaySec   = P.decay->load() / 1000.0f;
        s.sustain    = P.sustain->load();
        s.releaseSec = P.release->load();
        s.letRing    = P.letRing->load() > 0.5f;
        s.velSens    = P.velSens->load();
        s.bendRange  = P.bend->load();
        if (l.buffer.getNumSamples() < n) l.buffer.setSize (2, n, false, false, true);
        l.buffer.clear (0, n);
    }

    const float baseTune = P.tune->load() + P.fine->load() / 100.0f;
    const float depth = juce::jlimit (0.0f, 1.0f, P.lfoDepth->load() + modWheel);   // the mod wheel adds motion
    const int target = (int) P.lfoTarget->load();
    const float lfoInc = juce::MathConstants<float>::twoPi * P.lfoRate->load() / (float) sr;
    const bool stereo = buffer.getNumChannels() > 1;
    auto* L = buffer.getWritePointer (0);
    auto* R = stereo ? buffer.getWritePointer (1) : nullptr;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        const float lfo = std::sin (lfoPhase) * depth;   // -depth .. +depth
        lfoPhase = std::fmod (lfoPhase + lfoInc * (float) len, juce::MathConstants<float>::twoPi);

        for (int i = 0; i < numLayers; ++i)
        {
            auto& l = layers[(size_t) i];
            l.settings.tuneSemis = baseTune + P.tun[i]->load() + (target == 0 ? lfo * 0.5f : 0.0f);   // up to +/- 50 cents
            if (on[i])
                l.synth.renderNextBlock (l.buffer, filtered, start, len);

            // layer level and pan (constant power), LFO pan / volume on top
            float pan = juce::jlimit (-1.0f, 1.0f, P.pan[i]->load() + (target == 3 ? lfo : 0.0f));
            float g = juce::Decibels::decibelsToGain (P.vol[i]->load()) * (target == 1 ? 1.0f - 0.5f * depth + 0.5f * lfo : 1.0f);
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
        float t = juce::jlimit (-1.0f, 1.0f, P.tone->load() + (target == 2 ? lfo * 0.8f : 0.0f));
        if (std::abs (t - lastTone) > 0.002f) { toneL.set (t, sr); toneR.set (t, sr); lastTone = t; }
        for (int k = start; k < start + len; ++k)
        {
            L[k] = toneL.process (L[k]);
            if (R != nullptr) R[k] = toneR.process (R[k]);
        }
    }

    if (R != nullptr)   // stereo width
    {
        const float width = P.width->load();
        if (std::abs (width - 1.0f) > 1.0e-4f)
            for (int i = 0; i < n; ++i)
            {
                float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * width;
                L[i] = m + s; R[i] = m - s;
            }
    }

    juce::Reverb::Parameters rp;
    const float mix = P.revMix->load();
    rp.roomSize = 0.5f + 0.49f * P.revSize->load();
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
    const float master = juce::Decibels::decibelsToGain (P.volume->load());
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
    juce::ValueTree mm ("MidiMap");
    for (int c = 0; c < 128; ++c)
        if (int idx = ccToParam[(size_t) c].load(); idx >= 0)
            mm.appendChild (juce::ValueTree ("CC", { { "cc", c }, { "param", learnable[(size_t) idx]->getParameterID() } }), nullptr);
    state.removeChild (state.getChildWithName ("MidiMap"), nullptr);
    state.appendChild (mm, nullptr);
    state.setProperty ("padBase", padBaseNote, nullptr);
    state.setProperty ("preset", currentPresetName, nullptr);
    return state;
}

void SolIntentProcessor::applyState (const juce::ValueTree& state)
{
    params.replaceState (state);
    padBaseNote = state.getProperty ("padBase", 48);
    currentPresetName = state.getProperty ("preset", "Init").toString();

    if (auto mm = state.getChildWithName ("MidiMap"); mm.isValid())
    {
        for (auto& c : ccToParam) c = -1;
        for (auto cc : mm)
        {
            int idx = indexOf (learnable, cc["param"].toString());
            int num = cc["cc"];
            if (idx >= 0 && num >= 0 && num < 128) ccToParam[(size_t) num] = idx;
        }
    }

    rescanLibrary();
    auto root = sol::Library::getLibraryFolder();
    for (auto lt : state)
    {
        if (! lt.hasType ("Layer")) continue;
        int i = lt["index"];
        if (i < 0 || i >= numLayers) continue;
        auto name = lt["name"].toString();
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
    auto xml = state.createXml();
    return xml != nullptr && xml->writeTo (sol::Library::presetFolder().getChildFile (clean + ".solpreset"));
}

bool SolIntentProcessor::loadPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType())) return false;
    auto state = juce::ValueTree::fromXml (*xml);
    state.setProperty ("preset", f.getFileNameWithoutExtension(), nullptr);
    auto keepMap = makeState().getChildWithName ("MidiMap").createCopy();
    state.appendChild (keepMap, nullptr);
    stopAllSound();
    applyState (state);
    return true;
}

juce::AudioProcessorEditor* SolIntentProcessor::createEditor() { return new SolIntentEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SolIntentProcessor(); }
