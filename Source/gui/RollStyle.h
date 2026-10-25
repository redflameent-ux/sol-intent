#pragma once
// Colours and fonts for the ROLL page, matching the Sol Intent palette.
#include <juce_gui_basics/juce_gui_basics.h>

namespace sol::gui
{
namespace col
{
    const juce::Colour bg        { 0xff070a14 };
    const juce::Colour panel     { 0xff0b0e1a };
    const juce::Colour panelHi   { 0xff151a2d };
    const juce::Colour border    { 0xff262c44 };
    const juce::Colour text      { 0xfff3e6cc };   // cream
    const juce::Colour dim       { 0xff8a8fa8 };
    const juce::Colour track     { 0xff262c44 };
    const juce::Colour wavetable { 0xff3fe6f2 };   // the lines: cyan glow
    const juce::Colour analog    { 0xffe8a64c };   // velocity: amber gold
    const juce::Colour granular  { 0xffb45cff };
    const juce::Colour resonator { 0xffe07a4f };
    const juce::Colour sampler   { 0xfff3e6cc };   // selection: bright cream
    const juce::Colour mod       { 0xffb45cff };   // slide: violet
    const juce::Colour filter    { 0xffef5d8d };   // record
    const juce::Colour fx        { 0xff6d9bff };
}

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace sol::gui
