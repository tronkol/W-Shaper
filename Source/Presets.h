#pragma once

#include <utility>
#include <vector>

namespace wsp
{
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // valori "veri"; il resto torna ai default
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Voice - Follow Reference",  {} },
        { "Voice - Average Timbre",    { { "source", 1.0f } } },
        { "Voice - Slow Morph",        { { "source", 2.0f }, { "smooth", 400.0f }, { "attack", 60.0f }, { "release", 300.0f } } },
        { "Voice - Fast Morph",        { { "source", 2.0f }, { "smooth", 15.0f }, { "attack", 2.0f }, { "release", 40.0f } } },
        { "Air Band Only",             { { "start", 6000.0f }, { "end", 18000.0f }, { "amount", 80.0f } } },
        { "Body Only",                 { { "start", 150.0f }, { "end", 2500.0f }, { "slope", 12.0f } } },
        { "Sibilants to Pink",         { { "source", 0.0f }, { "noise", 1.0f }, { "target", 0.0f }, { "start", 3000.0f }, { "end", 12000.0f } } },
        { "Breathy Noise",             { { "source", 0.0f }, { "noise", 6.0f }, { "amount", 70.0f } } },
        { "Formant Up",                { { "formant", 4.0f } } },
        { "Formant Down",              { { "formant", -4.0f } } },
    };
    return list;
}
}
