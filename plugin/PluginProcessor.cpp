#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Ui.h"

namespace {
const char* const LOCK_IDS[drums::NUM_LAYERS] = {"lock0", "lock1", "lock2", "lock3"};
const char* const LVAR_IDS[drums::NUM_LAYERS] = {"lvar0", "lvar1", "lvar2", "lvar3"};
const char* const VAR_IDS[3] = {"gvar", "fvar", "bvar"};
const int BAR_CHOICES[] = {1, 2, 4, 8, 16};
const int FILL_EVERY[] = {0, 2, 4, 8};
const int DENS[] = {2, 4, 8, 16};
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DrumProcessor::layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray styles, maps, polys;
    for (int i = 0; i < drums::numStyles(); ++i) styles.add(U(drums::styleLabel(i)));
    for (int i = 0; i < drums::NUM_MAPS; ++i) maps.add(U(drums::mapLabel(i)));
    for (int i = 0; i < drums::NUM_POLY; ++i) polys.add(U(drums::polyLabel(i)));
    auto choice = [&](const char* id, const char* name, StringArray items, int def) {
        l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, U(name), items, def));
    };
    auto integer = [&](const char* id, const char* name, int lo, int hi, int def) {
        l.add(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, U(name), lo, hi, def));
    };
    auto flag = [&](const char* id, const char* name, bool def) {
        l.add(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, U(name), def));
    };
    choice("style", "Стиль", styles, 0);
    choice("map", "Библиотека", maps, 0);
    choice("outmode", "Выход", {U("Весь набор на одной дорожке"), U("Дорожка на инструмент")}, 0);
    integer("num", "Размер: доли", 1, 32, 4);
    choice("den", "Размер: длительность", {"2", "4", "8", "16"}, 1);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{"tempo", 1}, U("Темп"), NormalisableRange<float>(40.0f, 300.0f, 1.0f), 120.0f));
    flag("hostsync", "Темп и размер из проекта", true);
    choice("bars", "Тактов", {"1", "2", "4", "8", "16"}, 2);
    choice("fillevery", "Филл каждые", {U("без филлов"), U("2 такта"), U("4 такта"), U("8 тактов")}, 2);
    choice("filllen", "Длина филла", {U("доля"), U("две доли"), U("такт"), U("два такта")}, 2);
    integer("complexity", "Сложность", 0, 10, 5);
    integer("feel", "Живость", 0, 10, 3);
    integer("swing", "Свинг", 0, 100, 0);
    choice("poly", "Полиритм", polys, 0);
    integer("polyn", "Полиритм: N", 2, 23, 5);
    choice("tonic", "Строй литавр", {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}, 0);
    flag("landing", "Приземление после филла", true);
    integer("gvar", "Вариант грува", 0, 99999, 0);
    integer("fvar", "Вариант филла", 0, 99999, 0);
    integer("bvar", "Вариант брейка", 0, 99999, 0);
    static const char* lockNames[] = {"Закрепить низ", "Закрепить малый", "Закрепить тарелки", "Закрепить томы"};
    for (int i = 0; i < drums::NUM_LAYERS; ++i) {
        flag(LOCK_IDS[i], lockNames[i], false);
        integer(LVAR_IDS[i], lockNames[i], -1, 99999, -1);
    }
    integer("card", "Карточка", 0, kNumKinds - 1, 0);
    return l;
}

DrumProcessor::DrumProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "DrumArranger", layout()) {
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) params.addParameterListener(r->getParameterID(), this);
    regenerate();
}

DrumProcessor::~DrumProcessor() {
    cancelPendingUpdate();
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) params.removeParameterListener(r->getParameterID(), this);
}

bool DrumProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DrumProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    synth_.prepare(sr);
}

// the host may change a parameter from any thread: recompute on the message thread
void DrumProcessor::parameterChanged(const juce::String&, float) { triggerAsyncUpdate(); }
void DrumProcessor::handleAsyncUpdate() { regenerate(); }

