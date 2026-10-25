#include "RollEngine.h"

namespace sol::roll
{
static constexpr int stepSamples = 64;           // pitch/pressure updates ~750 times a second
static constexpr int previewChannel = 16;

RollEngine::RollEngine()
{
    pattern = std::make_shared<Pattern>();
    audioPattern = pattern;
}

void RollEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    out.ensureSize (16384);
}

void RollEngine::setPattern (const Pattern& p)
{
    auto next = std::make_shared<const Pattern> (p);
    {
        const juce::SpinLock::ScopedLockType sl (patternLock);
        pattern = next;
    }
    keepAlive.push_back (next);
    while (keepAlive.size() > 32) keepAlive.pop_front();   // the audio thread has long since moved on
}

PatternPtr RollEngine::getPattern() const
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    return pattern;
}

void RollEngine::play()  { internalPlaying = true; }
void RollEngine::pause() { internalPlaying = false; }
void RollEngine::stop()  { internalPlaying = false; stopRequested = true; }

bool RollEngine::popRecorded (RecEvent& e)
{
    int s1, n1, s2, n2;
    recFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return false;
    e = recBuf[(size_t) (n1 > 0 ? s1 : s2)];
    recFifo.finishedRead (1);
    return true;
}

void RollEngine::pushRec (const RecEvent& e)
{
    int s1, n1, s2, n2;
    recFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;   // full: drop rather than block
    recBuf[(size_t) (n1 > 0 ? s1 : s2)] = e;
    recFifo.finishedWrite (1);
}

void RollEngine::allNotesOff (juce::MidiBuffer& out, int sample)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        auto& a = active[(size_t) (ch - 1)];
        if (a.lineId >= 0)
        {
            out.addEvent (juce::MidiMessage::noteOff (ch, a.note), sample);
            a = {};
        }
    }
}

float RollEngine::wobble (const Pattern& p, const Line& l, double pos) const
{
    if (p.modShape <= 0 || p.modAmount <= 0.0f) return 0.0f;
    const double cycles = (pos - l.start()) / juce::jmax (1.0e-3, p.modRateBeats);
    const float ph = (float) (cycles - std::floor (cycles));
    float w;
    switch (p.modShape)
    {
        case 1:  w = std::sin (ph * juce::MathConstants<float>::twoPi); break;
        case 2:  w = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
        case 3:  w = 1.0f - 2.0f * ph; break;
        case 4:  w = ph < 0.5f ? 1.0f : -1.0f; break;
        default:
        {
            // stepped random, the same every time the pattern plays
            const auto k = (juce::uint32) ((int) std::floor (cycles) * 2654435761u + (juce::uint32) l.id * 40503u);
            w = (float) ((k >> 8) & 0xffff) / 32767.5f - 1.0f;
            break;
        }
    }
    if (! p.bipolar) w = (w + 1.0f) * 0.5f;
    float fade = 1.0f;
    const double len = l.end() - l.start();
    if (p.modFade > 0.001f && len > 0.0)
        fade = (float) juce::jlimit (0.0, 1.0, (pos - l.start()) / (p.modFade * len));
    return w * fade * p.modAmount * 0.01f;
}

