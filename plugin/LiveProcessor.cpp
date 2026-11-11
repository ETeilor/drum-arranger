#include "LiveProcessor.h"
#include "LiveEditor.h"
#include "Ui.h"

namespace {
const int DENS[] = {2, 4, 8, 16};
const int BAR_CHOICES[] = {1, 2, 4, 8};
const int KEY_BASES[] = {24, 36, 48, 60, 72, 84};
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DrumLiveProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray styles, maps, polys;
    for (int i = 0; i < drums::numStyles(); ++i) styles.add(U(drums::styleLabel(i)));
    for (int i = 0; i < drums::NUM_MAPS; ++i) maps.add(U(drums::mapLabel(i)));
    for (int i = 0; i < drums::NUM_POLY; ++i) polys.add(U(drums::polyLabel(i)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"style", 1}, U("Стиль"), styles, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"map", 1}, U("Библиотека"), maps, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"keys", 1}, U("Клавиши запуска"), StringArray{"C0", "C1", "C2", "C3", "C4", "C5"}, 3));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"num", 1}, U("Размер: доли"), 1, 32, 4));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"den", 1}, U("Размер: длительность"), StringArray{"2", "4", "8", "16"}, 1));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"tempo", 1}, U("Темп"), NormalisableRange<float>(40.0f, 300.0f, 1.0f), 120.0f));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"bars", 1}, U("Тактов"), StringArray{"1", "2", "4", "8"}, 2));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"filllen", 1}, U("Длина филла"), StringArray{U("доля"), U("две доли"), U("такт")}, 1));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"complexity", 1}, U("Сложность"), 0, 10, 5));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{"velcomplex", 1}, U("Сила нажатия задаёт сложность"), true));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"feel", 1}, U("Живость"), 0, 10, 3));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"swing", 1}, U("Свинг"), 0, 100, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"poly", 1}, U("Полиритм"), polys, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{"polyn", 1}, U("Полиритм: N"), 2, 23, 5));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{"tonic", 1}, U("Строй литавр"),
                                                 StringArray{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}, 0));
    return l;
}

DrumLiveProcessor::DrumLiveProcessor() : AudioProcessor(BusesProperties()), params(*this, nullptr, "DrumArrangerLive", layout()) {
    for (auto& p : pressed_) p = 0;
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) params.addParameterListener(r->getParameterID(), this);
    offs_.reserve(256);
    rec_.reserve(60000);
}

DrumLiveProcessor::~DrumLiveProcessor() {
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) params.removeParameterListener(r->getParameterID(), this);
}

void DrumLiveProcessor::prepareToPlay(double sr, int) { sampleRate_ = sr; }

int DrumLiveProcessor::firstKey() const { return KEY_BASES[juce::jlimit(0, 5, (int)std::lround(params.getRawParameterValue("keys")->load()))]; }

drums::Settings DrumLiveProcessor::settings() const {
    auto v = [&](const char* id) { return (int)std::lround(params.getRawParameterValue(id)->load()); };
    drums::Settings s;
    s.style = v("style");
    s.meter.num = hostNum_ > 0 ? hostNum_ : v("num");
    s.meter.den = hostDen_ > 0 ? hostDen_ : DENS[juce::jlimit(0, 3, v("den"))];
    s.bpm = bpm_;
    s.bars = BAR_CHOICES[juce::jlimit(0, 3, v("bars"))];
    s.fillLen = v("filllen");
    s.complexity = liveComplexity_ >= 0 && v("velcomplex") ? liveComplexity_ : v("complexity");
    s.feel = v("feel");
    s.swing = v("swing");
    s.poly = v("poly");
    s.polyN = v("polyn");
    s.tonic = v("tonic");
    s.grooveVariant = grooveVariant_;
    s.fillVariant = fillVariant_;
    s.breakVariant = breakVariant_;
    return s;
}

void DrumLiveProcessor::rebuild() {
    bars_ = drums::liveBars(settings());
    builtBpm_ = bpm_;
    dirty_ = false;
}

