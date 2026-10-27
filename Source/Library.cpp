#include "Library.h"
#include <regex>
#include <map>
#include <algorithm>
#include <cmath>
#include <array>

namespace sol
{

juce::String noteName (int midiNote)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midiNote % 12) + 12) % 12]) + juce::String (midiNote / 12 - 2);
}

// ------------------------------------------------------------------------------ playing
const Zone* Articulation::findZone (int note, int velocity, unsigned int rrCounter) const
{
    const Zone* candidates[64];
    int count = 0;

    for (auto& z : zones)
        if (note >= z.loKey && note <= z.hiKey && velocity >= z.loVel && velocity <= z.hiVel && count < 64)
            candidates[count++] = &z;

    if (count == 0)   // gap in the velocity layers: use the nearest layer (not a random mix of all of them)
    {
        int best = 1000;
        for (auto& z : zones)
            if (note >= z.loKey && note <= z.hiKey)
                best = juce::jmin (best, velocity < z.loVel ? z.loVel - velocity : velocity - z.hiVel);
        for (auto& z : zones)
            if (note >= z.loKey && note <= z.hiKey && count < 64
                && (velocity < z.loVel ? z.loVel - velocity : velocity - z.hiVel) == best)
                candidates[count++] = &z;
    }

    // several recordings answer the same key + velocity = round robin
    return count > 0 ? candidates[rrCounter % (unsigned int) count] : nullptr;
}

bool Articulation::hasRootAt (int note) const
{
    for (auto& z : zones)
        if (z.rootNote == note)
            return true;
    return false;
}

const Articulation* Instrument::get (int index) const
{
    if (articulations.empty()) return nullptr;
    return &articulations[(size_t) juce::jlimit (0, (int) articulations.size() - 1, index)];
}

// ------------------------------------------------------------------------------ folders & settings
static std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Sol Intent";
    o.folderName          = "Sol Intent";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

static juce::File homeFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Sol Intent");
}

juce::File Library::defaultLibraryFolder() { return homeFolder().getChildFile ("Library"); }

juce::File Library::presetFolder()
{
    auto f = homeFolder().getChildFile ("Presets");
    f.createDirectory();
    return f;
}

juce::String Library::getSetting (const juce::String& key) { return openSettings()->getValue (key); }

void Library::setSetting (const juce::String& key, const juce::String& value)
{
    auto s = openSettings();
    s->setValue (key, value);
    s->saveIfNeeded();
}

juce::File Library::getLibraryFolder()
{
    auto s = openSettings();
    auto path = s->getValue ("libraryFolder");
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        return juce::File (path);
    return defaultLibraryFolder();
}

void Library::setLibraryFolder (const juce::File& f)
{
    auto s = openSettings();
    s->setValue ("libraryFolder", f.getFullPathName());
    s->saveIfNeeded();
}

void Library::ensureLibraryExists (const juce::File& root)
{
    if (! root.isDirectory())
        root.createDirectory();

    auto note = root.getChildFile ("HOW TO ADD SOUNDS.txt");
    if (! note.existsAsFile())
        note.replaceWithText (
            "Easiest way: in the plugin press  + New instrument , then drag your WAV files onto the MAP screen.\n\n"
            "Or by hand - each folder in here is one instrument:\n"
            "1. Make a folder named after the instrument and put the recordings inside\n"
            "   (WAV, AIFF, FLAC, OGG or MP3).\n"
            "2. Put the note in each file name, like  C3.wav  or  Crystal Bowl F#2.wav  (C3 = middle C).\n"
            "   Files with no note in the name are laid out one per key, starting at C3.\n"
            "3. Several takes of one note? Add rr1, rr2 ... to the names and they take turns.\n"
            "   Soft and hard hits? Add v1 (softest), v2, v3 ... and velocity picks between them.\n"
            "4. Other playing styles (swells, rim hits, rolls) go in folders inside the instrument folder,\n"
            "   e.g.  Crystal Bowls\\Swells\\C3.wav  - each folder becomes an articulation.\n"
            "5. A folder with no recordings of its own is a pack: the folders inside it are its instruments.\n"
            "6. Press Rescan in the plugin.\n");
}

