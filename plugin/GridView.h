#pragma once
#include "Ui.h"

// A drum grid: one lane per instrument, time left to right, the height of a mark is its velocity.
class GridView : public juce::Component {
public:
    void setResult(const drums::Result* r) { result = r; repaint(); }
    void setPlayhead(double ticks) { if (ticks != playhead) { playhead = ticks; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    std::function<juce::File()> makeDragFile;

    // shared with the cards: lanes without labels
    static std::vector<int> lanes(const drums::Result& r);
    static void drawHits(juce::Graphics&, const drums::Result& r, juce::Rectangle<float> area, bool detailed);

    static constexpr float labelWidth = 104.0f;

private:
    const drums::Result* result = nullptr;
    double playhead = -1;
    bool dragging = false;
};