void RollEngine::process (juce::MidiBuffer& midi, int numSamples, juce::AudioPlayHead* playHead, int masterBendRange)
{
    // newest pattern from the screen
    if (patternLock.tryEnter())
    {
        audioPattern = pattern;
        patternLock.exit();
    }
    const Pattern& pat = *audioPattern;
    out.clear();

    // ---- transport ----
    bool hostPlaying = false;
    double hostPpq = 0.0, bpm = 120.0;
    if (playHead != nullptr)
        if (auto pos = playHead->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) hostPpq = *ppq;
        }
    currentBpm = bpm;

    if (stopRequested.exchange (false))
    {
        internalBeat = 0.0;
        allNotesOff (out, 0);
        lastPos = -1.0;
    }

    const bool running = hostPlaying || internalPlaying.load();
    transportRunning = running;
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const double startBeat = hostPlaying ? hostPpq : internalBeat;
    const double len = juce::jmax (0.25, pat.lengthBeats);
    auto patPosAt = [&] (int sample)
    {
        const double b = startBeat + sample * beatsPerSample;
        return pat.loop ? std::fmod (juce::jmax (0.0, b), len) : b;
    };

    // ---- recording: capture what's being played, in pattern time ----
    if (recording.load() && running)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            RecEvent e;
            e.beat = patPosAt (meta.samplePosition);
            e.channel = m.getChannel();
            if (m.isNoteOn()) { e.type = RecEvent::NoteOn; e.note = m.getNoteNumber(); e.value = m.getFloatVelocity(); }
            else if (m.isNoteOff()) { e.type = RecEvent::NoteOff; e.note = m.getNoteNumber(); }
            else if (m.isPitchWheel())
            {
                e.type = RecEvent::Bend;
                const float range = e.channel == 1 ? (float) masterBendRange : 48.0f;
                e.value = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f * range;
            }
            else if (m.isChannelPressure()) { e.type = RecEvent::Pressure; e.value = (float) m.getChannelPressureValue() / 127.0f; }
            else if (m.isController() && m.getControllerNumber() == 74) { e.type = RecEvent::Slide; e.value = (float) m.getControllerValue() / 127.0f; }
            else continue;
            pushRec (e);
        }

    if (! running)
    {
        bool any = false;
        for (auto& a : active) if (a.lineId >= 0 && a.channel != previewChannel) any = true;
        if (any) { for (int ch = 1; ch < previewChannel; ++ch) if (active[(size_t) ch - 1].lineId >= 0) { out.addEvent (juce::MidiMessage::noteOff (ch, active[(size_t) ch - 1].note), 0); active[(size_t) ch - 1] = {}; } }
        playheadBeat = internalPlaying.load() || internalBeat > 0.0 ? std::fmod (internalBeat, len) : -1.0;
        lastPos = -1.0;
    }
    else
    {
        for (int s = 0; s < numSamples; s += stepSamples)
        {
            const double pos = patPosAt (s);
            const double stepBeats = stepSamples * beatsPerSample;

            // jumped (loop wrap or the host moved the playhead)? close everything first
            if (lastPos >= 0.0 && (pos < lastPos - 1.0e-6 || pos > lastPos + 4.0 * stepBeats + 1.0e-3))
                for (int ch = 2; ch < previewChannel; ++ch)
                    if (active[(size_t) ch - 1].lineId >= 0)
                    {
                        out.addEvent (juce::MidiMessage::noteOff (ch, active[(size_t) ch - 1].note), s);
                        active[(size_t) ch - 1] = {};
                    }
            lastPos = pos;

            // notes that ended, or whose line was deleted
            for (int ch = 2; ch < previewChannel; ++ch)
            {
                auto& a = active[(size_t) ch - 1];
                if (a.lineId < 0) continue;
                const Line* line = nullptr;
                for (auto& l : pat.lines) if (l.id == a.lineId) { line = &l; break; }
                if (line == nullptr || pos >= line->end() || pos < line->start())
                {
                    out.addEvent (juce::MidiMessage::noteOff (ch, a.note), s);
                    a = {};
                }
            }

            for (auto& l : pat.lines)
            {
                if (l.nodes.size() < 2 || pos < l.start() || pos >= l.end()) continue;
                int ch = -1;
                for (int c = 2; c < previewChannel; ++c) if (active[(size_t) c - 1].lineId == l.id) { ch = c; break; }

                const float pitch = l.pitchAt (pos) + wobble (pat, l, pos);
                const float pressure = l.pressureAt (pos);
                const float slide = l.slideAt (pos);

                if (ch < 0)
                {
                    // start this note on the next free channel (2-15; 16 is for previews)
                    for (int tries = 0; tries < 14 && ch < 0; ++tries)
                    {
                        const int c = nextChannel;
                        nextChannel = nextChannel >= previewChannel - 1 ? 2 : nextChannel + 1;
                        if (active[(size_t) c - 1].lineId < 0) ch = c;
                    }
                    if (ch < 0) continue;   // 14 notes already sounding
                    auto& a = active[(size_t) ch - 1];
                    a.lineId = l.id; a.channel = ch;
                    a.note = juce::jlimit (0, 127, juce::roundToInt (l.startPitch()));
                    a.bend = pitch - (float) a.note;
                    a.pressure = pressure; a.slide = slide;
                    // bend before the note (so it starts at the right pitch); pressure and slide just
                    // after it, so the new note picks them up even if they never change
                    out.addEvent (juce::MidiMessage::pitchWheel (ch, bendValue (a.bend, 48.0f)), s);
                    out.addEvent (juce::MidiMessage::noteOn (ch, a.note, juce::jlimit (0.01f, 1.0f, l.velocity)), s);
                    out.addEvent (juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, juce::roundToInt (pressure * 127.0f))), s);
                    out.addEvent (juce::MidiMessage::controllerEvent (ch, 74, juce::jlimit (0, 127, juce::roundToInt (slide * 127.0f))), s);
                    continue;
                }

                auto& a = active[(size_t) ch - 1];
                const float bend = pitch - (float) a.note;
                if (std::abs (bend - a.bend) > 0.003f)
                {
                    a.bend = bend;
                    out.addEvent (juce::MidiMessage::pitchWheel (ch, bendValue (bend, 48.0f)), s);
                }
                if (std::abs (pressure - a.pressure) > 0.5f / 127.0f)
                {
                    a.pressure = pressure;
                    out.addEvent (juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, juce::roundToInt (pressure * 127.0f))), s);
                }
                if (std::abs (slide - a.slide) > 0.5f / 127.0f)
                {
                    a.slide = slide;
                    out.addEvent (juce::MidiMessage::controllerEvent (ch, 74, juce::jlimit (0, 127, juce::roundToInt (slide * 127.0f))), s);
                }
            }
        }
        if (! hostPlaying) internalBeat += numSamples * beatsPerSample;
        playheadBeat = patPosAt (0);
    }

    // ---- audition from the screen (channel 16) ----
    const float pp = previewPitch.load();
    auto& pv = active[(size_t) previewChannel - 1];
    if (pp < 0.0f && pv.lineId >= 0)
    {
        out.addEvent (juce::MidiMessage::noteOff (previewChannel, pv.note), 0);
        pv = {};
    }
    else if (pp >= 0.0f)
    {
        if (pv.lineId < 0 || std::abs (pp - (float) pv.note) > 47.0f)
        {
            if (pv.lineId >= 0) out.addEvent (juce::MidiMessage::noteOff (previewChannel, pv.note), 0);
            pv.lineId = 0; pv.channel = previewChannel;
            pv.note = juce::jlimit (0, 127, juce::roundToInt (pp));
            pv.bend = pp - (float) pv.note;
            out.addEvent (juce::MidiMessage::pitchWheel (previewChannel, bendValue (pv.bend, 48.0f)), 0);
            out.addEvent (juce::MidiMessage::noteOn (previewChannel, pv.note, previewVel.load()), 0);
        }
        else if (std::abs ((pp - (float) pv.note) - pv.bend) > 0.003f)
        {
            pv.bend = pp - (float) pv.note;
            out.addEvent (juce::MidiMessage::pitchWheel (previewChannel, bendValue (pv.bend, 48.0f)), 0);
        }
    }

    if (! out.isEmpty()) midi.addEvents (out, 0, numSamples, 0);
}

} // namespace sol::roll