bool Library::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3") && ! f.getFileName().startsWith (".");
}

juce::Array<juce::File> Library::audioFilesIn (const juce::File& dir)
{
    juce::Array<juce::File> out;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles))
        if (Library::isAudioFile (e.getFile()))
            out.add (e.getFile());
    out.sort();
    return out;
}

static juce::Array<juce::File> subfolders (const juce::File& dir)
{
    juce::Array<juce::File> out;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories))
        if (! e.getFile().getFileName().startsWith ("."))
            out.add (e.getFile());
    out.sort();
    return out;
}

static bool isInstrumentFolder (const juce::File& dir)
{
    return dir.getChildFile ("instrument.json").existsAsFile() || ! Library::audioFilesIn (dir).isEmpty();
}

std::vector<InstrumentInfo> Library::scan (const juce::File& root)
{
    std::vector<InstrumentInfo> out;
    out.push_back ({ "Demo Bowl (built-in)", "Built-in", juce::File() });

    if (! root.isDirectory())
        return out;

    std::vector<InstrumentInfo> packs;
    for (auto& dir : subfolders (root))
    {
        if (isInstrumentFolder (dir))
            out.push_back ({ dir.getFileName(), "My instruments", dir });
        else
            for (auto& sub : subfolders (dir))   // a pack
                if (isInstrumentFolder (sub))
                    packs.push_back ({ sub.getFileName(), dir.getFileName(), sub });
    }
    out.insert (out.end(), packs.begin(), packs.end());
    return out;
}

juce::File Library::createInstrument (const juce::String& name)
{
    auto root = getLibraryFolder();
    ensureLibraryExists (root);
    auto clean = juce::File::createLegalFileName (name.trim().isEmpty() ? "New Instrument" : name.trim());
    auto folder = root.getChildFile (clean);
    for (int i = 2; folder.exists(); ++i)
        folder = root.getChildFile (clean + " " + juce::String (i));
    folder.createDirectory();

    InstrumentSpec spec;
    spec.name = folder.getFileName();
    spec.articulations.push_back ({ "Strike", {} });
    writeSpec (folder, spec);
    return folder;
}

// ------------------------------------------------------------------------------ name parsing
int Library::parseNoteFromName (const juce::String& name, int middleCOctave)
{
    // Strip round-robin / velocity tags first so "rr2" or "v3" can't be mistaken for notes.
    auto s = name.toStdString();
    s = std::regex_replace (s, std::regex ("(^|[^A-Za-z])(rr|RR|v|V|vel|Vel|VEL)[0-9]+"), "$1");

    // The last thing in the name that looks like a note wins: C3, F#2, Bb4, Cs3, c-1
    std::regex noteRx ("(^|[^A-Za-z])([A-Ga-g])(#|s|b|\xE2\x99\xAF|\xE2\x99\xAD)?(-?[0-9])(?![0-9])");
    int result = -1;

    for (auto it = std::sregex_iterator (s.begin(), s.end(), noteRx); it != std::sregex_iterator(); ++it)
    {
        const auto& m = *it;
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int semis = base[std::toupper ((unsigned char) m[2].str()[0]) - 'A'];
        auto acc = m[3].str();
        if (acc == "#" || acc == "s" || acc == "\xE2\x99\xAF") semis += 1;
        if (acc == "b" || acc == "\xE2\x99\xAD")               semis -= 1;
        int midi = (std::stoi (m[4].str()) - middleCOctave) * 12 + 60 + semis;
        if (midi >= 0 && midi <= 127)
            result = midi;
    }
    return result;
}

static int parseTag (const juce::String& name, const char* pattern)
{
    std::smatch m;
    auto s = name.toStdString();
    if (std::regex_search (s, m, std::regex (pattern)))
        return std::stoi (m[2].str());
    return -1;
}

