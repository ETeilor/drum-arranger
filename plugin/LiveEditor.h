#pragma once
#include "LiveProcessor.h"
#include "Look.h"

// the take played so far, dragged out to a track as a MIDI file
class TakeChip : public juce::Component, public juce::SettableTooltipClient {
public:
    std::function<juce::File()> makeFile;
    juce::String label;
    bool enabled = false;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    bool hover = false, dragging = false;
};

class DrumLiveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DrumLiveEditor(DrumLiveProcessor&);
    ~DrumLiveEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> keyBox(int i) const;
    DrumLiveProcessor& proc;
    DrumLook look;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    juce::ComboBox styleBox, mapBox, keysBox, barsBox, fillLenBox, polyBox;
    juce::ToggleButton velBtn{U("Сила нажатия задаёт сложность")};
    juce::TextButton clearBtn{U("Очистить")};
    juce::Slider complexity, polyN;
    juce::Label titleLbl, styleLbl, mapLbl, keysLbl, phraseLbl, complexityLbl, polyLbl, polyNLbl;
    std::unique_ptr<CA> styleA, mapA, keysA, barsA, fillLenA, polyA;
    std::unique_ptr<BA> velA;
    std::unique_ptr<SA> complexityA, polyNA;
    TakeChip take;
    DrumLiveProcessor::Snapshot shown;
    int shownHits = -1, shownKey = -1;
    juce::TooltipWindow tips{this, 700};
};
