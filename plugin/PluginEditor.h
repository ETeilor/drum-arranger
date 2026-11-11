#pragma once
#include "GridView.h"
#include "Look.h"
#include "PluginProcessor.h"

// One of the four things the plug-in builds. A click selects it, a drag carries it to a DAW track as a
// .mid file, the arrows step through its variants.
class Card : public juce::Component, public juce::SettableTooltipClient {
public:
    juce::String title, caption;
    const drums::Result* result = nullptr;
    bool selected = false, hasVariants = true;
    std::function<void()> onSelect;
    std::function<void(int)> onStep;
    std::function<juce::File()> makeFile;
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::Rectangle<int> arrow(int dir) const;
    bool hover = false, dragging = false;
};

class DrumEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit DrumEditor(DrumProcessor&);
    ~DrumEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropHover = false; repaint(); }

private:
    void timerCallback() override;
    void refresh();
    void openSource();
    void exportSelected();
    void showError(const juce::String& err);

    DrumProcessor& proc;
    DrumLook look;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::TextButton sourceBtn{U("Ритм из MIDI")}, clearBtn, playBtn{U("Слушать")}, exportBtn{U("MIDI")};
    juce::ComboBox styleBox, mapBox, outBox, numBox, denBox, barsBox, fillEveryBox, fillLenBox, polyBox, tonicBox;
    juce::ToggleButton hostBtn{U("DAW")}, landingBtn{U("Акцент")};
    juce::TextButton lockBtn[drums::NUM_LAYERS];
    juce::Label titleLbl, styleLbl, mapLbl, meterLbl, slashLbl, phraseLbl, feelLbl, polyLbl, lockLbl, tonicLbl, report;
    struct Row {
        juce::Label name, value;
        juce::Slider slider;
        std::unique_ptr<SA> att;
    };
    Row tempo, complexity, feel, swing, polyN;
    std::unique_ptr<CA> styleA, mapA, outA, numA, denA, barsA, fillEveryA, fillLenA, polyA, tonicA;
    std::unique_ptr<BA> hostA, landingA, lockA[drums::NUM_LAYERS];

    Card cards[DrumProcessor::kNumKinds];
    GridView grid;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dropHover = false;
    juce::TooltipWindow tips{this, 700};
};
