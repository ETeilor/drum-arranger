#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Drums.h"

// Cyrillic literals must reach JUCE as UTF-8
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }
inline juce::String U(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

namespace ui {
// the dark palette of the choir plug-in with an accent of its own
const juce::Colour bg{0xff0c0c0e}, panel{0xff141417}, panelHi{0xff1c1c20}, line{0xff29292f}, text{0xfff1f0ec},
    textDim{0xff80808a}, accent{0xffff9d4d}, onAccent{0xff1a0e04}, danger{0xffff6b5e}, switchOff{0xff3a3a42};
inline juce::Colour lift(juce::Colour c, float amount) { return c.brighter(amount); }
inline juce::Font font(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
// colour says which layer an instrument belongs to: low blue, snare red, cymbals amber, toms teal
inline juce::Colour roleColour(int role) {
    using namespace drums;
    if (role == Timpani) return juce::Colour(0xffa8d98a);
    switch (roleLayer(role)) {
        case L_LOW: return juce::Colour(0xff8ab4f8);
        case L_SNARE: return juce::Colour(0xfff28b82);
        case L_CYMBALS: return juce::Colour(0xfff0c674);
        default: return juce::Colour(0xff7fd6c2);
    }
}
}  // namespace ui