void DrumProcessor::setParam(const char* id, float value) {
    if (auto* p = params.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
}

drums::Settings DrumProcessor::settings() const {
    auto v = [&](const char* id) { return (int)std::lround(params.getRawParameterValue(id)->load()); };
    drums::Settings s;
    s.style = v("style");
    s.meter.num = v("num");
    s.meter.den = DENS[juce::jlimit(0, 3, v("den"))];
    s.bpm = params.getRawParameterValue("tempo")->load();
    if (v("hostsync")) {
        if (hostBpm_.load() > 1.0) s.bpm = hostBpm_.load();
        if (hostNum_.load() > 0 && hostDen_.load() > 0) { s.meter.num = hostNum_.load(); s.meter.den = hostDen_.load(); }
    }
    s.bars = BAR_CHOICES[juce::jlimit(0, 4, v("bars"))];
    s.fillEvery = FILL_EVERY[juce::jlimit(0, 3, v("fillevery"))];
    s.fillLen = v("filllen");
    s.complexity = v("complexity");
    s.feel = v("feel");
    s.swing = v("swing");
    s.poly = v("poly");
    s.polyN = v("polyn");
    s.tonic = v("tonic");
    s.landing = v("landing") != 0;
    s.grooveVariant = v("gvar");
    s.fillVariant = v("fvar");
    s.breakVariant = v("bvar");
    for (int i = 0; i < drums::NUM_LAYERS; ++i) s.layerVariant[i] = v(LOCK_IDS[i]) ? v(LVAR_IDS[i]) : -1;
    s.follow = source_;
    return s;
}

drums::MidiOptions DrumProcessor::midiOptions() const {
    drums::MidiOptions o;
    o.map = (int)std::lround(params.getRawParameterValue("map")->load());
    o.perInstrument = params.getRawParameterValue("outmode")->load() > 0.5f;
    return o;
}

int DrumProcessor::selected() const { return (int)std::lround(params.getRawParameterValue("card")->load()); }
void DrumProcessor::select(int kind) { setParam("card", (float)kind); }

int DrumProcessor::variant(int kind) const {
    const char* id = kind == kFill ? "fvar" : kind == kBreak ? "bvar" : "gvar";
    return (int)std::lround(params.getRawParameterValue(id)->load());
}

void DrumProcessor::regenerate() {
    // a layer that has just been locked keeps the variant it sounds with now
    for (int i = 0; i < drums::NUM_LAYERS; ++i) {
        bool locked = params.getRawParameterValue(LOCK_IDS[i])->load() > 0.5f;
        int lv = (int)std::lround(params.getRawParameterValue(LVAR_IDS[i])->load());
        if (locked && lv < 0) setParam(LVAR_IDS[i], (float)variant(kGroove));
        if (!locked && lv >= 0) setParam(LVAR_IDS[i], -1.0f);
    }
    auto s = settings();
    usedBpm_ = s.bpm;
    usedNum_ = s.meter.num;
    usedDen_ = s.meter.den;
    static const drums::Kind kinds[kNumKinds] = {drums::Kind::Groove, drums::Kind::Fill, drums::Kind::Break, drums::Kind::Phrase};
    for (int k = 0; k < kNumKinds; ++k) results_[(size_t)k] = drums::generate(kinds[k], s);
    rebuildPreviewEvents();
    if (onResultChanged) onResultChanged();
}

void DrumProcessor::stepVariant(int kind, int dir) {
    const int which = kind == kFill ? 1 : kind == kBreak ? 2 : 0;
    const drums::Kind dk = kind == kFill ? drums::Kind::Fill : kind == kBreak ? drums::Kind::Break : drums::Kind::Groove;
    auto s = settings();
    int* field = which == 1 ? &s.fillVariant : which == 2 ? &s.breakVariant : &s.grooveVariant;
    const uint32_t now = drums::soundHash(drums::generate(dk, s));
    int v = *field;
    for (int tries = 0; tries < 60; ++tries) {
        v += dir;
        if (v < 0) { v = 0; break; }
        *field = v;
        if (drums::soundHash(drums::generate(dk, s)) != now) break;
    }
    setParam(VAR_IDS[which], (float)v);
}

juce::File DrumProcessor::writeFile(int kind) const {
    // a new folder for every drag: a host that remembers a file by its path never gets an old pattern
    auto base = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("DrumArranger");
    auto dir = base.getChildFile("drag" + juce::String(juce::Time::currentTimeMillis()) + "_" + juce::String(++dragCounter_));
    dir.createDirectory();
    static const char* names[kNumKinds] = {"Groove", "Fill", "Break", "Phrase"};
    auto s = settings();
    auto f = dir.getChildFile(juce::String("Drum ") + names[kind] + " " + drums::styleId(s.style) + ".mid");
    auto bytes = drums::writeMidi(result(kind), midiOptions());
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

bool DrumProcessor::isMidiFile(const juce::File& f) {
    juce::FileInputStream in(f);
    if (!in.openedOk()) return false;
    char head[4] = {0, 0, 0, 0};
    return in.read(head, 4) == 4 && std::memcmp(head, "MThd", 4) == 0;
}

bool DrumProcessor::loadSource(const juce::File& f, juce::String& err) {
    juce::MemoryBlock mb;
    std::vector<drums::SourceNote> notes;
    drums::Meter m;
    double bpm = 120;
    if (!f.loadFileAsData(mb) || !drums::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, m, bpm)) {
        err = U("Это не MIDI-файл: ") + f.getFileName() + U(". Перетащите файл .mid из Finder или экспортируйте регион из Logic (File → Export → Selection as MIDI File)");
        return false;
    }
    if (notes.empty()) {
        err = U("В файле нет нот: ") + f.getFileName();
        return false;
    }
    source_ = notes;
    sourceName_ = f.getFileName();
    regenerate();
    return true;
}

void DrumProcessor::clearSource() {
    source_.clear();
    sourceName_ = {};
    regenerate();
}

// ---------------------------------------------------------------- preview
void DrumProcessor::rebuildPreviewEvents() {
    const auto& r = result(selected());
    const int map = midiOptions().map;
    std::vector<PEvent> ev;
    for (auto& h : r.hits) {
        // the landing is the downbeat of the next bar: in a loop that is the start
        double t = h.tick >= r.length ? 0.0 : (double)h.tick;
        int note = h.role == drums::Timpani && h.pitch >= 0 ? h.pitch : drums::noteFor(map, h.role);
        ev.push_back({t, h.role, h.vel, h.pitch, note, true});
        ev.push_back({std::min((double)r.length - 1, t + h.len), h.role, 0, h.pitch, note, false});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const PEvent& a, const PEvent& b) { return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on); });
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewEvents_.swap(ev);
    previewLenTicks_ = r.length;
    previewBpm_ = r.bpm;
    previewIdx_ = 0;
    while (previewIdx_ < previewEvents_.size() && previewEvents_[previewIdx_].tick < previewTicks_.load()) ++previewIdx_;
}