// ------------------------------------------------------------------------------ mapping
void Library::fillAutoRanges (ArticulationSpec& art)
{
    // Each key between two recorded notes plays whichever recording is closer,
    // so every recording plays back untouched on its own key.
    std::vector<int> roots;
    for (auto& z : art.zones) roots.push_back (z.root);
    std::sort (roots.begin(), roots.end());
    roots.erase (std::unique (roots.begin(), roots.end()), roots.end());
    const int stretch = 12;   // how far past the lowest / highest recording it may repitch

    for (auto& z : art.zones)
    {
        auto idx = (size_t) std::distance (roots.begin(), std::find (roots.begin(), roots.end(), z.root));
        if (z.lo < 0) z.lo = idx == 0 ? juce::jmax (0, z.root - stretch)
                                      : roots[idx - 1] + (z.root - roots[idx - 1]) / 2 + 1;
        if (z.hi < 0) z.hi = idx == roots.size() - 1 ? juce::jmin (127, z.root + stretch)
                                                     : z.root + (roots[idx + 1] - z.root) / 2;
        if (z.hi < z.lo) std::swap (z.lo, z.hi);
    }
}

bool Library::anyNamedNotes (const juce::Array<juce::File>& files)
{
    for (auto& f : files)
        if (parseNoteFromName (f.getFileNameWithoutExtension()) >= 0) return true;
    return false;
}

ArticulationSpec Library::autoMapFiles (const juce::String& name, const juce::File& instFolder, const juce::Array<juce::File>& files,
                                       const std::vector<int>& takenKeys, ImportMode mode)
{
    ArticulationSpec art;
    art.name = name;

    struct Parsed { juce::File file; int note, vel; bool named; };
    std::vector<Parsed> parsed;
    juce::Array<juce::File> unnamed;

    for (auto& f : files)
    {
        auto base = f.getFileNameWithoutExtension();
        int note = parseNoteFromName (base);
        int vel = parseTag (base, "(^|[^A-Za-z])(?:v|V|vel|Vel|VEL)([0-9]+)");
        if (note < 0) unnamed.add (f);
        else          parsed.push_back ({ f, note, vel, true });
    }

    int nextKey = 60;   // no note in the name: one per key from C3 up
    for (auto& f : unnamed)
    {
        auto taken = [&] (int k)
        {
            for (auto& p : parsed) if (p.note == k) return true;
            return std::find (takenKeys.begin(), takenKeys.end(), k) != takenKeys.end();
        };
        while (taken (nextKey) && nextKey < 127) ++nextKey;
        parsed.push_back ({ f, nextKey++, -1, false });
    }

    std::map<int, std::vector<int>> velsPerNote;   // v1, v2, v3 ... split 0-127 evenly
    for (auto& p : parsed)
        if (p.vel >= 0) velsPerNote[p.note].push_back (p.vel);
    for (auto& [n, v] : velsPerNote) { std::sort (v.begin(), v.end()); v.erase (std::unique (v.begin(), v.end()), v.end()); }

    for (auto& p : parsed)
    {
        ZoneSpec z;
        z.file = p.file.getRelativePathFrom (instFolder).replaceCharacter ('\\', '/');
        z.root = p.note;
        const bool spread = mode == ImportMode::Spread || (mode == ImportMode::Auto && p.named);
        if (! spread) z.lo = z.hi = p.note;   // its own key only, exactly as recorded
        if (p.vel >= 0)
        {
            auto& layers = velsPerNote[p.note];
            auto idx = (int) std::distance (layers.begin(), std::find (layers.begin(), layers.end(), p.vel));
            auto n = (int) layers.size();
            z.velLo = idx == 0 ? 0 : (128 * idx) / n;
            z.velHi = idx == n - 1 ? 127 : (128 * (idx + 1)) / n - 1;
        }
        art.zones.push_back (z);
    }
    std::stable_sort (art.zones.begin(), art.zones.end(), [] (auto& a, auto& b) { return a.root < b.root; });
    return art;
}

