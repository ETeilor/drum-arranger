// Engine checks: every style in every kind, meter and tempo; playability; determinism; density;
// how many fills and breaks really differ; the arithmetic of cycles and tihais; MIDI round trip.
#include <cstdio>
#include <set>
#include <string>
#include "Drums.h"

using namespace drums;

static int failures = 0;
static void expect(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL %s\n", what.c_str()); ++failures; }
}
static void note(const std::string& what) { std::printf("  %s\n", what.c_str()); }

int main() {
    const Meter meters[] = {{4, 4}, {3, 4}, {5, 4}, {6, 8}, {7, 8}, {12, 8}, {5, 8}, {9, 16}, {13, 16}, {2, 2}, {11, 8}};
    const double tempos[] = {70, 120, 190};
    const Kind kinds[] = {Kind::Groove, Kind::Fill, Kind::Break, Kind::Phrase};

    // 1. the sweep
    long cases = 0, problems = 0;
    for (int st = 0; st < numStyles(); ++st)
        for (auto& m : meters)
            for (double bpm : tempos)
                for (int c = 0; c <= 10; c += 2)
                    for (Kind k : kinds) {
                        Settings s;
                        s.style = st, s.meter = m, s.bpm = bpm, s.complexity = c, s.bars = 4;
                        s.fillVariant = c + (int)bpm, s.breakVariant = c + st;
                        Result r = generate(k, s);
                        ++cases;
                        std::string id = std::string(styleId(st)) + " " + std::to_string(m.num) + "/" + std::to_string(m.den) +
                                         " bpm " + std::to_string((int)bpm) + " c" + std::to_string(c) + " kind " + std::to_string((int)k);
                        if (!r.ok() || r.hits.empty()) { expect(false, id + ": пусто или ошибка " + r.error); continue; }
                        bool sane = true;
                        for (auto& h : r.hits) sane = sane && h.tick >= 0 && h.tick <= r.length && h.vel >= 1 && h.vel <= 127 && h.len >= 1;
                        expect(sane, id + ": нота вне региона или громкости");
                        auto pp = playabilityProblems(r, styleKit(st));
                        if (!pp.empty()) { ++problems; if (problems <= 12) expect(false, id + ": " + pp[0]); }
                    }
    note("прогон: " + std::to_string(cases) + " случаев, неиграбельных " + std::to_string(problems));
    expect(problems == 0, "все партии установки играются двумя руками и двумя ногами");

    // 2. determinism
    {
        Settings s;
        s.style = findStyle("funk"), s.complexity = 8, s.grooveVariant = 5;
        Result a = generate(Kind::Phrase, s), b = generate(Kind::Phrase, s);
        expect(writeMidi(a, {}) == writeMidi(b, {}), "одни настройки дают один и тот же файл");
    }

    // 3. density grows with complexity
    for (int st = 0; st < numStyles(); ++st) {
        int prev = -1, first = 0, last = 0;
        bool mono = true;
        for (int c = 0; c <= 10; ++c) {
            Settings s;
            s.style = st, s.complexity = c, s.feel = 0;
            int n = (int)generate(Kind::Groove, s).hits.size();
            if (c == 0) first = n;
            last = n;
            if (prev >= 0 && n < prev * 0.9 - 2) mono = false;
            prev = n;
        }
        expect(last >= first, std::string(styleId(st)) + ": на сложности 10 нот не меньше, чем на 0 (" + std::to_string(first) + " -> " + std::to_string(last) + ")");
        expect(mono, std::string(styleId(st)) + ": плотность не проваливается с ростом сложности");
    }

    // 4. groove variants differ
    for (int st = 0; st < numStyles(); ++st) {
        std::set<uint32_t> seen;
        for (int v = 0; v < 20; ++v) {
            Settings s;
            s.style = st, s.complexity = 7, s.grooveVariant = v, s.feel = 0;
            seen.insert(soundHash(generate(Kind::Groove, s)));
        }
        expect(seen.size() >= 4, std::string(styleId(st)) + ": из 20 вариантов грува разных " + std::to_string(seen.size()));
    }

    // 5. how many fills and breaks really differ
    {
        size_t totalFills = 0;
        for (int len = 0; len <= 3; ++len) {
            std::set<uint32_t> seen;
            for (int v = 0; v < 2000; ++v) {
                Settings s;
                s.style = findStyle("rock8"), s.complexity = 10, s.fillLen = len, s.fillVariant = v, s.feel = 0;
                Result r = generate(Kind::Fill, s);
                // only the fill itself: the groove before it is the same in every variant
                seen.insert(soundHash(r));
            }
            note("филлы длины " + std::to_string(len) + ": из 2000 вариантов разных " + std::to_string(seen.size()));
            totalFills += seen.size();
        }
        expect(totalFills >= 2000, "разных филлов на сложности 10 не меньше 2000, получилось " + std::to_string(totalFills));
        std::set<uint32_t> low;
        for (int v = 0; v < 400; ++v) {
            Settings s;
            s.style = findStyle("rock8"), s.complexity = 5, s.fillVariant = v, s.feel = 0;
            low.insert(soundHash(generate(Kind::Fill, s)));
        }
        note("филлы на сложности 5, такт: из 400 разных " + std::to_string(low.size()));
        expect(low.size() >= 200, "на сложности 5 разных филлов не меньше 200");
        std::set<uint32_t> br;
        for (int v = 0; v < 200; ++v) {
            Settings s;
            s.style = findStyle("rock8"), s.complexity = 10, s.breakVariant = v, s.feel = 0;
            br.insert(soundHash(generate(Kind::Break, s)));
        }
        note("брейки: из 200 вариантов разных " + std::to_string(br.size()));
        expect(br.size() >= 20, "разных брейков не меньше 20");
    }

    // 6. arithmetic
    expect(meterGrouping({7, 8}) == "7/8 = 2+2+3", "7/8 = 2+2+3, получилось " + meterGrouping({7, 8}));
    expect(meterGrouping({5, 8}) == "5/8 = 2+3", "5/8 = 2+3");
    expect(meterGrouping({12, 8}) == "12/8 = 3+3+3+3", "12/8 = 3+3+3+3");
    expect(meterGrouping({9, 16}) == "9/16 = 3+3+3", "9/16, получилось " + meterGrouping({9, 16}));
    expect(meterGrouping({13, 16}) == "13/16 = 4+4+2+3", "13/16, получилось " + meterGrouping({13, 16}));
    expect(realignBars(5, {4, 4}) == 5 && realignBars(23, {4, 4}) == 23 && realignBars(8, {4, 4}) == 1 && realignBars(6, {4, 4}) == 3,
           "цикл против такта: 5 -> 5 тактов, 23 -> 23, 8 -> 1, 6 -> 3");
    expect(euclid(3, 8) == "x..x..x.", "E(3,8) = x..x..x., получилось " + euclid(3, 8));
    expect(euclid(5, 16) == "x...x..x..x..x.." || euclid(5, 16) == "x..x..x..x..x...", "E(5,16), получилось " + euclid(5, 16));
    for (int n = 2; n <= 23; ++n)
        for (int k = 1; k <= n; ++k) {
            std::string e = euclid(k, n);
            int cnt = 0;
            for (char ch : e) cnt += ch == 'x';
            if (cnt != k || e[0] != 'x') expect(false, "E(" + std::to_string(k) + "," + std::to_string(n) + ")");
        }
    {
        Settings s;
        s.style = findStyle("rock8"), s.poly = P_TALA, s.polyN = 4, s.fillLen = 2, s.feel = 0;
        Result r = generate(Kind::Fill, s);
        expect(r.report.find("3·5 + 2·1 = 17") != std::string::npos, "тихаи в 4/4 на такт: 5-1-5-1-5 = 17\n" + r.report);
        bool lands = false;
        for (auto& h : r.hits) lands = lands || (h.tick == r.length && h.role == Kick);
        expect(lands, "тихаи заканчивается на первой доле следующего такта");
        for (int gati : {3, 5, 7}) {
            s.polyN = gati;
            Result g = generate(Kind::Fill, s);
            expect(g.report.find("Тихаи") != std::string::npos, "тихаи в гати " + std::to_string(gati));
        }
    }
    {
        // feet in a cycle of 23 sixteenths against 4/4: five cycles and a tail of 13 fill eight bars
        Settings s;
        s.style = findStyle("metal8"), s.poly = P_FEET, s.polyN = 23, s.bars = 8, s.feel = 0;
        Result r = generate(Kind::Groove, s);
        std::set<int> first, second;
        for (auto& h : r.hits)
            if (h.role == Kick) {
                int st = h.grid / STEP;
                if (st < 23) first.insert(st);
                else if (st < 46) second.insert(st - 23);
            }
        expect(!first.empty() && first == second, "цикл ног повторяется каждые 23 шестнадцатых");
        expect(r.report.find("5 полных циклов и хвост 13") != std::string::npos, "8 тактов = 5·23 + 13\n" + r.report);
    }
    {
        Settings s;
        s.style = findStyle("rock8"), s.poly = P_CROSS, s.polyN = 3, s.bars = 1, s.feel = 0;
        Result r = generate(Kind::Groove, s);
        std::vector<int> t;
        for (auto& h : r.hits) if (h.role == Cowbell) t.push_back(h.grid);
        expect(t.size() == 3 && t[0] == 0 && t[1] == 640 && t[2] == 1280, "3 на такт 4/4: удары через 640 тиков");
    }
    {
        Settings s;
        s.style = findStyle("rock8"), s.poly = P_MODULATION, s.polyN = 3, s.bars = 4, s.bpm = 120;
        Result r = generate(Kind::Groove, s);
        expect(r.report.find("160 BPM") != std::string::npos, "модуляция на пульс в три шестнадцатых: 120 -> 160\n" + r.report);
    }

    // 7. MIDI
    {
        Settings s;
        s.style = findStyle("breakdown"), s.complexity = 8;
        Result r = generate(Kind::Phrase, s);
        MidiOptions one, per;
        per.perInstrument = true;
        auto bytes = writeMidi(r, one);
        std::vector<SourceNote> back;
        Meter m;
        double bpm = 0;
        expect(readMidi(bytes.data(), bytes.size(), back, m, bpm), "записанный файл читается");
        expect(back.size() == r.hits.size(), "нот в файле столько же, сколько ударов: " + std::to_string(back.size()) + " и " + std::to_string(r.hits.size()));
        expect(m.num == 4 && m.den == 4 && std::abs(bpm - 120.0) < 0.01, "размер и темп в файле");
        std::set<int> roles;
        for (auto& h : r.hits) roles.insert(h.role);
        auto pb = writeMidi(r, per);
        int tracks = (pb[10] << 8) | pb[11];
        expect(tracks == (int)roles.size() + 1, "дорожка на инструмент: дорожек " + std::to_string(tracks - 1) + ", инструментов " + std::to_string(roles.size()));
        expect(noteFor(M_GM, Kick) == 36 && noteFor(M_GM, Snare) == 38 && noteFor(M_GM, HatClosed) == 42, "GM: бочка 36, малый 38, хэт 42");
        expect(noteFor(M_DUPLANTIER, China) == 54 && noteFor(M_DUPLANTIER, RideBell) == 53 && noteFor(M_DUPLANTIER, PercHigh) == 12, "MixWave: чайна F#2, колокол F2, тимбалес C-1");
        expect(noteFor(M_BBCSO, BassDrum) == 48 && noteFor(M_BBCSO, Kick) == 48 && noteFor(M_BBCSO, OrchSnare) == 52, "BBC SO: бас-барабан C2, малый E2");
        for (int map = 0; map < NUM_MAPS; ++map)
            for (int role = 0; role < NUM_ROLES; ++role) {
                int n = noteFor(map, role);
                if (n < 0 || n > 127) expect(false, "нота вне диапазона");
            }
    }
    {
        // timpani keep their pitch and their own track
        Settings s;
        s.style = findStyle("timpani"), s.tonic = 2, s.complexity = 6;
        Result r = generate(Kind::Groove, s);
        bool tonic = false, dom = false;
        for (auto& h : r.hits)
            if (h.role == Timpani) { tonic = tonic || h.pitch % 12 == 2; dom = dom || h.pitch % 12 == 9; expect(h.pitch >= 40 && h.pitch <= 53, "литавра в диапазоне"); }
        expect(tonic && dom, "литавры: тоника ре и доминанта ля");
        auto bytes = writeMidi(r, {});
        expect(((bytes[10] << 8) | bytes[11]) == 3, "литавры на своей дорожке");
    }
    {
        // the kick follows a rhythm source
        Settings s;
        s.style = findStyle("rock8"), s.bars = 1, s.feel = 0;
        s.follow = {{0, 100, 40, 90}, {360, 400, 40, 90}, {1200, 1300, 43, 90}};
        Result r = generate(Kind::Groove, s);
        std::set<int> kicks;
        for (auto& h : r.hits) if (h.role == Kick) kicks.insert(h.grid);
        expect(kicks == std::set<int>({0, 360, 1200}), "бочка стоит на атаках источника");
    }
    {
        Settings s;
        s.style = findStyle("rock8");
        LiveBars lb = liveBars(s);
        bool landing = false;
        for (auto& h : lb.fill) landing = landing || h.tick == lb.barTicks;
        expect(lb.groove.size() == 4 && lb.barTicks == 1920 && landing && !lb.brk.empty(), "живой режим: четыре такта грува, филл с приземлением, брейк");
    }
    {
        Settings s;
        s.meter = {4, 3};
        expect(!generate(Kind::Groove, s).ok(), "размер 4/3 даёт понятную ошибку");
    }

    std::printf(failures ? "FAILED: %d\n" : "all passed, failures: %d\n", failures);
    return failures ? 1 : 0;
}
