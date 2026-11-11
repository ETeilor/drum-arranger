#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Drums.h"

// Drum Arranger Live: a MIDI FX in front of a drum library. Five neighbouring keys run it: groove on and
// off, next variant, fill, break, stop. How hard the key is hit sets the complexity. It follows the
// host transport when the host plays, and its own clock when it does not.
class DrumLiveProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    enum Trigger { tGroove, tVariant, tFill, tBreak, tStop, tCount };
    enum Mode { mStopped, mGroove, mFill, mBreak };

    DrumLiveProcessor();
    ~DrumLiveProcessor() override;
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Drum Arranger Live"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    // for the editor
    struct Snapshot {
        int mode = mStopped, bar = 0, bars = 4, complexity = 5, variant = 0;
        double barProgress = 0;
    };
    Snapshot snapshot() const;
    void press(int trigger, int velocity = 100) { pressed_[(size_t)trigger] = velocity; }   // a click in the editor
    int firstKey() const;

    // everything played since the last clear, for dragging to a track
    int recordedHits() const { return recCount_.load(); }
    void clearRecording();
    juce::File writeRecordingFile() const;

private:
    void parameterChanged(const juce::String&, float) override { dirty_ = true; }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    drums::Settings settings() const;
    void rebuild();
    void handle(int trigger, int velocity, double absTick);
    void emit(const drums::Hit& h, int sample, juce::MidiBuffer& out, double absTick);

    drums::LiveBars bars_;
    std::atomic<bool> dirty_{true};
    std::array<std::atomic<int>, tCount> pressed_{};
    bool running_ = false;
    long long startBar_ = 0, fillBar_ = -1, breakBar_ = -1, lastBar_ = -1;
    int liveComplexity_ = -1, grooveVariant_ = 0, fillVariant_ = 0, breakVariant_ = 0;
    double freeTicks_ = 0, sampleRate_ = 44100, bpm_ = 120, builtBpm_ = 0;
    int hostNum_ = 0, hostDen_ = 0;
    bool wasHostPlaying_ = false;
    long long clock_ = 0;
    struct Off { long long at; int note, channel; };
    std::vector<Off> offs_;

    mutable juce::SpinLock snapLock_;
    Snapshot snap_;

    mutable juce::SpinLock recLock_;
    std::vector<drums::Hit> rec_;
    double recOrigin_ = -1;
    std::atomic<int> recCount_{0};
    drums::Meter recMeter_;
    double recBpm_ = 120;
    mutable int dragCounter_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumLiveProcessor)
};
