// Plug-in level tests without a host: every style, state round trip, variants and locks, preview audio,
// host tempo, the rhythm source, the live MIDI FX, and editor snapshots.
#include <juce_audio_utils/juce_audio_utils.h>
#include "LiveEditor.h"
#include "LiveProcessor.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

struct FakeHead : juce::AudioPlayHead {
    bool playing = false;
    double ppq = 0, bpm = 96;
    int num = 4, den = 4;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{num, den});
        return p;
    }
};

static int failures = 0;
static void expect(bool ok, const juce::String& text) {
    // plain literals arrive as Latin-1: put the bytes back together as UTF-8
    std::string raw;
    bool narrow = true;
    for (auto ch : text) { if (ch > 255) { narrow = false; break; } raw += (char)ch; }
    const juce::String what = narrow ? juce::String::fromUTF8(raw.c_str()) : text;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++failures;
}
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static void set(juce::AudioProcessorValueTreeState& ps, const char* id, float v) {
    auto* p = ps.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(v));
}
static void shot(juce::AudioProcessorEditor& ed, const juce::File& dir, const juce::String& name) {
    pump(120);
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 2.0f);
    auto out = dir.getChildFile(name);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    os.flush();
    expect(out.getSize() > 1000, "снимок интерфейса " + name);
}
static int countNotes(const juce::MidiBuffer& mb, int note = -1) {
    int n = 0;
    for (const auto m : mb) if (m.getMessage().isNoteOn() && (note < 0 || m.getMessage().getNoteNumber() == note)) ++n;
    return n;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File outDir(argc > 1 ? argv[1] : ".");
    outDir.createDirectory();
    const double sr = 48000;
    juce::AudioBuffer<float> buf(2, 512);

    // 1. every style builds all four cards and writes a file that reads back
    {
        DrumProcessor p;
        p.prepareToPlay(sr, 512);
        int bad = 0;
        for (int st = 0; st < drums::numStyles(); ++st) {
            set(p.params, "style", (float)st);
            p.regenerate();
            for (int k = 0; k < DrumProcessor::kNumKinds; ++k) {
                if (!p.result(k).ok() || p.result(k).hits.empty()) { ++bad; continue; }
                auto f = p.writeFile(k);
                juce::MemoryBlock mb;
                std::vector<drums::SourceNote> notes;
                drums::Meter m;
                double bpm = 0;
                if (!f.loadFileAsData(mb) || !drums::readMidi((const unsigned char*)mb.getData(), mb.getSize(), notes, m, bpm) ||
                    notes.size() != p.result(k).hits.size()) ++bad;
            }
        }
        expect(bad == 0, juce::String::fromUTF8("все стили дают четыре карточки и читаемые файлы, стилей ") + juce::String(drums::numStyles()));
    }

    // 2. state round trip, variants, locks
    {
        DrumProcessor a;
        a.prepareToPlay(sr, 512);
        set(a.params, "style", (float)drums::findStyle("funk"));
        set(a.params, "complexity", 8);
        set(a.params, "num", 7);
        set(a.params, "den", 2);          // 7/8
        set(a.params, "hostsync", 0);
        a.regenerate();
        const auto before = drums::soundHash(a.result(DrumProcessor::kGroove));
        a.stepVariant(DrumProcessor::kGroove, 1);
        a.regenerate();
        expect(drums::soundHash(a.result(DrumProcessor::kGroove)) != before && a.variant(DrumProcessor::kGroove) > 0, "другой вариант грува звучит иначе");
        const auto fillBefore = drums::soundHash(a.result(DrumProcessor::kFill));
        a.stepVariant(DrumProcessor::kFill, 1);
        a.regenerate();
        expect(drums::soundHash(a.result(DrumProcessor::kFill)) != fillBefore, "другой вариант филла звучит иначе");

        auto lowOnly = [](const drums::Result& r) {
            uint32_t h = 1;
            for (auto& x : r.hits) if (drums::roleLayer(x.role) == drums::L_LOW) h = h * 31u + (uint32_t)x.grid;
            return h;
        };
        set(a.params, "lock0", 1);
        a.regenerate();
        const auto low = lowOnly(a.result(DrumProcessor::kGroove));
        const auto whole = drums::soundHash(a.result(DrumProcessor::kGroove));
        a.stepVariant(DrumProcessor::kGroove, 1);
        a.regenerate();
        expect(lowOnly(a.result(DrumProcessor::kGroove)) == low && drums::soundHash(a.result(DrumProcessor::kGroove)) != whole,
               "закреплённый низ остаётся, остальное меняется");

        juce::MemoryBlock state;
        a.getStateInformation(state);
        DrumProcessor b;
        b.prepareToPlay(sr, 512);
        b.setStateInformation(state.getData(), (int)state.getSize());
        pump(100);
        expect(drums::soundHash(b.result(DrumProcessor::kGroove)) == drums::soundHash(a.result(DrumProcessor::kGroove)) &&
               drums::soundHash(b.result(DrumProcessor::kFill)) == drums::soundHash(a.result(DrumProcessor::kFill)) &&
               b.result(0).meter.num == 7 && b.result(0).meter.den == 8, "состояние восстанавливается нота в ноту");
    }

    // 3. preview audio and MIDI out, looping
    {
        DrumProcessor p;
        p.prepareToPlay(sr, 512);
        set(p.params, "hostsync", 0);
        set(p.params, "bars", 0);     // one bar: two seconds at 120
        p.regenerate();
        p.setPreview(true);
        float peak = 0;
        int notes = 0;
        for (int i = 0; i < 470; ++i) {   // about five seconds: the loop comes round twice
            juce::MidiBuffer mo;
            p.processBlock(buf, mo);
            peak = std::max(peak, buf.getMagnitude(0, 512));
            notes += countNotes(mo);
        }
        const int perBar = (int)p.result(DrumProcessor::kGroove).hits.size();
        expect(peak > 0.02f && peak < 1.5f, "прослушивание даёт звук, пик " + juce::String(peak, 3));
        expect(notes >= 2 * perBar && notes <= 3 * perBar, U("прослушивание идёт по кругу и отдаёт ноты на выход: ") + juce::String(notes) + U(" при ") + juce::String(perBar) + U(" в такте"));
        p.setPreview(false);
    }

    // 4. tempo and meter from the host
    {
        DrumProcessor p;
        p.prepareToPlay(sr, 512);
        FakeHead head;
        head.bpm = 93;
        head.num = 6, head.den = 8;
        p.setPlayHead(&head);
        juce::MidiBuffer mo;
        p.processBlock(buf, mo);
        pump(100);
        auto& r = p.result(DrumProcessor::kGroove);
        expect(std::abs(r.bpm - 93.0) < 0.01 && r.meter.num == 6 && r.meter.den == 8, "темп и размер берутся из проекта");
        set(p.params, "hostsync", 0);
        p.regenerate();
        expect(std::abs(p.result(0).bpm - 120.0) < 0.01 && p.result(0).meter.num == 4, "без синхронизации работают свои темп и размер");
    }

    // 5. rhythm source
    {
        DrumProcessor p;
        p.prepareToPlay(sr, 512);
        drums::Result src;
        src.length = 1920;
        for (int t : {0, 360, 840, 1200, 1560}) { drums::Hit h; h.tick = h.grid = t; h.role = drums::Kick; src.hits.push_back(h); }
        auto bytes = drums::writeMidi(src, {});
        auto f = outDir.getChildFile("source_test.mid");
        f.replaceWithData(bytes.data(), bytes.size());
        juce::String err;
        set(p.params, "feel", 0);
        set(p.params, "bars", 0);
        expect(p.loadSource(f, err), "источник ритма загружен " + err);
        std::vector<int> kicks;
        for (auto& h : p.result(DrumProcessor::kGroove).hits) if (h.role == drums::Kick) kicks.push_back(h.grid);
        expect(kicks == std::vector<int>({0, 360, 840, 1200, 1560}), "бочка стоит на атаках источника");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        DrumProcessor q;
        q.setStateInformation(state.getData(), (int)state.getSize());
        pump(100);
        expect(q.sourceNotes() == 5, "источник ритма сохраняется в проекте");
        expect(!p.loadSource(outDir.getChildFile("no_such_file.mid"), err) && err.isNotEmpty(), "плохой файл даёт понятное сообщение");
        f.deleteFile();
    }

    // 6. live MIDI FX
    {
        DrumLiveProcessor lp;
        lp.prepareToPlay(sr, 512);
        const int key = lp.firstKey();
        auto run = [&](int blocks, juce::MidiBuffer first = {}) {
            juce::MidiBuffer all;
            for (int i = 0; i < blocks; ++i) {
                juce::MidiBuffer mb;
                if (i == 0) mb = first;
                lp.processBlock(buf, mb);
                for (const auto m : mb) all.addEvent(m.getMessage(), i * 512 + m.samplePosition);
            }
            return all;
        };
        auto on = [&](int note, int vel) { juce::MidiBuffer mb; mb.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)vel), 0); return mb; };
        expect(countNotes(run(50)) == 0, "живой режим молчит, пока не нажата клавиша");
        auto g = run(190, on(key, 100));     // about two seconds: one bar at 120
        expect(countNotes(g, 36) >= 2 && countNotes(g, 38) >= 2 && countNotes(g, key) == 0, "клавиша грува запускает грув, сама клавиша не проходит");
        expect(lp.snapshot().mode == DrumLiveProcessor::mGroove, "состояние: грув");
        auto pass = run(4, on(key + 20, 90));
        expect(countNotes(pass, key + 20) == 1, "остальные клавиши проходят к инструменту");
        auto fl = run(400, on(key + 2, 110));
        int toms = countNotes(fl, 50) + countNotes(fl, 47) + countNotes(fl, 43);
        expect(toms >= 3 && countNotes(fl, 49) >= 1, "клавиша филла даёт филл и тарелку после него, томов " + juce::String(toms));
        auto quiet = run(8, on(key, 40));    // the groove key again: stop
        auto after = run(100);
        expect(countNotes(after) == 0 && lp.snapshot().mode == DrumLiveProcessor::mStopped, "повторное нажатие останавливает");
        auto soft = run(190, on(key, 12)), hard = (run(4, on(key + 5, 100)), run(190, on(key, 127)));
        expect(countNotes(hard) > countNotes(soft), U("сильное нажатие даёт более плотный грув: ") + juce::String(countNotes(soft)) + U(" и ") + juce::String(countNotes(hard)));
        run(4, on(key + 5, 100));
        expect(lp.recordedHits() > 20, "сыгранное записывается");
        auto take = lp.writeRecordingFile();
        expect(take.existsAsFile() && take.getSize() > 100, "сыгранное сохраняется в MIDI-файл");
        lp.clearRecording();
        expect(lp.recordedHits() == 0, "очистка сыгранного");

        // with the host playing the pattern sits on the host's bars
        FakeHead head;
        head.playing = true;
        head.bpm = 120;
        lp.setPlayHead(&head);
        juce::MidiBuffer all;
        const double ppqPerBlock = 512.0 / sr * head.bpm / 60.0;
        for (int i = 0; i < 400; ++i) {
            head.ppq = 6.0 + i * ppqPerBlock;      // start in the middle of a bar
            juce::MidiBuffer mb;
            if (i == 0) mb = on(key, 100);
            lp.processBlock(buf, mb);
            for (const auto m : mb) if (m.getMessage().isNoteOn() && m.getMessage().getNoteNumber() == 36 && m.getMessage().getVelocity() > 105)
                all.addEvent(m.getMessage(), (int)std::lround((head.ppq + m.samplePosition * ppqPerBlock / 512.0) * 1000.0));
        }
        bool onBars = all.getNumEvents() > 0;
        for (const auto m : all) { double ppq = m.samplePosition / 1000.0; if (std::abs(ppq / 4.0 - std::round(ppq / 4.0)) > 0.02) onBars = false; }
        expect(onBars, "при игре проекта сильная бочка стоит на первых долях его тактов");
        lp.setPlayHead(nullptr);

        std::unique_ptr<juce::AudioProcessorEditor> ed(lp.createEditor());
        shot(*ed, outDir, "ui_live.png");
    }

    // 7. editor snapshots
    {
        DrumProcessor p;
        p.prepareToPlay(sr, 512);
        struct Shot { const char* tag; const char* style; int card, poly, polyN, num, den; };
        const Shot shots[] = {{"rock", "rock8", 0, 0, 5, 4, 1}, {"metal_poly", "metal8", 0, drums::P_FEET, 7, 4, 1}, {"fill_tala", "funk", 1, drums::P_TALA, 5, 4, 1},
                              {"cinematic", "hybrid", 3, 0, 5, 12, 2}, {"orchestral", "timpani", 0, 0, 5, 3, 1}, {"idm", "glitch", 0, 0, 5, 7, 2}, {"break", "disco", 2, 0, 5, 4, 1}};
        for (auto& sh : shots) {
            set(p.params, "hostsync", 0);
            set(p.params, "style", (float)drums::findStyle(sh.style));
            set(p.params, "card", (float)sh.card);
            set(p.params, "poly", (float)sh.poly);
            set(p.params, "polyn", (float)sh.polyN);
            set(p.params, "num", (float)sh.num);
            set(p.params, "den", (float)sh.den);
            set(p.params, "complexity", 7);
            set(p.params, "lock0", std::string(sh.tag) == "metal_poly" ? 1.0f : 0.0f);
            p.regenerate();
            if (std::string(sh.tag) == "rock") {   // play a little so the playhead shows
                p.setPreview(true);
                juce::MidiBuffer mo;
                for (int i = 0; i < 120; ++i) p.processBlock(buf, mo);
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            shot(*ed, outDir, juce::String("ui_") + sh.tag + ".png");
            p.setPreview(false);
        }
    }

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