void DrumLiveProcessor::handle(int trigger, int velocity, double absTick) {
    const bool velToC = params.getRawParameterValue("velcomplex")->load() > 0.5f;
    auto setComplexity = [&] {
        if (!velToC) return;
        int c = juce::jlimit(0, 10, (velocity * 11) / 128);
        if (c != liveComplexity_) { liveComplexity_ = c; dirty_ = true; }
    };
    const int bt = std::max(1, bars_.barTicks);
    const long long bar = (long long)std::floor(absTick / bt);
    const double inBar = absTick - (double)bar * bt;
    auto start = [&] {
        if (running_) return;
        running_ = true;
        startBar_ = bar;
    };
    switch (trigger) {
        case tGroove:
            if (running_) { running_ = false; fillBar_ = breakBar_ = -1; }
            else { setComplexity(); start(); }
            break;
        case tVariant: ++grooveVariant_; dirty_ = true; break;
        case tFill: {
            setComplexity();
            start();
            // the fill sits at the end of a bar: this bar if it has not begun yet, else the next one
            int lenChoice = (int)std::lround(params.getRawParameterValue("filllen")->load());
            int fillStart = lenChoice >= 2 ? 0 : bt - bt / (lenChoice == 0 ? 4 : 2);
            fillBar_ = inBar <= fillStart + 1 ? bar : bar + 1;
            break;
        }
        case tBreak:
            setComplexity();
            start();
            breakBar_ = inBar < 1.0 ? bar : bar + 1;
            break;
        case tStop: running_ = false; fillBar_ = breakBar_ = -1; break;
    }
}

void DrumLiveProcessor::emit(const drums::Hit& h, int sample, juce::MidiBuffer& out, double absTick) {
    const int map = (int)std::lround(params.getRawParameterValue("map")->load());
    const bool pitched = h.role == drums::Timpani && h.pitch >= 0;
    const int note = pitched ? h.pitch : drums::noteFor(map, h.role), ch = pitched ? 1 : 10;
    // a key that is still sounding is released first
    for (auto& o : offs_)
        if (o.note == note && o.channel == ch && o.at > clock_ + sample) { out.addEvent(juce::MidiMessage::noteOff(ch, note), sample); o.at = -1; }
    out.addEvent(juce::MidiMessage::noteOn(ch, note, (juce::uint8)juce::jlimit(1, 127, h.vel)), sample);
    const double samplesPerTick = sampleRate_ * 60.0 / (bpm_ * drums::TPB);
    if (offs_.size() < offs_.capacity()) offs_.push_back({clock_ + sample + std::max(1LL, (long long)(h.len * samplesPerTick)), note, ch});
    const juce::SpinLock::ScopedTryLockType l(recLock_);
    if (l.isLocked() && rec_.size() < rec_.capacity()) {
        if (recOrigin_ < 0) {
            recOrigin_ = std::floor(absTick / bars_.barTicks) * bars_.barTicks;
            recBpm_ = bpm_;
            recMeter_ = settings().meter;
        }
        drums::Hit r = h;
        r.tick = r.grid = (int)std::lround(absTick - recOrigin_);
        rec_.push_back(r);
        recCount_ = (int)rec_.size();
    }
}

void DrumLiveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    const int n = buffer.getNumSamples();
    bool hostPlaying = false;
    double ppq = 0;
    bpm_ = params.getRawParameterValue("tempo")->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) if (*b > 1.0) bpm_ = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto ts = pos->getTimeSignature()) {
                if (ts->numerator != hostNum_ || ts->denominator != hostDen_) { hostNum_ = ts->numerator; hostDen_ = ts->denominator; dirty_ = true; }
            }
        }
    if (std::abs(bpm_ - builtBpm_) > 0.5) dirty_ = true;   // the tempo decides which fills are playable
    if (dirty_.load()) rebuild();
    const double ticksPerSample = bpm_ * drums::TPB / 60.0 / sampleRate_;
    // one timeline: the host's while it plays, our own otherwise
    if (hostPlaying != wasHostPlaying_) {
        wasHostPlaying_ = hostPlaying;
        if (hostPlaying && running_) startBar_ = (long long)std::floor(ppq * drums::TPB / bars_.barTicks);
        if (!hostPlaying) freeTicks_ = 0, startBar_ = 0;
        fillBar_ = breakBar_ = lastBar_ = -1;
    }
    const double t0 = hostPlaying ? ppq * drums::TPB : freeTicks_;

    // keys: five in a row from the chosen C; everything else passes through
    juce::MidiBuffer out;
    const int base = firstKey();
    static const int offsets[tCount] = {0, 1, 2, 4, 5};   // C, C#, D, E, F
    for (const auto m : midi) {
        auto msg = m.getMessage();
        int trig = -1;
        if (msg.isNoteOnOrOff())
            for (int i = 0; i < tCount; ++i) if (msg.getNoteNumber() == base + offsets[i]) trig = i;
        if (trig < 0) { out.addEvent(msg, m.samplePosition); continue; }
        if (msg.isNoteOn()) handle(trig, msg.getVelocity(), t0 + m.samplePosition * ticksPerSample);
    }
    for (int i = 0; i < tCount; ++i) {
        int v = pressed_[(size_t)i].exchange(0);
        if (v > 0) handle(i, v, t0);
    }
    if (dirty_.load()) rebuild();

    // note-offs that fall into this block
    for (auto& o : offs_)
        if (o.at >= 0 && o.at < clock_ + n) { out.addEvent(juce::MidiMessage::noteOff(o.channel, o.note), (int)std::max(0LL, o.at - clock_)); o.at = -1; }
    offs_.erase(std::remove_if(offs_.begin(), offs_.end(), [](const Off& o) { return o.at < 0; }), offs_.end());

    const int bt = std::max(1, bars_.barTicks);
    Snapshot s;
    s.bars = (int)bars_.groove.size();
    s.complexity = settings().complexity;
    s.variant = grooveVariant_;
    if (running_ && !bars_.groove.empty()) {
        const double t1 = t0 + n * ticksPerSample;
        const long long firstBar = (long long)std::floor(t0 / bt) - 1, lastBar = (long long)std::floor((t1 - 1e-9) / bt);
        for (long long b = std::max(firstBar, startBar_ - 1); b <= lastBar; ++b) {
            const std::vector<drums::Hit>* pat = nullptr;
            if (b == fillBar_) pat = &bars_.fill;
            else if (b == breakBar_) pat = &bars_.brk;
            else if (b >= startBar_) pat = &bars_.groove[(size_t)((b - startBar_) % (long long)bars_.groove.size())];
            if (!pat) continue;
            const bool special = b == fillBar_ || b == breakBar_;
            for (auto& h : *pat) {
                if (!special && h.tick >= bt) continue;
                // the bar after a fill or a break opens with their landing, not with its own downbeat
                if (!special && h.tick < 30 && (b - 1 == fillBar_ || b - 1 == breakBar_) && b - 1 >= 0) continue;
                const double abs = (double)b * bt + h.tick;
                if (abs < t0 || abs >= t1) continue;
                emit(h, juce::jlimit(0, n - 1, (int)((abs - t0) / ticksPerSample)), out, abs);
            }
        }
        const long long cur = (long long)std::floor(t0 / bt);
        if (cur != lastBar_) {
            // a fill that has been played makes room for the next one
            if (lastBar_ >= 0 && lastBar_ == fillBar_) { ++fillVariant_; dirty_ = true; }
            if (lastBar_ >= 0 && lastBar_ == breakBar_) { ++breakVariant_; dirty_ = true; }
            lastBar_ = cur;
        }
        s.mode = cur == fillBar_ ? mFill : cur == breakBar_ ? mBreak : mGroove;
        s.bar = (int)(((cur - startBar_) % (long long)bars_.groove.size() + (long long)bars_.groove.size()) % (long long)bars_.groove.size());
        s.barProgress = (t0 - (double)cur * bt) / bt;
    }
    if (!hostPlaying) freeTicks_ = running_ ? freeTicks_ + n * ticksPerSample : 0.0;
    if (!running_ && !hostPlaying) { startBar_ = 0; lastBar_ = -1; }
    clock_ += n;
    midi.swapWith(out);
    const juce::SpinLock::ScopedTryLockType l(snapLock_);
    if (l.isLocked()) snap_ = s;
}

DrumLiveProcessor::Snapshot DrumLiveProcessor::snapshot() const {
    const juce::SpinLock::ScopedLockType l(snapLock_);
    return snap_;
}

void DrumLiveProcessor::clearRecording() {
    const juce::SpinLock::ScopedLockType l(recLock_);
    rec_.clear();
    recOrigin_ = -1;
    recCount_ = 0;
}

juce::File DrumLiveProcessor::writeRecordingFile() const {
    drums::Result r;
    {
        const juce::SpinLock::ScopedLockType l(recLock_);
        if (rec_.empty()) return {};
        r.hits = rec_;
        r.meter = recMeter_;
        r.bpm = recBpm_;
    }
    const int bt = r.meter.num * 4 * drums::TPB / r.meter.den;
    r.length = ((r.hits.back().tick / bt) + 1) * bt;
    drums::MidiOptions o;
    o.map = (int)std::lround(params.getRawParameterValue("map")->load());
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("DrumArranger")
                   .getChildFile("take" + juce::String(juce::Time::currentTimeMillis()) + "_" + juce::String(++dragCounter_));
    dir.createDirectory();
    auto f = dir.getChildFile("Drum Live Take.mid");
    auto bytes = drums::writeMidi(r, o);
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

void DrumLiveProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = params.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void DrumLiveProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*xml));
    dirty_ = true;
}

juce::AudioProcessorEditor* DrumLiveProcessor::createEditor() { return new DrumLiveEditor(*this); }

#ifndef DRUMS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumLiveProcessor(); }
#endif
