#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <vector>

// =====================================================================================
//  The instrument library.
//
//  A library is a folder. Inside it, every folder that holds audio files is one
//  instrument. A folder with no audio of its own is a pack, and the folders inside it
//  are its instruments:
//
//      Library/
//          Crystal Bowls (Chromatic)/   C3.wav  C#3.wav ...         <- instrument
//              Swells/                  C3.wav ...                  <- extra articulation
//          Koshi Chimes/                                            <- pack
//              Koshi Terra/             strike1.wav ...             <- instrument
//
//  Mapping (root notes, key ranges, velocities, articulations) is read from note names
//  in the file names, or from instrument.json in the folder, which the plugin's MAP
//  screen writes. Nothing needs rebuilding to add instruments.
// =====================================================================================

namespace sol
{

// ---- mapping only (no audio): what instrument.json holds and the MAP screen edits
struct ZoneSpec
{
    juce::String file;          // relative to the instrument folder, e.g. "Swells/C3.wav"
    int root = 60;
    int lo = -1, hi = -1;       // -1 = work it out from the neighbouring roots
    int velLo = 0, velHi = 127;
    float gainDb = 0.0f;
};

struct ArticulationSpec
{
    juce::String name;
    std::vector<ZoneSpec> zones;
};

struct InstrumentSpec
{
    juce::String name;
    bool letRing = true;
    std::vector<ArticulationSpec> articulations;
};

// ---- loaded, playable
struct Zone
{
    juce::String fileName;
    juce::AudioBuffer<float> audio;     // always 2 channels
    double sampleRate = 44100.0;
    int rootNote = 60, loKey = 60, hiKey = 60, loVel = 0, hiVel = 127;
    float gain = 1.0f;
};

struct Articulation
{
    juce::String name;
    std::vector<Zone> zones;
    int lowestKey = 60, highestKey = 60;

    const Zone* findZone (int note, int velocity, unsigned int rrCounter) const;
    bool hasRootAt (int note) const;
};

struct Instrument
{
    juce::String name, category;
    juce::File folder;                  // empty for the built-in demo
    std::vector<Articulation> articulations;
    bool letRingDefault = true;

    const Articulation* get (int index) const;   // clamped; nullptr if none
    int numArticulations() const { return (int) articulations.size(); }
};

using InstrumentPtr = std::shared_ptr<const Instrument>;

struct InstrumentInfo
{
    juce::String name, category;
    juce::File folder;                  // empty = built-in demo
    bool isDemo() const { return folder == juce::File(); }
};

namespace Library
{
    juce::File defaultLibraryFolder();
    juce::File getLibraryFolder();                  // shared by every copy of the plugin
    void setLibraryFolder (const juce::File&);
    void ensureLibraryExists (const juce::File&);
    juce::File presetFolder();

    std::vector<InstrumentInfo> scan (const juce::File& root);

    InstrumentSpec describe (const InstrumentInfo&);                 // mapping without loading audio
    void fillAutoRanges (ArticulationSpec&);                         // turns -1 ranges into real ones
    bool writeSpec (const juce::File& folder, const InstrumentSpec&);
    InstrumentPtr load (const InstrumentInfo&, juce::AudioFormatManager&, juce::String& errorOut);
    InstrumentPtr makeDemoBowl();

    // maps files by their names; files with no note go on free keys from C3 up
    ArticulationSpec autoMapFiles (const juce::String& name, const juce::File& instFolder,
                                   const juce::Array<juce::File>& files, const std::vector<int>& takenKeys = {});
    juce::Array<juce::File> audioFilesIn (const juce::File& folder);

    // makes "Library/<name>" with an empty mapping; returns the folder
    juce::File createInstrument (const juce::String& name);

    int parseNoteFromName (const juce::String& fileNameWithoutExt, int middleCOctave = 3);
    bool isAudioFile (const juce::File&);
}

juce::String noteName (int midiNote);   // 60 -> "C3"

} // namespace sol