void DrumProcessor::setPreview(bool on) {
    const juce::SpinLock::ScopedLockType l(previewLock_);
    previewTicks_ = 0;
    previewIdx_ = 0;
    previewOn_ = on && !previewEvents_.empty();
    killNotes_ = true;
}

void DrumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    buffer.clear();
    const int n = buffer.getNumSamples();
    midi.clear();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            bool changed = false;
            if (auto b = pos->getBpm()) { if (std::abs(*b - hostBpm_.load()) > 0.01) { hostBpm_ = *b; changed = true; } }
            if (auto ts = pos->getTimeSignature()) {
                if (ts->numerator != hostNum_.load() || ts->denominator != hostDen_.load()) { hostNum_ = ts->numerator; hostDen_ = ts->denominator; changed = true; }
            }
            if (changed) triggerAsyncUpdate();
        }

    if (killNotes_.exchange(false)) {
        synth_.allOff();
        midi.addEvent(juce::MidiMessage::allNotesOff(10), 0);
    }
    int rendered = 0;
    if (previewOn_.load()) {
        const juce::SpinLock::ScopedTryLockType l(previewLock_);
        if (l.isLocked() && previewLenTicks_ > 0) {
            const double ticksPerSample = previewBpm_ * drums::TPB / 60.0 / sampleRate_;
            double t0 = previewTicks_.load();
            int done = 0;
            while (done < n) {
                // up to the end of the loop, then wrap
                int chunk = std::min(n - done, std::max(1, (int)std::ceil((previewLenTicks_ - t0) / ticksPerSample)));
                double t1 = t0 + chunk * ticksPerSample;
                while (previewIdx_ < previewEvents_.size() && previewEvents_[previewIdx_].tick < t1) {
                    auto& e = previewEvents_[previewIdx_++];
                    int at = done + juce::jlimit(0, chunk - 1, (int)((e.tick - t0) / ticksPerSample));
                    int ch = e.role == drums::Timpani ? 1 : 10;
                    if (e.on) {
                        synth_.render(buffer, rendered, at - rendered);   // sample-accurate onsets
                        rendered = at;
                        synth_.trigger(e.role, e.vel, e.pitch);
                        midi.addEvent(juce::MidiMessage::noteOn(ch, e.note, (juce::uint8)juce::jlimit(1, 127, e.vel)), at);
                    } else midi.addEvent(juce::MidiMessage::noteOff(ch, e.note), at);
                }
                done += chunk;
                t0 = t1;
                if (t0 >= previewLenTicks_) { t0 -= previewLenTicks_; previewIdx_ = 0; }
            }
            previewTicks_ = t0;
        }
    }
    synth_.render(buffer, rendered, n - rendered);
}

// ---------------------------------------------------------------- state
void DrumProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto xml = std::make_unique<juce::XmlElement>("DrumArrangerState");
    xml->addChildElement(params.copyState().createXml().release());
    auto* src = xml->createNewChildElement("Source");
    src->setAttribute("name", sourceName_);
    juce::String s;
    for (auto& nt : source_) s << nt.start << ' ' << nt.end << ' ' << nt.pitch << ' ' << nt.vel << ';';
    src->setAttribute("notes", s);
    copyXmlToBinary(*xml, dest);
}

void DrumProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml) return;
    if (auto* st = xml->getChildByName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*st));
    source_.clear();
    sourceName_ = {};
    if (auto* src = xml->getChildByName("Source")) {
        sourceName_ = src->getStringAttribute("name");
        for (auto& tok : juce::StringArray::fromTokens(src->getStringAttribute("notes"), ";", "")) {
            auto f = juce::StringArray::fromTokens(tok, " ", "");
            if (f.size() >= 4) source_.push_back({f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue()});
        }
    }
    triggerAsyncUpdate();   // the host may call this off the message thread
}

juce::AudioProcessorEditor* DrumProcessor::createEditor() { return new DrumEditor(*this); }

#ifndef DRUMS_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumProcessor(); }
#endif