static ZoneSpec zoneFromVar (const juce::var& zv)
{
    ZoneSpec z;
    z.file   = zv["file"].toString();
    z.root   = zv.hasProperty ("root")   ? (int) zv["root"]   : 60;
    z.lo     = zv.hasProperty ("lo")     ? (int) zv["lo"]     : -1;
    z.hi     = zv.hasProperty ("hi")     ? (int) zv["hi"]     : -1;
    z.velLo  = zv.hasProperty ("velLo")  ? (int) zv["velLo"]  : 0;
    z.velHi  = zv.hasProperty ("velHi")  ? (int) zv["velHi"]  : 127;
    z.gainDb = zv.hasProperty ("gainDb") ? (float) (double) zv["gainDb"] : 0.0f;
    return z;
}

InstrumentSpec Library::describe (const InstrumentInfo& info)
{
    InstrumentSpec spec;
    spec.name = info.name;
    if (info.isDemo())
        return spec;

    auto jsonFile = info.folder.getChildFile ("instrument.json");
    if (jsonFile.existsAsFile())
    {
        auto json = juce::JSON::parse (jsonFile);
        if (json.hasProperty ("name"))    spec.name = json["name"].toString();
        if (json.hasProperty ("letRing")) spec.letRing = (bool) json["letRing"];

        if (auto* arts = json["articulations"].getArray())
        {
            for (auto& av : *arts)
            {
                ArticulationSpec a;
                a.name = av["name"].toString();
                if (auto* zs = av["zones"].getArray())
                    for (auto& zv : *zs) a.zones.push_back (zoneFromVar (zv));
                spec.articulations.push_back (std::move (a));
            }
        }
        else if (auto* zs = json["zones"].getArray())   // simple single-articulation form
        {
            ArticulationSpec a { "Main", {} };
            for (auto& zv : *zs) a.zones.push_back (zoneFromVar (zv));
            spec.articulations.push_back (std::move (a));
        }
        if (! spec.articulations.empty())
            return spec;
    }

    // No mapping file: work it out from the file and folder names.
    if (auto files = Library::audioFilesIn (info.folder); ! files.isEmpty())
        spec.articulations.push_back (Library::autoMapFiles ("Main", info.folder, files));
    for (auto& sub : subfolders (info.folder))
        if (auto files = Library::audioFilesIn (sub); ! files.isEmpty())
            spec.articulations.push_back (Library::autoMapFiles (sub.getFileName(), info.folder, files));
    return spec;
}

bool Library::writeSpec (const juce::File& folder, const InstrumentSpec& spec)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", spec.name);
    root->setProperty ("letRing", spec.letRing);
    juce::Array<juce::var> arts;
    for (auto& a : spec.articulations)
    {
        auto* ao = new juce::DynamicObject();
        ao->setProperty ("name", a.name);
        juce::Array<juce::var> zones;
        for (auto& z : a.zones)
        {
            auto* zo = new juce::DynamicObject();
            zo->setProperty ("file", z.file);
            zo->setProperty ("root", z.root);
            zo->setProperty ("lo", z.lo);
            zo->setProperty ("hi", z.hi);
            zo->setProperty ("velLo", z.velLo);
            zo->setProperty ("velHi", z.velHi);
            zo->setProperty ("gainDb", z.gainDb);
            zones.add (juce::var (zo));
        }
        ao->setProperty ("zones", zones);
        arts.add (juce::var (ao));
    }
    root->setProperty ("articulations", arts);
    return folder.getChildFile ("instrument.json").replaceWithText (juce::JSON::toString (juce::var (root)));
}

// ------------------------------------------------------------------------------ octave copies
static constexpr int octaveTaps = 24;   // each side of the half-band filter

