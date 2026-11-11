// Fills from rudiments, and breaks.
// A fill is a point in a grid of choices: sticking (a rudiment), subdivision, the drums it is laid out
// on, the shape of the dynamics, a kick under the hands, a faster second half. The variant number walks
// that grid in a fixed order, so the same number is always the same fill.
#include <numeric>
#include "Internal.h"

namespace drums {

namespace {

struct Rud {
    const char* name;
    const char* st;   // R L hands, K kick; '>' accents the next stroke, 'f' flam, 'd' drag
    int minC;
};
const Rud RUDS[] = {
    {"одиночные удары", "RL", 0},
    {"двойки", "RRLL", 2},
    {"парадидл", ">RLRR>LRLL", 3},
    {"линейный, две руки и бочка", ">RLK", 3},
    {"группы по три", ">RLL", 4},
    {"парадидл-дидл", ">RLRRLL", 4},
    {"флэм-акцент", ">fRLR>fLRL", 4},
    {"линейный, три руки и бочка", ">RLRK", 4},
    {"линейный, четыре руки и две бочки", ">RLRLKK", 4},
    {"двойной парадидл", ">RLRLRR>LRLRLL", 5},
    {"обращённый парадидл", ">RLLR>LRRL", 5},
    {"ролл из шести ударов", ">RLLRR>L", 5},
    {"линейный, две руки и две бочки", ">RLKK", 5},
    {"флэм-тэп", ">fRR>fLL", 6},
    {"флэм-парадидл", ">fRLRR>fLRLL", 6},
    {"драг-тэп", "dR>LdL>R", 6},
    {"тройной парадидл", ">RLRLRLRR>LRLRLRLL", 6},
    {"швейцарский триоль", ">fRRL", 7},
    {"ратамакью", "dRLR>LdLRL>R", 7},
    {"группы по пять", ">RLRLL", 7},
};
const int NUM_RUDS = (int)(sizeof(RUDS) / sizeof(RUDS[0]));

struct Stroke {
    char hand;
    bool accent;
    int grace;
};
std::vector<Stroke> parse(const char* st) {
    std::vector<Stroke> out;
    bool acc = false;
    int grace = 0;
    for (const char* p = st; *p; ++p) {
        if (*p == '>') acc = true;
        else if (*p == 'f') grace = 1;
        else if (*p == 'd') grace = 2;
        else { out.push_back({*p, acc, grace}); acc = false; grace = 0; }
    }
    return out;
}

struct Sub {
    const char* name;
    int perQuarter, minC;
};
// the plain ones first: variant 0 is sixteenths
const Sub SUBS[] = {{"шестнадцатые", 4, 0}, {"восьмые", 2, 0}, {"триоли", 3, 2}, {"секстоли", 6, 4},
                    {"тридцать вторые", 8, 6}, {"квинтоли", 5, 8}, {"септоли", 7, 9}};

enum { O_DOWN, O_SNARE, O_ACC, O_FLOOR, O_UP, O_RL, O_ZIG, O_CYM, NUM_ORCH };
const char* ORCH_NAMES[] = {"сверху вниз по томам", "на малом", "акценты на томах", "малый и напольный том", "снизу вверх",
                            "правая по томам, левая на малом", "зигзаг", "акценты на тарелке с бочкой"};
const int ORCH_MINC[] = {0, 0, 3, 3, 2, 4, 5, 6};
const char* SHAPE_NAMES[] = {"нарастание", "ровно", "затихание с акцентом в конце"};

int coprimeStride(int total) {
    int s = std::max(1, (int)(total * 0.618));
    while (std::gcd(s, total) != 1) ++s;
    return s;
}

double maxRate(const Settings& s) { return s.bpm / 60.0; }   // quarters per second

Hit mk(int tick, int role, int vel, int len = 0) {
    Hit h;
    h.tick = h.grid = tick;
    h.role = role;
    h.vel = std::clamp(vel, 1, 127);
    h.len = len > 0 ? len : defaultLen(role);
    return h;
}

}  // namespace

void buildFill(const FillEnv& e) {
    const Settings& s = e.s;
    const int c = s.complexity;
    const KitRoles k = kitRoles(e.kit);
    const bool grouped = s.poly == P_FEET || s.poly == P_CROSS || s.poly == P_GROUPS || s.poly == P_EUCLID;

    std::vector<int> ruds, subs, orchs, shapes{0, 1};
    for (int i = 0; i < NUM_RUDS; ++i) if (RUDS[i].minC <= c) ruds.push_back(i);
    for (int i = 0; i < 7; ++i)
        if (SUBS[i].minC <= c && (maxRate(s) * SUBS[i].perQuarter <= 16.0 || SUBS[i].perQuarter == 2)) subs.push_back(i);
    for (int i = 0; i < NUM_ORCH; ++i) if (ORCH_MINC[i] <= c) orchs.push_back(i);
    if (c >= 3) shapes.push_back(2);
    const int nKick = c >= 4 ? 2 : 1, nAccel = c >= 5 ? 2 : 1;
    if (grouped) orchs = {O_ACC, O_RL, O_CYM};
    if (s.poly == P_MODULATION) {
        // the fill takes the subdivision of the modulation: N notes to the beat
        int want = (s.polyN == 5 || s.polyN == 6 || s.polyN == 7) ? s.polyN : 3;
        for (int i = 0; i < 7; ++i)
            if (SUBS[i].perQuarter == want && maxRate(s) * want <= 16.0) subs = {i};
    }

    const int total = (int)(ruds.size() * subs.size() * orchs.size() * shapes.size()) * nKick * nAccel;
    long long idx = ((long long)(e.variant < 0 ? 0 : e.variant) * coprimeStride(total)) % total;
    int orch = orchs[(size_t)(idx % (long long)orchs.size())]; idx /= (long long)orchs.size();
    int sub = subs[(size_t)(idx % (long long)subs.size())]; idx /= (long long)subs.size();
    int shape = shapes[(size_t)(idx % (long long)shapes.size())]; idx /= (long long)shapes.size();
    int rudI = ruds[(size_t)(idx % (long long)ruds.size())]; idx /= (long long)ruds.size();
    bool kickUnder = (idx % nKick) == 1; idx /= nKick;
    bool accel = (idx % nAccel) == 1;

    std::vector<Stroke> st = parse(RUDS[rudI].st);
    std::string rudName = RUDS[rudI].name;
    if (grouped) {                       // singles, an accent opens every group of N
        st.clear();
        for (int i = 0; i < s.polyN; ++i) st.push_back({i % 2 ? 'L' : 'R', i == 0, 0});
        if (s.polyN % 2) for (int i = 0; i < s.polyN; ++i) st.push_back({i % 2 ? 'R' : 'L', i == 0, 0});
        rudName = "одиночные группами по " + std::to_string(s.polyN);
    }
    bool hasAccents = false;
    for (auto& x : st) hasAccents = hasAccents || x.accent;

    // when the strokes fall
    int n1 = SUBS[sub].perQuarter, n2 = n1;
    if (accel) {
        static const int faster[] = {0, 0, 3, 4, 6, 6, 8, 8, 8};   // 2 -> 3, 3 -> 4, 4 -> 6, 6 -> 8
        int f = faster[n1];
        if (f > n1 && maxRate(s) * f <= 16.0) n2 = f;
    }
    std::vector<int> times;
    const int half = accel && n2 != n1 ? (e.lenTicks / 2 / (TPB / 2)) * (TPB / 2) : e.lenTicks;
    for (int i = 0;; ++i) {
        int t = (int)std::lround(i * (double)TPB / n1);
        if (t >= half) break;
        times.push_back(t);
    }
    for (int i = 0; half < e.lenTicks; ++i) {
        int t = half + (int)std::lround(i * (double)TPB / n2);
        if (t >= e.lenTicks) break;
        times.push_back(t);
    }

    const int toms[3] = {k.t1, k.t2, k.t3};
    int accentCount = 0;
    int lastRole[2] = {-1, -1}, lastTick[2] = {-100000, -100000};
    const double msPerTick = 60000.0 / (s.bpm * TPB);
    std::string sticking;
    for (size_t i = 0; i < times.size(); ++i) {
        const Stroke& x = st[i % st.size()];
        const int t = times[i];
        const double f = (double)t / std::max(1, e.lenTicks);
        const bool onBeat = t % TPB == 0;
        bool accent = hasAccents ? x.accent : onBeat;
        if (i < 24) { sticking += x.hand; sticking += (i + 1) % 4 == 0 ? "  " : " "; }
        double shapeMul = shape == 0 ? 0.62 + 0.38 * f : shape == 1 ? 1.0 : 1.0 - 0.3 * f;
        if (shape == 2 && i + 1 == times.size()) { accent = true; shapeMul = 1.05; }
        int vel = (int)((accent ? 114 : (hasAccents ? 76 : 86)) * shapeMul);
        if (x.hand == 'K') {
            e.out->push_back(mk(e.base + t, k.low, std::max(vel, 96)));
            continue;
        }
        int role = k.snare;
        const int q = std::min(3, (int)(f * 4));
        switch (orch) {
            case O_SNARE: role = k.snare; break;
            case O_DOWN: role = q == 0 ? k.snare : toms[q - 1]; break;
            case O_UP: role = q == 3 ? k.snare : toms[2 - q]; break;
            case O_ACC: role = accent ? toms[accentCount % 3] : k.snare; break;
            case O_RL: role = x.hand == 'R' ? toms[std::max(0, q - 1)] : k.snare; break;
            case O_ZIG: {
                const int seq[8] = {k.snare, k.t1, k.snare, k.t2, k.snare, k.t3, k.t2, k.t3};
                role = seq[std::min(7, (int)(f * 8))];
                break;
            }
            case O_FLOOR: role = x.hand == 'R' ? k.t3 : k.snare; break;
            case O_CYM: role = accent ? k.cym : k.snare; break;
        }
        if (accent) ++accentCount;
        // a hand needs time to travel: under 70 ms it stays on the drum it just played
        const int hd = x.hand == 'R' ? 0 : 1;
        if (lastRole[hd] >= 0 && role != lastRole[hd] && (t - lastTick[hd]) * msPerTick < 70.0) role = lastRole[hd];
        lastRole[hd] = role;
        lastTick[hd] = t;
        if (!accent && role == k.snare && orch == O_ACC) vel = (int)(vel * 0.85);
        e.out->push_back(mk(e.base + t, role, vel, role == k.cym ? 240 : 0));
        if (role == k.cym) e.out->push_back(mk(e.base + t, k.low, 108));
        for (int g = x.grace; g >= 1; --g)
            if (t - 20 * g >= 0) e.out->push_back(mk(e.base + t - 20 * g, role, (int)(vel * 0.42), 20));
    }
    if (kickUnder)
        for (int t = 0; t < e.lenTicks; t += TPB) e.out->push_back(mk(e.base + t, k.low, 100));

    *e.report += "Филл №" + std::to_string(e.variant + 1) + ": " + rudName + ", " + SUBS[sub].name +
                 (n2 != n1 ? ", вторая половина быстрее" : "") + ", " + ORCH_NAMES[orch] + ", " + SHAPE_NAMES[shape] +
                 (kickUnder ? ", бочка на доли" : "") + "\nАппликатура: " + sticking + "\n";
}

// ---------------------------------------------------------------- breaks
namespace {
enum { B_STOP, B_HATS, B_KICKS, B_GAP, B_BUILD, B_HALF, B_TOMS, B_SYNC, B_CHOKE, B_DISPLACE, NUM_BREAKS };
const int BREAK_MINC[] = {0, 0, 0, 0, 2, 2, 2, 4, 4, 6};
const char* BREAK_NAMES[] = {"остановка", "только тарелки", "только бочка", "пауза внутри такта", "раскат на малом",
                             "полуход на один такт", "такт на томах", "синкопы всем набором", "тарелки с бочкой на слабые доли",
                             "грув сдвинут"};
}  // namespace

void buildBreak(const Settings& s, const Bar& bar, int kit, int base, std::vector<Hit>& hits, std::string& report) {
    const int c = s.complexity, BT = bar.ticks(), ng = bar.ng();
    const KitRoles k = kitRoles(kit);
    std::vector<int> allowed;
    for (int i = 0; i < NUM_BREAKS; ++i) if (BREAK_MINC[i] <= c) allowed.push_back(i);
    const int v = std::max(0, s.breakVariant);
    const int type = allowed[(size_t)(v % (int)allowed.size())];
    const uint32_t sub = (uint32_t)(v / (int)allowed.size());
    auto nz = [&](int a) { return unit(mix(mix(sub, (uint32_t)(type * 17 + s.style)), (uint32_t)a)); };
    auto inBar = [&](const Hit& h) { return h.tick >= base && h.tick < base + BT; };
    std::vector<Hit> groove, rest;
    for (auto& h : hits) (inBar(h) ? groove : rest).push_back(h);
    std::vector<Hit> out;
    auto add = [&](int step, int role, int vel, int len = 0) { out.push_back(mk(base + step * STEP, role, vel, len)); };
    std::string detail;

    switch (type) {
        case B_STOP: {
            add(0, k.low, 116);
            add(0, k.cym, 110, 240);
            int pick = c == 0 ? 0 : (int)(nz(1) * 4) % 4;   // how the band comes back in
            if (pick == 1 && bar.steps >= 2) add(bar.steps - 2, k.snare, 104);
            if (pick == 2 && bar.steps >= 2) { add(bar.steps - 2, k.snare, 92); add(bar.steps - 1, k.snare, 108); }
            if (pick == 3 && bar.steps >= 4) { add(bar.steps - 4, k.low, 100); add(bar.steps - 2, k.snare, 108); }
            static const char* names[] = {"без затакта", "затакт малым", "затакт двумя шестнадцатыми", "затакт бочкой и малым"};
            detail = names[pick];
            break;
        }
        case B_HATS: {
            int kind = (int)(nz(5) * 3) % 3;   // the groove's own cymbals, eighths, sixteenths
            if (kind == 0) for (auto& h : groove) if (roleLayer(h.role) == L_CYMBALS) out.push_back(h);
            if (out.empty()) for (int st = 0; st < bar.steps; st += (kind == 2 ? 1 : 2)) add(st, k.hat, st % 4 ? (st % 2 ? 54 : 68) : 90);
            if (kit != K_ORCHESTRAL && kit != K_CINEMATIC) add(bar.mid(ng - 1), HatOpen, 96);
            break;
        }
        case B_KICKS: {
            for (auto& h : groove) if (roleLayer(h.role) == L_LOW) out.push_back(h);
            if (nz(6) < 0.6) add(0, k.cym, 108, 240);
            add(0, k.low, 112);
            break;
        }
        case B_GAP: {
            int g = ng <= 1 ? 0 : 1 + (int)(nz(2) * (ng - 1)) % (ng - 1);
            int from = base + bar.gs[(size_t)g] * STEP, to = from + bar.gl[(size_t)g] * STEP;
            if (ng <= 1) { from = base + BT / 2; to = base + BT; }
            for (auto& h : groove) if (h.tick < from || h.tick >= to) out.push_back(h);
            detail = "молчит доля " + std::to_string(g + 1);
            break;
        }
        case B_BUILD: {
            for (int t = 0; t < BT;) {
                double f = (double)t / BT;
                int n = f < 0.5 ? 2 : f < 0.75 ? 4 : 8;
                if (s.bpm / 60.0 * n > 16.0) n = 4;
                if (s.bpm / 60.0 * n > 16.0) n = 2;
                out.push_back(mk(base + t, k.snare, 50 + (int)(68 * f)));
                t += TPB / n;
            }
            int kicks = (int)(nz(7) * 3) % 3;   // on every beat, on the first only, on every eighth
            for (int g = 0; g < ng; ++g) {
                if (kicks != 1 || g == 0) add(bar.gs[(size_t)g], k.low, 100);
                if (kicks == 2) add(bar.mid(g), k.low, 92);
            }
            break;
        }
        case B_HALF: {
            for (auto& h : groove) if (roleLayer(h.role) == L_CYMBALS) out.push_back(h);
            add(0, k.low, 114);
            add(ng >= 2 ? bar.gs[(size_t)(ng / 2)] : bar.mid(0), k.snare, 114);
            if (nz(8) < 0.5 && ng >= 2) add(bar.mid(ng / 2 - 1), k.low, 100);
            if (nz(9) < 0.4) add(bar.steps - 1, k.low, 96);
            break;
        }
        case B_TOMS: {
            std::string e = euclid(std::min(bar.steps / 2, 3 + (int)(nz(3) * 3)), bar.steps / 2);
            for (int st = 0; st < bar.steps; st += 2) {
                bool acc = e[(size_t)(st / 2) % e.size()] == 'x';
                add(st, acc ? ((st / 2) % 3 == 0 ? k.t1 : k.t2) : k.t3, acc ? 106 : 80);
            }
            for (int g = 0; g < ng; ++g) add(bar.gs[(size_t)g], k.low, 102);
            break;
        }
        case B_SYNC: {
            std::string e = euclid(std::min(bar.steps, 3 + (int)(nz(4) * 5)), bar.steps);
            for (int st = 0; st < bar.steps; ++st)
                if (e[(size_t)st] == 'x') { add(st, k.low, 112); add(st, k.snare, 108); add(st, k.cym, 104, 60); }
            detail = e;
            break;
        }
        case B_CHOKE: {
            for (int g = 0; g < ng; ++g) { add(bar.mid(g), k.low, 110); add(bar.mid(g), k.cym, 104, 60); }
            add(bar.gs[(size_t)((int)(nz(10) * ng) % ng)], k.snare, 110);
            break;
        }
        case B_DISPLACE: {
            for (auto h : groove) {
                int rel = (h.tick - base + (1 + (int)(nz(11) * 3) % 3) * STEP) % BT;
                h.tick = h.grid = base + rel;
                out.push_back(h);
            }
            break;
        }
    }
    if (type != B_STOP && type != B_BUILD && type != B_DISPLACE && c >= 3 && bar.steps >= 4) {
        int pick = (int)(sub / 3) % 3;
        if (pick) {
            int from = base + (bar.steps - 2) * STEP;
            out.erase(std::remove_if(out.begin(), out.end(), [&](const Hit& h) { return h.tick >= from && roleLayer(h.role) != L_CYMBALS; }), out.end());
            if (pick == 1) add(bar.steps - 2, k.snare, 106);
            else { add(bar.steps - 2, k.snare, 94); add(bar.steps - 1, k.snare, 110); }
            detail += std::string(detail.empty() ? "" : ", ") + (pick == 1 ? "затакт малым" : "затакт двумя шестнадцатыми");
        }
    }
    hits = rest;
    hits.insert(hits.end(), out.begin(), out.end());
    report += "Брейк №" + std::to_string(v + 1) + ": " + BREAK_NAMES[type] + (detail.empty() ? "" : ", " + detail) + "\n";
}

}  // namespace drums
