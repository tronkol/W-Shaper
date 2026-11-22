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
        { "Natural Match",        {} },
        { "Full Transfer",        { { "amount", 100.0f }, { "match", 100.0f }, { "detail", 70.0f } } },
        { "Average Timbre Only",  { { "match", 0.0f } } },
        { "Fast & Precise",       { { "attack", 2.0f }, { "release", 30.0f }, { "smooth", 10.0f }, { "detail", 75.0f } } },
        { "Smooth Morph",         { { "attack", 40.0f }, { "release", 200.0f }, { "smooth", 200.0f } } },
        { "Body Only",            { { "start", 80.0f }, { "end", 3000.0f }, { "slope", 12.0f } } },
        { "Presence & Air",       { { "start", 2000.0f }, { "end", 18000.0f } } },
        { "Formant Up",           { { "formant", 3.0f } } },
        { "Formant Down",         { { "formant", -3.0f } } },
    };
    return list;
}
}