void Zone::buildOctaves()
{
    // half-band low-pass (windowed sinc, Blackman), then keep every other sample
    constexpr int N = octaveTaps;
    static const std::array<float, 2 * octaveTaps + 1> h = []
    {
        constexpr int N = octaveTaps;
        std::array<float, 2 * N + 1> k {};
        double sum = 0.0;
        for (int j = -N; j <= N; ++j)
        {
            const double x = 0.45 * j;   // cutoff a little under half the band
            const double sinc = j == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double t = (double) (j + N) / (2.0 * N);
            const double win = 0.42 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * t) + 0.08 * std::cos (2.0 * juce::MathConstants<double>::twoPi * t);
            k[(size_t) (j + N)] = (float) (sinc * win);
            sum += sinc * win;
        }
        for (auto& v : k) v = (float) (v / sum);
        return k;
    }();

    octaves.clear();
    const juce::AudioBuffer<float>* src = &audio;
    for (int level = 1; level <= 3; ++level)
    {
        const int inLen = src->getNumSamples(), outLen = inLen / 2;
        if (outLen < 64) break;
        juce::AudioBuffer<float> dst (2, outLen);
        for (int c = 0; c < 2; ++c)
        {
            const float* x = src->getReadPointer (c);
            float* y = dst.getWritePointer (c);
            for (int i = 0; i < outLen; ++i)
            {
                const int centre = 2 * i;
                float acc = 0.0f;
                if (centre >= N && centre + N < inLen)
                    for (int j = -N; j <= N; ++j) acc += h[(size_t) (j + N)] * x[centre + j];
                else
                    for (int j = -N; j <= N; ++j)
                    {
                        const int idx = centre + j;
                        if (idx >= 0 && idx < inLen) acc += h[(size_t) (j + N)] * x[idx];
                    }
                y[i] = acc;
            }
        }
        octaves.push_back (std::move (dst));
        src = &octaves.back();
    }
}

// ------------------------------------------------------------------------------ loading audio
static bool readFile (const juce::File& f, juce::AudioFormatManager& fm, Zone& z)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0)
        return false;

    auto len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) r->sampleRate * 600);   // up to 10 minutes per recording
    z.audio.setSize (2, len);
    z.audio.clear();
    r->read (&z.audio, 0, len, 0, true, r->numChannels > 1);
    if (r->numChannels == 1)
        z.audio.copyFrom (1, 0, z.audio, 0, 0, len);
    z.sampleRate = r->sampleRate;
    z.fileName = f.getFileName();
    z.buildOctaves();
    return true;
}

InstrumentPtr Library::load (const InstrumentInfo& info, juce::AudioFormatManager& fm, juce::String& error)
{
    if (info.isDemo())
        return makeDemoBowl();

    auto spec = describe (info);
    auto inst = std::make_shared<Instrument>();
    inst->name = spec.name;
    inst->category = info.category;
    inst->folder = info.folder;
    inst->letRingDefault = spec.letRing;

    for (auto& as : spec.articulations)
    {
        fillAutoRanges (as);
        Articulation art;
        art.name = as.name;
        for (auto& zs : as.zones)
        {
            Zone z;
            if (! readFile (info.folder.getChildFile (zs.file), fm, z))
            {
                error << "Couldn't read " << zs.file << "\n";
                continue;
            }
            z.rootNote = zs.root;
            z.loKey = zs.lo; z.hiKey = zs.hi;
            z.loVel = zs.velLo; z.hiVel = zs.velHi;
            z.gain = juce::Decibels::decibelsToGain (zs.gainDb);
            art.zones.push_back (std::move (z));
        }
        art.lowestKey = 127; art.highestKey = 0;
        for (auto& z : art.zones) { art.lowestKey = juce::jmin (art.lowestKey, z.loKey); art.highestKey = juce::jmax (art.highestKey, z.hiKey); }
        if (art.zones.empty()) { art.lowestKey = 60; art.highestKey = 59; }
        inst->articulations.push_back (std::move (art));
    }

    if (inst->articulations.empty())
        inst->articulations.push_back ({ "Strike", {}, 60, 59 });
    return inst;
}

