#pragma once
// Colours and fonts for the ROLL page, matching the Sol Intent palette.
#include <juce_gui_basics/juce_gui_basics.h>

namespace sol::gui
{
namespace col
{
    const juce::Colour bg        { 0xff060a07 };
    const juce::Colour panel     { 0xff0a0f0b };
    const juce::Colour panelHi   { 0xff131c16 };
    const juce::Colour border    { 0xff24332a };
    const juce::Colour text      { 0xfff3e6cc };   // cream
    const juce::Colour dim       { 0xff8a9a8c };
    const juce::Colour track     { 0xff24332a };
    const juce::Colour wavetable { 0xff4fe08a };   // the lines: emerald glow
    const juce::Colour analog    { 0xfff0b445 };   // velocity: sun gold
    const juce::Colour granular  { 0xff6f5cff };
    const juce::Colour resonator { 0xffe07a4f };
    const juce::Colour sampler   { 0xfff3e6cc };   // selection: bright cream
    const juce::Colour mod       { 0xff6f5cff };   // slide: indigo
    const juce::Colour filter    { 0xffef5d8d };   // record
    const juce::Colour fx        { 0xff6d9bff };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace sol::gui
