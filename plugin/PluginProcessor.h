#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DrumSynth.h"
#include "Drums.h"

// The processor keeps the settings and four results built from them: a groove, a fill, a break and a
// phrase (grooves closed by fills). Each can be auditioned and dragged out as a MIDI file.
// Audio output is only the built-in preview kit.
class DrumProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater {
public:
    enum { kGroove, kFill, kBreak, kPhrase, kNumKinds };

    DrumProcessor();
    ~DrumProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Drum Arranger"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- model, message thread only
    juce::AudioProcessorValueTreeState params;
    void regenerate();
    const drums::Result& result(int kind) const { return results_[(size_t)juce::jlimit(0, (int)kNumKinds - 1, kind)]; }
    drums::Settings settings() const;
    int selected() const;
    void select(int kind);
    int variant(int kind) const;
    void stepVariant(int kind, int dir);          // the next variant that sounds different
    juce::File writeFile(int kind) const;         // a .mid in a fresh temp folder, ready to drag
    drums::MidiOptions midiOptions() const;
    // rhythm source: a bass line or an ostinato the kick follows
    bool loadSource(const juce::File& f, juce::String& err);
    void clearSource();
    juce::String sourceName() const { return sourceName_; }
    int sourceNotes() const { return (int)source_.size(); }
    static bool isMidiFile(const juce::File& f);
    std::function<void()> onResultChanged;

    // preview
    void setPreview(bool on);
    bool isPreviewing() const { return previewOn_.load(); }
    double previewPositionTicks() const { return previewTicks_.load(); }

private:
    void parameterChanged(const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void rebuildPreviewEvents();
    void setParam(const char* id, float value);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::array<drums::Result, kNumKinds> results_;
    std::vector<drums::SourceNote> source_;
    juce::String sourceName_;
    mutable int dragCounter_ = 0;

    // host tempo and meter, written by the audio thread
    std::atomic<double> hostBpm_{0.0};
    std::atomic<int> hostNum_{0}, hostDen_{0};
    double usedBpm_ = 0;
    int usedNum_ = 0, usedDen_ = 0;

    struct PEvent { double tick; int role, vel, pitch, note; bool on; };
    juce::SpinLock previewLock_;
    std::vector<PEvent> previewEvents_;
    double previewLenTicks_ = 0, previewBpm_ = 120;
    std::atomic<bool> previewOn_{false}, killNotes_{false};
    std::atomic<double> previewTicks_{0};
    size_t previewIdx_ = 0;
    DrumSynth synth_;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumProcessor)
};
