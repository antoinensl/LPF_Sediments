#pragma once

#include <vector>
#include <array>
#include <string>
#include "Corpus.h"

namespace juce { class Random; }

// Spreading sound over several outputs: stereo, a ring of loudspeakers,
// surround and Atmos beds (5.1 to 9.1.6, SMPTE / Dolby channel order), or
// Ambisonics (AmbiX: ACN channel order, SN3D normalisation) up to 3rd order.
//
// Directions are in degrees, 0 = front, positive = clockwise (to the right),
// elevation positive = up. "dist" goes from 0 (in the middle of the room: the
// sound comes from everywhere) to 1 (fully from its direction).
namespace Spatial
{
    constexpr int maxChannels = 16;

    enum class Kind { stereo, ring, ambisonics, surround };

    // A surround / Atmos bed layout, with what VBAP panning needs.
    struct SpeakerLayout
    {
        struct Speaker { const char* name; float az, el; bool lfe; };
        std::vector<Speaker> speakers;              // in channel order
        bool hasHeight = false;
        int lfeChannel = -1, centreChannel = -1;
        bool centreUsed = true;
        int panCount = 0;                           // speakers that take part in panning
        // panning: speaker triplets (3D) or neighbouring pairs (flat), with inverse matrices
        struct Group { int idx[3]; int n; float inv[9]; };
        std::vector<Group> groups;
        std::vector<int> earLevel;                  // speakers that share a virtual one below
        int nadir = -1;                             // index of the virtual speaker below (in a hidden slot)
        std::string channelOrder() const;
    };

    struct Format
    {
        Kind kind = Kind::stereo;
        int channels = 2;
        int order = 0;              // Ambisonics only
        bool frontCentre = false;   // ring: speaker 1 straight ahead (else the first pair straddles the front)
        bool lfe = false;           // surround: feed the LFE channel with the whole mix (low-passed by the engine)
        const SpeakerLayout* layout = nullptr;      // surround only
        float speakerAngle (int k) const noexcept;   // ring
    };

    // Index of the "Output" parameter -> format.
    Format formatFor (int outputChoice, bool frontCentre, bool useCentre = true, bool lfe = false);
    int channelsFor (int outputChoice);
    extern const char* const outputNames[];
    constexpr int numOutputChoices = 19;
    constexpr int firstSurroundChoice = 12;

    struct Place
    {
        float az = 0.0f, el = 0.0f, dist = 0.0f;
        float ambiDist = 0.0f;      // Ambisonics: may differ (on the dome or sphere, the middle is above, not everywhere)
    };

    // Gains for a sound coming from `place`. For each output channel c, the
    // sound's left input goes in with gl[c] and its right input with gr[c]
    // (stereo output keeps a stereo sound stereo; other formats fold it to mono).
    // `inputStereo` = the sound really has two different channels.
    void gains (const Format&, const Place&, bool inputStereo, float* gl, float* gr);

    // Placement settings, read by the synthesis thread.
    enum class PlaceBy { centre = 0, map, eachSound, pitch, brightness, noisiness, loudness };
    // sphere comes last so that sessions saved with older versions keep their setting
    enum class Height { flat = 0, dome, pitch, brightness, noisiness, loudness, sphere };

    struct Placer
    {
        PlaceBy by = PlaceBy::centre;
        Height height = Height::flat;
        float width = 1.0f;          // 1 = full circle, 0 = everything in front
        float scatter = 0.0f;        // random spread around the computed place
        float rotation = 0.0f;       // degrees, added to every direction
        std::vector<float> soundAz;  // per source index
        // Robust range of each descriptor in the current corpus (for 0..1 values)
        std::array<float, Corpus::numDesc> lo {}, hi {};
        float medianPitch = 60.0f;

        void prepare (const Corpus&);                           // when the corpus changes
        // Where one moment of the corpus goes. With rnd, scatter is applied.
        Place placeUnit (const Corpus&, int unit, juce::Random* rnd) const;
        // Where a sound heard at map position (x, y) goes (the model voice).
        Place placeAt (const Corpus&, float x, float y) const;
    private:
        float descValue (const Corpus&, int unit, int which) const;   // 0..1
        Place finish (Place p, const Corpus&, int unit, float mapX, float mapY, juce::Random* rnd) const;
    };

    // Default directions for n sounds: evenly around the circle.
    float defaultSoundAz (int index, int count);
}