// ------------------------------------------------------------------------------ built-in demo
// A synthesised singing bowl so the plugin makes sound before any recordings are added.
// Only a placeholder for testing - the real thing is the recorded library.
InstrumentPtr Library::makeDemoBowl()
{
    auto inst = std::make_shared<Instrument>();
    inst->name = "Demo Bowl (built-in)";
    inst->category = "Built-in";

    // rings for ~22 s and fades out naturally (32 kHz keeps the memory down - it's only a demo)
    const double sr = 32000.0;
    const int len = (int) (sr * 22.0);
    juce::Random rng (1234);
    ArticulationSpec layout { "Strike", {} };
    Articulation art;
    art.name = "Strike";

    for (int root = 48; root <= 84; root += 6)
    {
        Zone z;
        z.fileName = "demo " + noteName (root);
        z.sampleRate = sr;
        z.rootNote = root;
        z.audio.setSize (2, len);
        z.audio.clear();

        const double f0 = 440.0 * std::pow (2.0, (root - 69) / 12.0);
        // bowl-like (inharmonic) partials: ratio, level, decay seconds, beat Hz
        const double partials[][4] = { { 1.0, 1.0, 9.0, 0.7 }, { 2.71, 0.5, 6.0, 1.3 }, { 5.15, 0.22, 3.2, 2.1 },
                                       { 8.32, 0.1, 1.3, 3.0 }, { 12.1, 0.05, 0.7, 0.0 } };
        auto* L = z.audio.getWritePointer (0);
        auto* R = z.audio.getWritePointer (1);

        for (auto& p : partials)
        {
            double f = f0 * p[0];
            if (f > sr * 0.45) continue;
            double ph1 = rng.nextDouble() * juce::MathConstants<double>::twoPi, ph2 = ph1 + 0.6;
            double w1 = juce::MathConstants<double>::twoPi * f / sr;
            double w2 = juce::MathConstants<double>::twoPi * (f + p[3]) / sr;
            for (int i = 0; i < len; ++i)
            {
                double t = i / sr;
                double env = p[1] * std::exp (-t / p[2]) * juce::jmin (1.0, t * 400.0);
                L[i] += (float) (env * (0.6 * std::sin (ph1) + 0.4 * std::sin (ph2)));
                R[i] += (float) (env * (0.4 * std::sin (ph1) + 0.6 * std::sin (ph2 + 0.3)));
                ph1 += w1; ph2 += w2;
            }
        }
        for (int i = 0; i < (int) (sr * 0.012); ++i)   // mallet tick
        {
            float n = (rng.nextFloat() * 2.0f - 1.0f) * 0.15f * std::exp (-(float) i / 80.0f);
            L[i] += n; R[i] += n;
        }
        // gentle fade over the last 4 seconds so it never stops abruptly
        const int fadeLen = (int) (sr * 4.0);
        for (int i = len - fadeLen; i < len; ++i)
        {
            const float f = (float) (len - i) / (float) fadeLen;
            L[i] *= f * f; R[i] *= f * f;
        }
        z.audio.applyGain (0.25f);
        z.buildOctaves();
        layout.zones.push_back ({ {}, root });
        art.zones.push_back (std::move (z));
    }

    fillAutoRanges (layout);
    art.lowestKey = 127; art.highestKey = 0;
    for (size_t i = 0; i < art.zones.size(); ++i)
    {
        art.zones[i].loKey = layout.zones[i].lo;
        art.zones[i].hiKey = layout.zones[i].hi;
        art.lowestKey = juce::jmin (art.lowestKey, art.zones[i].loKey);
        art.highestKey = juce::jmax (art.highestKey, art.zones[i].hiKey);
    }
    inst->articulations.push_back (std::move (art));
    return inst;
}

} // namespace sol
