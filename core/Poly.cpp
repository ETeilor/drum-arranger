// Polyrhythm layers over a groove, and the tihai.
#include <numeric>
#include "Internal.h"

namespace drums {

namespace {

Hit mk(int tick, int role, int vel) {
    Hit h;
    h.tick = h.grid = tick;
    h.role = role;
    h.vel = std::clamp(vel, 1, 127);
    h.len = defaultLen(role);
    return h;
}

// the extra voice a cross-rhythm is played on
int crossVoice(int kit) {
    switch (kit) {
        case K_METAL: return RideBell;
        case K_CINEMATIC: return TomHigh;
        case K_ORCHESTRAL: return Triangle;
        case K_ELECTRONIC: return PercHigh;
        default: return Cowbell;
    }
}

}  // namespace

void applyPoly(const Settings& s, const Bar& bar, int kit, int nBars, std::vector<Hit>& hits, std::string& report) {
    const int N = s.polyN, c = s.complexity, BT = bar.ticks(), total = nBars * bar.steps;
    const KitRoles k = kitRoles(kit);
    if (s.poly == P_FEET) {
        // the feet repeat a cycle of N sixteenths; the hands stay in the meter and hold the bar together
        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.role == Kick || h.role == k.low; }), hits.end());
        int strokes = std::clamp((N * (3 + c)) / 20, 1, N);
        std::string pat = euclid(strokes, N);
        for (int st = 0; st < total; ++st)
            if (pat[(size_t)(st % N)] == 'x') hits.push_back(mk(st * STEP, k.low, st % N == 0 ? 108 : 98));
        int re = realignBars(N, s.meter);
        report += "Цикл ног: " + std::to_string(N) + " шестнадцатых (" + pat + "), совпадает с тактом через " + std::to_string(re) +
                  " т. Здесь " + std::to_string(total / N) + " полных циклов";
        if (total % N) report += " и хвост " + std::to_string(total % N) + " шестнадцатых, со следующего повтора цикл начинается заново";
        report += "\n";
    } else if (s.poly == P_CROSS) {
        int voice = crossVoice(kit);
        for (int b = 0; b < nBars; ++b)
            for (int i = 0; i < N; ++i) hits.push_back(mk(b * BT + (int)std::lround((double)i * BT / N), voice, i == 0 ? 104 : 88));
        report += std::to_string(N) + " ровных ударов на такт поверх размера, голос: " + roleName(voice) + "\n";
    } else if (s.poly == P_GROUPS) {
        // accents every N sixteenths run over the bar lines; from complexity 5 the kick marks them too
        for (auto& h : hits) {
            if (roleLayer(h.role) != L_CYMBALS || h.grid % STEP) continue;
            bool acc = (h.grid / STEP) % N == 0;
            h.vel = acc ? 112 : std::min(h.vel, 66);
        }
        if (c >= 5)
            for (int st = 0; st < total; st += N) hits.push_back(mk(st * STEP, k.low, 102));
        report += "Акценты каждые " + std::to_string(N) + " шестнадцатых идут через тактовую черту, совпадают с тактом через " +
                  std::to_string(realignBars(N, s.meter)) + " т.\n";
    } else if (s.poly == P_EUCLID) {
        int voice = crossVoice(kit);
        std::string pat = euclid(std::min(N, bar.steps), bar.steps);
        for (int b = 0; b < nBars; ++b)
            for (int st = 0; st < bar.steps; ++st)
                if (pat[(size_t)st] == 'x') hits.push_back(mk(b * BT + st * STEP, voice, st == 0 ? 102 : 86));
        report += "Евклид E(" + std::to_string(std::min(N, bar.steps)) + "," + std::to_string(bar.steps) + ") = " + pat + ", голос: " +
                  roleName(voice) + "\n";
    }
}

// A tihai: a phrase of p units three times with two equal gaps of g units; its last stroke is the
// downbeat after the fill. Counting that stroke: 3p + 2g = units + 1.
bool buildTihai(const FillEnv& e) {
    const Settings& s = e.s;
    int n = s.polyN;
    if (n != 3 && n != 4 && n != 5 && n != 6 && n != 7 && n != 9) n = 4;
    if (s.bpm / 60.0 * n > 16.0) n = 4;
    if (s.bpm / 60.0 * n > 16.0) return false;
    const int U = (int)std::lround((double)e.lenTicks * n / TPB);
    struct PG { int p, g; };
    std::vector<PG> withGap, noGap;
    for (int g = 0; g <= 2 * n && g < U; ++g) {
        int rem = U + 1 - 2 * g;
        if (rem < 6 || rem % 3) continue;
        (g ? withGap : noGap).push_back({rem / 3, g});
    }
    withGap.insert(withGap.end(), noGap.begin(), noGap.end());
    if (withGap.empty()) return false;
    const int v = std::max(0, e.variant);
    const PG pg = withGap[(size_t)(v % (int)withGap.size())];
    const bool threes = (v / (int)withGap.size()) % 2 == 1;

    // the phrase as groups of 2..5 units; each group is one konnakol word
    std::vector<int> groups;
    for (int rem = pg.p; rem > 0;) {
        int take;
        if (rem <= 5) take = rem;
        else if (threes) take = (rem - 3 == 1) ? 4 : 3;
        else take = (rem - 4 == 1) ? 3 : 4;
        groups.push_back(take);
        rem -= take;
    }
    static const char* words[] = {"", "ta", "ta ka", "ta ki ta", "ta ka di mi", "ta di gi na thom"};
    const KitRoles k = kitRoles(e.kit);
    const int toms[3] = {k.t1, k.t2, k.t3};
    auto tickOf = [&](int u) { return e.base + (int)std::lround((double)u * e.lenTicks / U); };
    std::string syl;
    for (int rep = 0; rep < 3; ++rep) {
        int u = rep * (pg.p + pg.g);
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            for (int i = 0; i < groups[gi]; ++i, ++u) {
                if (u == U) continue;                                  // the last stroke is the landing
                bool first = i == 0;
                e.out->push_back(mk(tickOf(u), first ? toms[rep] : k.snare, first ? 110 : 72 + 6 * rep));
            }
            if (rep == 0) syl += std::string(gi ? " | " : "") + words[groups[gi]];
        }
        if (rep < 2 && pg.g >= 1) e.out->push_back(mk(tickOf(u), k.low, 94));   // "tum" opens the gap
    }
    addLanding(s, e.kit, e.base + e.lenTicks, *e.out);
    *e.report += "Тихаи №" + std::to_string(v + 1) + ": фраза " + std::to_string(pg.p) + " × 3, пауза " + std::to_string(pg.g) +
                 " × 2, вместе с ударом на первую долю 3·" + std::to_string(pg.p) + " + 2·" + std::to_string(pg.g) + " = " +
                 std::to_string(U + 1) + " (гати " + std::to_string(n) + ")\nСлоги фразы: " + syl + "\n";
    return true;
}

}  // namespace drums
