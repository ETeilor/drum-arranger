// Meters, the style table and the groove families.
#include <map>
#include <numeric>
#include "Internal.h"

namespace drums {

// ---------------------------------------------------------------- basics
uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u + b + 0x7F4A7C15u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

Bar makeBar(const Meter& m) {
    Bar b;
    auto push = [&](int len) {
        b.gs.push_back(b.steps);
        b.gl.push_back(len);
        b.steps += len;
    };
    b.steps = 0;
    int num = std::max(1, m.num);
    if (m.den <= 4) {                       // quarters (or longer): every beat is a group of four sixteenths
        int perBeat = m.den == 4 ? 1 : (m.den == 2 ? 2 : 4);
        for (int i = 0; i < num * perBeat; ++i) push(4);
    } else {
        // eighths or sixteenths: groups of three when the count divides by three, else twos (fours) with
        // the long group last: 7/8 = 2+2+3, 5/8 = 2+3, 9/16 = 4+2+3
        int unit = m.den == 8 ? 2 : 1;      // sixteenths per counted unit
        std::vector<int> g;
        if (num % 3 == 0 && num > 3) {
            for (int i = 0; i < num / 3; ++i) g.push_back(3);
        } else if (m.den == 8) {
            int n = num;
            while (n > 3) { g.push_back(2); n -= 2; }
            g.push_back(n);
        } else {
            int n = num;
            while (n > 0) {
                if (n == 1) { if (!g.empty()) g.back() += 1; else g.push_back(1); n = 0; }
                else if (n == 2 || n == 3) { g.push_back(n); n = 0; }
                else if (n == 5) { g.push_back(2); g.push_back(3); n = 0; }
                else if (n == 6) { g.push_back(3); g.push_back(3); n = 0; }
                else { g.push_back(4); n -= 4; }
            }
        }
        for (int x : g) push(x * unit);
    }
    return b;
}

std::string meterGrouping(const Meter& m) {
    Bar b = makeBar(m);
    int unit = m.den >= 16 ? 1 : (m.den == 8 ? 2 : 4);
    std::string s = std::to_string(m.num) + "/" + std::to_string(m.den);
    if (m.den < 8) return s;
    s += " = ";
    for (int g = 0; g < b.ng(); ++g) {
        if (g) s += "+";
        s += std::to_string(b.gl[(size_t)g] / unit);
    }
    return s;
}

int realignBars(int n, const Meter& m) {
    int steps = makeBar(m).steps;
    n = std::max(1, n);
    return (int)(std::lcm((long long)n, (long long)steps) / steps);
}

// k strokes over n steps, as even as possible, starting on a stroke (same set as Bjorklund's algorithm
// gives, up to rotation)
std::string euclid(int k, int n) {
    n = std::max(1, n);
    k = std::clamp(k, 0, n);
    std::string s((size_t)n, '.');
    for (int i = 0; i < n; ++i)
        if ((i * k) % n < k) s[(size_t)i] = 'x';
    return s;
}

double Ctx::nz(int layer, int a, int b) const {
    int v = s.layerVariant[layer] >= 0 ? s.layerVariant[layer] : s.grooveVariant;
    return unit(mix(mix(mix((uint32_t)v, (uint32_t)(s.style * 31 + layer)), (uint32_t)a), (uint32_t)b));
}

int defaultLen(int role) {
    switch (role) {
        case HatOpen: return 200;
        case Ride: case RideBell: return 160;
        case Crash1: case Crash2: case China: case Piatti: return 960;
        case Splash: return 480;
        case TamTam: return 1920;
        case SusCymbal: case Triangle: return 240;
        case Timpani: case BassDrum: case TaikoLow: return 240;
        default: return 60;
    }
}

void Ctx::addT(int role, int tick, int vel, int len, int pitch) const {
    Hit h;
    h.tick = h.grid = base + tick;
    h.role = role;
    h.vel = std::clamp(vel, 1, 127);
    h.len = len > 0 ? len : defaultLen(role);
    h.pitch = pitch;
    out->push_back(h);
}

// timpani sound best between F2 and F3 (MIDI 41..53, middle C = 60)
int timpaniPitch(int pc) { return 41 + (((pc - 5) % 12) + 12) % 12; }
int timpaniDominant(int tonic) { return tonic - 5 >= 40 ? tonic - 5 : tonic + 7; }

KitRoles kitRoles(int kit) {
    switch (kit) {
        case K_CINEMATIC: return {TaikoHigh, TomHigh, TomMid, TaikoLow, Piatti, BassDrum, Ticker};
        case K_ORCHESTRAL: return {OrchSnare, OrchSnare, TenorDrum, TenorDrum, Piatti, BassDrum, Triangle};
        case K_ELECTRONIC: return {Snare, PercHigh, PercLow, TomFloor, Crash1, Kick, HatClosed};
        default: return {Snare, TomHigh, TomMid, TomFloor, Crash1, Kick, HatClosed};
    }
}

namespace {

// the root the timpani play in this bar: the lowest note of the rhythm source, else the chosen tonic
int barRoot(const Ctx& x) {
    int lo = 999;
    for (auto& n : x.s.follow)
        if (n.start >= x.base && n.start < x.base + x.bar.ticks()) lo = std::min(lo, n.pitch);
    return timpaniPitch(lo < 999 ? lo % 12 : x.s.tonic);
}

// real notes: a roll of thirty-seconds with a velocity ramp
void roll(const Ctx& x, int role, int fromTick, int toTick, int v0, int v1, int stepTicks = 60, int pitch = -1) {
    int n = std::max(1, (toTick - fromTick) / stepTicks);
    for (int i = 0; i < n; ++i)
        x.addT(role, fromTick + i * stepTicks, v0 + (v1 - v0) * i / std::max(1, n - 1), stepTicks, pitch);
}

// ---------------------------------------------------------------- family: backbeat grooves
enum { BB_NORMAL, BB_HALF, BB_ALL, BB_DOUBLE, BB_NONE };
enum { KB_ROCK, KB_FOUR, KB_GALLOP, KB_DOUBLE, KB_DBEAT, KB_SPARSE };
enum { HT_8, HT_16, HT_4, HT_OFF, HT_SHUFFLE, HT_TOM, HT_RIDE };

void beat(const Ctx& x) {
    const Bar& B = x.bar;
    const int ng = B.ng(), c = x.c;
    const int bb = x.sp.p[0], kb = x.sp.p[1], ht = x.sp.p[2], snare = x.sp.p[3], ghost = x.sp.p[4], busy = x.sp.p[5];
    std::vector<char> K((size_t)B.steps, 0), S((size_t)B.steps, 0);
    auto k = [&](int st) -> char& { return K[(size_t)st]; };
    auto sn = [&](int st) -> char& { return S[(size_t)st]; };

    if (bb != BB_NONE) {
        if (ng == 1) sn(B.mid(0)) = 1;
        else if (bb == BB_NORMAL) { for (int g = 1; g < ng; g += 2) sn(B.gs[(size_t)g]) = 1; }
        else if (bb == BB_HALF) sn(B.gs[(size_t)(ng / 2)]) = 1;
        else if (bb == BB_ALL) { for (int g = 0; g < ng; ++g) sn(B.gs[(size_t)g]) = 1; }
        else if (bb == BB_DOUBLE) { for (int g = 0; g < ng; ++g) sn(B.mid(g)) = 1; }
    }
    switch (kb) {
        case KB_ROCK:
            if (bb == BB_HALF) break;
            for (int g = 0; g < ng; ++g) if (!sn(B.gs[(size_t)g])) k(B.gs[(size_t)g]) = 1;
            break;
        case KB_FOUR: for (int g = 0; g < ng; ++g) k(B.gs[(size_t)g]) = 1; break;
        case KB_GALLOP:
            for (int g = 0; g < ng; ++g) {
                int l = B.gl[(size_t)g], s0 = B.gs[(size_t)g];
                k(s0) = 1;
                if (l / 2 > 0) k(s0 + l / 2) = 1;
                if (l / 2 + 1 < l) k(s0 + l / 2 + 1) = 1;
            }
            break;
        case KB_DOUBLE:
            // bursts per beat: eighths at the bottom, three of four in the middle, continuous on top
            for (int g = 0; g < ng; ++g) {
                static const char* cells[] = {"x.x.", "x.xx", "xx.x", "xxx.", "xxxx"};
                double r = x.nz(L_LOW, 700 + g, x.vk());
                int cell = c < 3 ? 0 : c < 6 ? 1 + (int)(r * 3) % 3 : c < 9 ? (r < 0.6 ? 4 : 1 + (int)(r * 7) % 3) : 4;
                for (int p = 0; p < B.gl[(size_t)g]; ++p)
                    if (cells[cell][p % 4] == 'x') k(B.gs[(size_t)g] + p) = 1;
            }
            break;
        case KB_DBEAT: for (int g = 1; g < ng; ++g) k(B.mid(g)) = 1; break;
        default: break;
    }
    if (bb != BB_ALL || kb == KB_FOUR || kb == KB_DOUBLE) k(0) = 1;
    if (bb == BB_ALL && kb == KB_ROCK) k(0) = 1;   // Motown: kick under the first snare

    // extra kicks: candidates ranked by where they sit in the beat, the variant breaks ties
    if (kb != KB_DOUBLE && kb != KB_GALLOP && busy > 0) {
        struct Cand { int step; double w; };
        std::vector<Cand> cands;
        for (int st = 0; st < B.steps; ++st) {
            if (k(st) || sn(st)) continue;
            int g = B.groupOf(st), pos = st - B.gs[(size_t)g], l = B.gl[(size_t)g];
            double w = pos == 0 ? 0.9 : pos * 2 == l ? 0.7 : pos == l - 1 ? 0.55 : pos == 1 ? 0.2 : 0.35;
            if (pos == 1 && c < 6) continue;
            if (kb == KB_FOUR) w *= 0.6;
            cands.push_back({st, w + 0.45 * x.nz(L_LOW, st, x.vk())});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
        int want = (int)std::floor(ng * busy * std::max(0, c - 1) / 18.0 + 0.5);
        for (auto& cd : cands) {
            if (want <= 0) break;
            int run = 1;                                    // never three sixteenths in a row on one foot
            for (int q = cd.step - 1; q >= 0 && k(q); --q) ++run;
            for (int q = cd.step + 1; q < B.steps && k(q); ++q) ++run;
            if (run >= 3) continue;
            k(cd.step) = 1;
            --want;
        }
    }
    for (int st = 0; st < B.steps; ++st) {
        if (k(st)) x.add(Kick, st, st == 0 ? 112 : (kb == KB_DOUBLE ? (st % 2 ? 92 : 100) : 102));
        if (sn(st)) {
            x.add(snare, st, snare == SideStick ? 92 : 110);
            if (snare == Clap && c >= 6) x.add(Snare, st, 84);
        }
    }
    // ghost notes and one syncopated accent
    if (ghost > 0 && c >= 4) {
        struct Cand { int step; double w; };
        std::vector<Cand> cands;
        for (int st = 0; st < B.steps; ++st) {
            int g = B.groupOf(st), pos = st - B.gs[(size_t)g];
            if (k(st) || sn(st) || pos % 2 == 0) continue;
            cands.push_back({st, x.nz(L_SNARE, st, x.vk())});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
        int want = std::min((int)cands.size(), (int)std::floor(ng * ghost * (c - 3) / 14.0 + 0.5));
        for (int i = 0; i < want; ++i) x.add(Snare, cands[(size_t)i].step, 30 + (int)(12 * cands[(size_t)i].w));
    }
    if (c >= 8 && busy >= 2 && ng >= 2 && bb != BB_DOUBLE) {
        int g = ((int)(x.nz(L_SNARE, 500, x.vk()) * ng)) % ng;
        int st = B.gs[(size_t)g] + B.gl[(size_t)g] - 1;
        if (!k(st) && !sn(st)) x.add(Snare, st, 92);
    }

    // the time-keeping hand
    auto pulseVel = [&](int st) {
        int g = B.groupOf(st), pos = st - B.gs[(size_t)g];
        return pos == 0 ? 96 : pos % 2 == 0 ? 70 : 52;
    };
    int openStep = -1;
    if ((ht == HT_8 || ht == HT_16) && c >= 3 && x.nz(L_CYMBALS, 77, x.vk()) < 0.25 + 0.05 * c) openStep = B.mid(ng - 1);
    switch (ht) {
        case HT_4: for (int g = 0; g < ng; ++g) x.add(HatClosed, B.gs[(size_t)g], 92); break;
        case HT_8:
        case HT_16:
            for (int st = 0; st < B.steps; ++st) {
                int pos = st - B.gs[(size_t)B.groupOf(st)];
                if (ht == HT_8 && pos % 2) {
                    // at high complexity a pair of sixteenths decorates the end of one beat
                    bool deco = c >= 6 && pos == B.gl[(size_t)B.groupOf(st)] - 1 && x.nz(L_CYMBALS, st, x.vk()) < 0.08 * (c - 4);
                    if (!deco) continue;
                }
                if (st == openStep) x.add(HatOpen, st, 90);
                else x.add(HatClosed, st, pulseVel(st));
            }
            break;
        case HT_OFF:
            for (int g = 0; g < ng; ++g) x.add(c >= 2 ? HatOpen : HatClosed, B.mid(g), 92, c >= 2 ? 110 : 0);
            if (c >= 5)
                for (int st = 0; st < B.steps; ++st) {
                    int g = B.groupOf(st);
                    if (st != B.mid(g) && (c >= 7 || (st - B.gs[(size_t)g]) % 2 == 0)) x.add(HatClosed, st, st == B.gs[(size_t)g] ? 64 : 50);
                }
            break;
        case HT_SHUFFLE:
            for (int g = 0; g < ng; ++g) {
                int t0 = B.gs[(size_t)g] * STEP, l = B.gl[(size_t)g] * STEP;
                x.addT(c >= 6 ? Ride : HatClosed, t0, 96);
                x.addT(c >= 6 ? Ride : HatClosed, t0 + l * 2 / 3, 70);
                if (c >= 8 && x.nz(L_CYMBALS, g, x.vk()) < 0.35) x.addT(Snare, t0 + l / 3, 34);   // the middle triplet, ghosted
            }
            break;
        case HT_TOM:
            for (int st = 0; st < B.steps; st += 2) {
                if (sn(st)) continue;
                int g = B.groupOf(st), pos = st - B.gs[(size_t)g];
                int role = TomFloor;
                if (c >= 4 && (g % 2) && pos * 2 == B.gl[(size_t)g]) role = TomMid;
                if (c >= 7 && pos != 0 && x.nz(L_TOMS, st, x.vk()) < 0.2) role = TomHigh;
                x.add(role, st, pos == 0 ? 94 : 76);
            }
            if (c >= 5) for (int g = 0; g < ng; ++g) x.add(HatPedal, B.gs[(size_t)g], 70);
            break;
        case HT_RIDE:
            for (int st = 0; st < B.steps; st += 2) {
                int pos = st - B.gs[(size_t)B.groupOf(st)];
                int g = B.groupOf(st);
                bool accent = pos == 0 && g > 0 && c >= 4 && x.nz(L_CYMBALS, 600 + g, x.vk()) < 0.3;
                x.add(accent ? China : (pos == 0 && c >= 5 ? RideBell : Ride), st, accent ? 104 : pos == 0 ? 98 : 74);
            }
            if (c >= 3 && kb != KB_DOUBLE) for (int g = 1; g < ng; g += 2) x.add(HatPedal, B.gs[(size_t)g], 72);
            break;
    }
}

// ---------------------------------------------------------------- family: blast beats
// p[0]: 0 traditional (kick and snare alternate, the cymbal goes with the kick), 1 bomb (kick on every
// sixteenth, snare and cymbal on the eighths), 2 hammer (everything together on the eighths)
void blast(const Ctx& x) {
    const Bar& B = x.bar;
    const int type = x.sp.p[0], c = x.c;
    for (int st = 0; st < B.steps; ++st) {
        int pos = st - B.gs[(size_t)B.groupOf(st)];
        int cym = c < 3 ? HatClosed : Ride;
        // the variant picks the beats that get an accent cymbal
        if (pos == 0 && x.nz(L_CYMBALS, 600 + B.groupOf(st), x.vk()) < 0.2 + 0.03 * c) cym = c < 3 ? HatOpen : (c >= 6 ? China : Crash2);
        bool even = st % 2 == 0;
        int sv = 92 + (pos == 0 ? 8 : 0) - (st % 4 == 3 ? 5 : 0);   // blasts are not played flat out
        if (type == 0) {
            if (even) { x.add(Kick, st, 104); x.add(cym, st, pos == 0 ? 96 : 82); }
            else x.add(Snare, st, sv);
        } else if (type == 1) {
            x.add(Kick, st, even ? 102 : 94);
            if (even) { x.add(Snare, st, sv); x.add(cym, st, pos == 0 ? 96 : 82); }
        } else {
            if (even) { x.add(Kick, st, 104); x.add(Snare, st, sv); x.add(cym, st, pos == 0 ? 96 : 82); }
            else if (c >= 7) x.add(Kick, st, 92);
        }
    }
}

// ---------------------------------------------------------------- family: cinematic
// Three bands share one accent pattern: a fast high band, a mid band that drives, a low band that only
// plays on or near the accents.  p[0]: 0 hybrid pulse, 1 taiko ensemble, 2 clock, 3 trailer hits
std::string accentPattern(const Ctx& x) {
    const Bar& B = x.bar;
    int kAcc = B.ng() + 1 + (int)(x.nz(L_SNARE, 5) * B.ng());
    return euclid(std::min(kAcc, B.steps), B.steps);
}

void cine(const Ctx& x) {
    const Bar& B = x.bar;
    const int type = x.sp.p[0], c = x.c, ng = B.ng();
    const bool lastBar = x.barIndex == x.nBars - 1;
    std::string acc = accentPattern(x);
    auto isAcc = [&](int st) { return acc[(size_t)st] == 'x'; };
    // accents the low band takes: the downbeat always, the rest ranked
    std::vector<int> lowSteps{0};
    {
        struct Cand { int step; double w; };
        std::vector<Cand> cands;
        for (int st = 1; st < B.steps; ++st)
            if (isAcc(st)) {
                bool pulse = st == B.gs[(size_t)B.groupOf(st)];
                cands.push_back({st, (pulse ? 0.5 : 0.0) + x.nz(L_LOW, st, x.vk())});
            }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
        int want = std::min((int)cands.size(), c * (int)cands.size() / 12);
        for (int i = 0; i < want; ++i) lowSteps.push_back(cands[(size_t)i].step);
    }
    if (type == 0) {
        if (c >= 1)
            for (int st = 0; st < B.steps; st += (c < 3 ? 2 : 1)) x.add(Ticker, st, isAcc(st) ? 104 : (st % 2 ? 46 : 60));
        for (int st = 0; st < B.steps; ++st) {
            if (isAcc(st)) {
                x.add(TomMid, st, 100);
                if (c >= 4) x.add(TaikoHigh, st, 96);
            } else if (c >= 5 && st % 2 == 0 && x.nz(L_TOMS, st, x.vk()) < 0.5) x.add(TomMid, st, 62);
            else if (c >= 8 && st + 1 < B.steps && isAcc(st + 1) && x.nz(L_TOMS, st, x.vk()) < 0.5) x.add(TomHigh, st, 70);
        }
        for (int st : lowSteps) { x.add(TaikoLow, st, st == 0 ? 116 : 104); if (st == 0) x.add(BassDrum, st, 108); }
        if (c >= 6 && lastBar) { x.addT(TaikoHigh, B.ticks() - 120, 80); x.addT(TaikoHigh, B.ticks() - 60, 98); }
    } else if (type == 1) {
        static const char* cells4[] = {"x...", "x.x.", "x..x", "x.xx", "xxxx"};
        for (int g = 0; g < ng; ++g) {
            int lvl = std::clamp((int)(c / 3.0 + x.nz(L_SNARE, g, x.vk()) * 1.6), 0, 4);
            for (int p = 0; p < 4; ++p)
                if (cells4[lvl][p] == 'x') {
                    int st = B.at(g, p);
                    if (p > 0 && st == B.at(g, p - 1)) continue;
                    x.add(TaikoHigh, st, p == 0 ? 108 : 78);
                }
            if (g % 2 == 0) x.add(TaikoLow, B.gs[(size_t)g], g == 0 ? 116 : 100);
            if (c >= 7 && g % 2 == 1) x.add(Anvil, B.gs[(size_t)g], 84);
        }
        for (int st : lowSteps) if (st != 0 && c >= 4) x.add(TaikoLow, st, 96);
        if (x.barIndex % 2 == 0) x.add(BassDrum, 0, 110);
    } else if (type == 2) {
        for (int st = 0; st < B.steps; st += (c >= 4 ? 1 : 2)) x.add(Ticker, st, st % 4 == 0 ? 72 : (st % 2 ? 40 : 54));
        if (c >= 3 || x.barIndex % 2 == 0) {
            x.add(BassDrum, 0, 100);
            if (c >= 2 && B.steps > 2) x.add(BassDrum, 2, 78);   // heartbeat
        }
        if (c >= 5 && lowSteps.size() > 1) x.add(TaikoLow, lowSteps[1], 92);
        if (c >= 7) {
            std::string e = euclid(3, B.steps);
            for (int st = 0; st < B.steps; ++st)
                if (e[(size_t)st] == 'x') x.add(TomMid, (st + 3) % B.steps, 60);
        }
    } else {
        bool impact = x.barIndex == 0 || c >= 4 || x.barIndex % 2 == 0;
        if (impact) {
            x.add(BassDrum, 0, 120);
            x.add(TaikoLow, 0, 120);
            x.add(Piatti, 0, 108);
            if (c >= 5 && x.barIndex == 0) x.add(TamTam, 0, 96);
        }
        if (c >= 6 && ng >= 2 && x.nz(L_LOW, 40, x.barIndex) < 0.5) { x.add(TaikoLow, B.gs[(size_t)(ng / 2)], 108); x.add(BassDrum, B.gs[(size_t)(ng / 2)], 100); }
        bool nextImpact = !lastBar && (c >= 4 || (x.barIndex + 1) % 2 == 0);
        if (c >= 3 && nextImpact && B.steps >= 4) { x.add(TaikoHigh, B.steps - 4, 80); x.add(TaikoHigh, B.steps - 2, 96); }
        if (lastBar) {
            int from = B.gs[(size_t)std::max(0, ng - 2)] * STEP;
            if (c >= 5) {
                int midT = (from + B.ticks()) / 2;
                roll(x, TomMid, from, midT, 40, 78, 120);
                roll(x, TomMid, midT, B.ticks(), 80, 118, 60);
            } else roll(x, TomMid, from, B.ticks(), 44, 112, c >= 2 ? 120 : 240);
            if (c >= 7) roll(x, SusCymbal, from, B.ticks(), 30, 104, 60);
        }
    }
}

// ---------------------------------------------------------------- family: orchestral
// p[0]: 0 timpani and accents, 1 march, 2 bolero ostinato, 3 build
std::string marchCell(int len, int lvl, bool& isRoll) {
    isRoll = false;
    if (len == 4) {
        static const char* c4[] = {"x...", "x.x.", "x.xx", "xxx.", "xxxx"};
        if (lvl >= 5) { isRoll = true; return "x..."; }
        return c4[std::clamp(lvl, 0, 4)];
    }
    if (len == 6) {
        static const char* c6[] = {"x.....", "x..x..", "x.xx..", "x.x.x.", "x.xxx.", "xxxxxx"};
        return c6[std::clamp(lvl, 0, 5)];
    }
    std::string s((size_t)len, '.');
    s[0] = 'x';
    if (lvl >= 2) for (int i = 0; i < len; i += 2) s[(size_t)i] = 'x';
    if (lvl >= 4) std::fill(s.begin(), s.end(), 'x');
    return s;
}

void orch(const Ctx& x) {
    const Bar& B = x.bar;
    const int type = x.sp.p[0], c = x.c, ng = B.ng();
    const bool lastBar = x.barIndex == x.nBars - 1;
    const int T = barRoot(x), D = timpaniDominant(T);
    const int total = std::max(1, x.nBars * B.ticks());
    auto frac = [&](int tick) { return (double)(x.base + tick) / total; };
    int lastG = B.gs[(size_t)(ng - 1)] * STEP;
    if (type == 0) {
        x.addT(Timpani, 0, 98, 480, T);
        if (c >= 5 && x.barIndex % 2 == 1) {
            for (int g = 1; g < ng; ++g) x.addT(Timpani, B.gs[(size_t)g] * STEP, 80, 240, g == ng - 1 ? D : T);
        } else if (c >= 2 && x.barIndex % 2 == 1 && ng >= 2) {
            // how the dominant leads back: on the last beat, on its second half, or both
            int pick = (int)(x.nz(L_LOW, 610) * 3) % 3;
            int halfT = B.mid(ng - 1) * STEP;
            if (pick != 1) x.addT(Timpani, lastG, 84, 240, D);
            if (pick != 0) x.addT(Timpani, halfT, 80, 240, D);
        }
        if (c >= 7 && lastBar) roll(x, Timpani, lastG, B.ticks(), 50, 110, 60, D);
        if (x.barIndex == 0 || (c >= 4 && x.barIndex % 4 == 0) || (c >= 8 && x.barIndex % 2 == 0)) x.add(BassDrum, 0, 102);
        if (x.barIndex == 0 && c >= 2) x.add(Piatti, 0, 104);
        if (x.barIndex == 0 && c >= 9) x.add(TamTam, 0, 90);
        if (c >= 5 && x.barIndex % 2 == 0)
            for (int g = 1; g < ng; ++g) if (x.nz(L_CYMBALS, 620 + g) < 0.5) x.add(Triangle, B.gs[(size_t)g], 50);
        if (c >= 3 && lastBar) roll(x, SusCymbal, B.gs[(size_t)std::max(0, ng - 2)] * STEP, B.ticks(), 30, 100, 60);
    } else if (type == 1) {
        for (int g = 0; g < ng; ++g) {
            int lvl = c == 0 ? (g % 2 ? 1 : 0) : std::clamp((int)(c / 2.5 * (0.6 + 0.8 * x.nz(L_SNARE, g, x.vk()))) + (g == ng - 1 ? 1 : 0), 0, 5);
            bool isRoll = false;
            int l = B.gl[(size_t)g], s0 = B.gs[(size_t)g];
            std::string cell = marchCell(l, lvl, isRoll);
            if (isRoll) roll(x, OrchSnare, s0 * STEP, (s0 + l) * STEP, 70, 100, 60);
            else
                for (int p = 0; p < l; ++p)
                    if (cell[(size_t)p] == 'x') x.add(OrchSnare, s0 + p, p == 0 ? 104 : 72);
            if (c >= 5 && g % 2 == 0 && !isRoll && s0 > 0) x.addT(OrchSnare, s0 * STEP - 22, 46);   // flam
            if (g % 2 == 0) { x.add(BassDrum, s0, 90); if (c >= 6 && g == 0) x.add(Piatti, s0, 88, 480); }
            if (c >= 4) x.add(TenorDrum, s0, 80);
        }
    } else if (type == 2) {
        for (int g = 0; g < ng; ++g) {
            int t0 = B.gs[(size_t)g] * STEP, l = B.gl[(size_t)g] * STEP, half = l / 2;
            auto v = [&](int t, bool accent) { return 38 + (int)(frac(t) * (40 + 4 * c)) + (accent ? 10 : 0); };
            auto put = [&](int t, bool accent) {
                x.addT(OrchSnare, t, v(t, accent));
                if (c >= 3 && frac(t) >= 0.5) x.addT(TenorDrum, t, v(t, accent) - 8);
            };
            put(t0, true);
            if (g == ng - 1 && x.barIndex % 2 == 1) { for (int i = 1; i < 6; ++i) put(t0 + i * l / 6, false); }
            else if (g == ng - 1) put(t0 + half, false);
            else if (x.nz(L_SNARE, 630 + g) < 0.72) { for (int i = 0; i < 3; ++i) put(t0 + half + i * half / 3, false); }
            else put(t0 + half, false);
        }
        if (c >= 5) {
            x.addT(Timpani, 0, 70 + (int)(frac(0) * 30), 240, T);
            if (ng >= 2) x.addT(Timpani, lastG, 64 + (int)(frac(lastG) * 30), 240, D);
            if (c >= 8) for (int g = 1; g < ng - 1; ++g) x.addT(Timpani, B.gs[(size_t)g] * STEP, 60 + (int)(frac(0) * 30), 240, T);
        }
    } else {
        int stepT = 240;
        for (int t = 0; t < B.ticks();) {
            double f = frac(t);
            stepT = f < 0.35 + 0.3 * x.nz(L_SNARE, 640) ? (c >= 3 ? 120 : 240) : 60;
            x.addT(OrchSnare, t, 30 + (int)(85 * std::pow(f, 1.5)), stepT);
            if (f >= 0.5) x.addT(SusCymbal, t, 24 + (int)(80 * f), stepT);
            t += stepT;
        }
        x.addT(Timpani, 0, 76, 480, T);
        for (int g = 1; g < ng; ++g) if (c >= 2 && x.nz(L_LOW, 650 + g, x.barIndex) < 0.3) x.addT(TenorDrum, B.gs[(size_t)g] * STEP, 60 + (int)(40 * frac(0)));
        if (lastBar) roll(x, Timpani, lastG, B.ticks(), 60, 116, 60, D);
        if (c >= 5 && lastBar) for (int g = 0; g < ng; ++g) x.add(BassDrum, B.gs[(size_t)g], 70 + 40 * g / std::max(1, ng - 1));
    }
}

// ---------------------------------------------------------------- family: breakbeats
// p[0]: 0 the classic funk break (two-beat cells), 1 two-step
void brk(const Ctx& x, std::vector<Hit>* into = nullptr) {
    Ctx y = x;
    if (into) y.out = into;
    const Bar& B = y.bar;
    const int type = y.sp.p[0], c = y.c, ng = B.ng();
    std::vector<char> used((size_t)B.steps, 0);
    auto K = [&](int st, int v = 104) { y.add(Kick, st, v); used[(size_t)st] = 1; };
    auto S = [&](int st, int v) { if (v < 80 && c < 3) return; y.add(Snare, st, v); used[(size_t)st] = 1; };
    if (type == 0) {
        for (int g = 0; g < ng; ++g) {
            int pair = (g / 2) % 2, in = g % 2;
            if (g == ng - 1 && ng % 2 == 1) { K(B.at(g, 0)); if (B.gl[(size_t)g] > 2) S(B.at(g, 2), 108); continue; }
            if (pair == 0 && in == 0) { K(B.at(g, 0), g == 0 ? 112 : 104); if (B.gl[(size_t)g] > 2) K(B.at(g, 2), 96); }
            if (pair == 0 && in == 1) { S(B.at(g, 0), 110); S(B.at(g, 3), 62); }
            if (pair == 1 && in == 0) { S(B.at(g, 1), 58); K(B.at(g, 2), 102); K(B.at(g, 3), 96); }
            if (pair == 1 && in == 1) {
                // every other bar the last backbeat comes an eighth late
                bool late = c >= 5 && y.vk() % 2 == 1 && y.nz(L_SNARE, 31, y.vk()) < 0.6 && B.gl[(size_t)g] >= 4;
                S(late ? B.at(g, 2) : B.at(g, 0), 110);
                if (!late) S(B.at(g, 3), 70);
            }
        }
    } else {
        K(0, 112);
        if (ng >= 2) K(B.at(ng / 2, 2), 104);
        for (int g = 1; g < ng; g += 2) S(B.gs[(size_t)g], 110);
        if (c >= 4 && ng >= 3) { S(B.at(1, 3), 50); S(B.at(2, 1), 46); }
        if (c >= 7 && ng >= 4 && y.nz(L_LOW, 12, y.vk()) < 0.6) K(B.at(ng - 1, 2), 96);
    }
    if (c >= 7) {
        struct Cand { int step; double w; };
        std::vector<Cand> cands;
        for (int st = 1; st < B.steps; ++st)
            if (!used[(size_t)st] && (st - B.gs[(size_t)B.groupOf(st)]) % 2) cands.push_back({st, y.nz(L_SNARE, 200 + st, y.vk())});
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
        for (int i = 0; i < std::min((int)cands.size(), ng * (c - 6) / 4); ++i) y.add(Snare, cands[(size_t)i].step, 44);
    }
    bool sixteenths = type == 1 && c >= 5;
    for (int st = 0; st < B.steps; st += sixteenths ? 1 : 2) {
        int pos = st - B.gs[(size_t)B.groupOf(st)];
        y.add(type == 0 && c >= 3 ? Ride : HatClosed, st, pos == 0 ? 90 : pos % 2 == 0 ? 70 : 50);
    }
}
void brkStyle(const Ctx& x) { brk(x); }

// ---------------------------------------------------------------- family: trap
void trap(const Ctx& x) {
    const Bar& B = x.bar;
    const int c = x.c, ng = B.ng();
    int snareStep = ng == 1 ? B.mid(0) : B.gs[(size_t)(ng / 2)];
    x.add(Clap, snareStep, 110);
    if (c >= 5) x.add(Snare, snareStep, 88);
    std::vector<char> K((size_t)B.steps, 0);
    K[0] = 1;
    struct Cand { int step; double w; };
    std::vector<Cand> cands;
    for (int st = 1; st < B.steps; ++st) {
        if (st == snareStep) continue;
        int g = B.groupOf(st), pos = st - B.gs[(size_t)g], l = B.gl[(size_t)g];
        double w = pos * 2 == l ? 0.6 : pos == l - 1 ? 0.6 : pos == 0 ? 0.5 : 0.3;
        cands.push_back({st, w + 0.5 * x.nz(L_LOW, st, x.vk())});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.w > b.w; });
    for (int i = 0; i < std::min((int)cands.size(), ng * c / 8); ++i) K[(size_t)cands[(size_t)i].step] = 1;
    for (int st = 0; st < B.steps; ++st) if (K[(size_t)st]) x.add(Kick, st, st == 0 ? 114 : 100, 240);
    // hats: eighths, with rolls where the variant puts them
    int openStep = c >= 4 && x.nz(L_CYMBALS, 77, x.vk()) < 0.5 ? B.mid(ng - 1) : -1;
    for (int st = 0; st < B.steps; st += 2) {
        if (st == openStep) { x.add(HatOpen, st, 88); continue; }
        double r = x.nz(L_CYMBALS, st, x.vk());
        bool rollHere = c >= 3 && r < 0.10 + 0.035 * c && st + 2 <= B.steps;
        if (!rollHere) { x.add(HatClosed, st, st % 4 == 0 ? 88 : 68); continue; }
        double r2 = x.nz(L_CYMBALS, 100 + st, x.vk());
        int n = c < 5 ? 2 : (r2 < 0.4 ? 3 : r2 < 0.75 ? 4 : 2);
        bool up = x.nz(L_CYMBALS, 300 + st, x.vk()) < 0.5;
        for (int i = 0; i < n; ++i) {
            int v = up ? 58 + 40 * i / std::max(1, n - 1) : 98 - 36 * i / std::max(1, n - 1);
            x.addT(HatClosed, st * STEP + i * 240 / n, v, 30);
        }
    }
    if (c >= 7) {
        std::string e = euclid(3, B.steps);
        for (int st = 0; st < B.steps; ++st)
            if (e[(size_t)st] == 'x') { int at = (st + 2) % B.steps; if (at != snareStep) x.add(PercHigh, at, 70); }
    }
}

// ---------------------------------------------------------------- family: IDM
// p[0]: 0 glitch (a break, then edits), 1 Euclidean layers, 2 phasing
void idm(const Ctx& x) {
    const Bar& B = x.bar;
    const int type = x.sp.p[0], c = x.c, ng = B.ng();
    if (type == 0) {
        std::vector<Hit> h;
        brk(x, &h);
        int ops = c * std::max(1, ng) / 4;
        for (int i = 0; i < ops && !h.empty(); ++i) {
            int op = (int)(x.nz(L_SNARE, 900 + i, x.vk()) * 5) % 5;
            size_t idx = (size_t)(x.nz(L_SNARE, 950 + i, x.vk()) * (double)h.size()) % h.size();
            Hit t = h[idx];
            int rel = t.tick - x.base;
            bool anchor = rel == 0 && t.role == Kick;
            if (op == 0) {                                         // ratchet
                int n = 2 + (int)(x.nz(L_SNARE, 960 + i, x.vk()) * 4);
                static const int gaps[3] = {30, 40, 60};
                int gap = gaps[(int)(x.nz(L_SNARE, 970 + i, x.vk()) * 3) % 3];
                bool up = x.nz(L_SNARE, 980 + i, x.vk()) < 0.5;
                for (int r = 1; r <= n; ++r) {
                    Hit q = t;
                    q.tick = q.grid = t.tick + r * gap;
                    q.len = std::min(q.len, gap);
                    q.vel = std::clamp(up ? t.vel - 40 + 40 * r / n : t.vel - 12 * r, 20, 120);
                    if (q.tick - x.base < B.ticks()) h.push_back(q);
                }
            } else if (op == 1 && !anchor) {                       // displace by a sixteenth
                int d = x.nz(L_SNARE, 960 + i, x.vk()) < 0.5 ? -STEP : STEP;
                if (rel + d >= 0 && rel + d < B.ticks()) { h[idx].tick += d; h[idx].grid += d; }
            } else if (op == 2 && roleLayer(t.role) == L_CYMBALS) { // drop a hat
                h.erase(h.begin() + (long)idx);
            } else if (op == 3) {                                  // stutter a slice of two sixteenths
                int s0 = (rel / (2 * STEP)) * 2 * STEP;
                std::vector<Hit> slice;
                for (auto& q : h) if (q.tick - x.base >= s0 && q.tick - x.base < s0 + 2 * STEP) slice.push_back(q);
                for (int rep = 1; rep <= 2; ++rep)
                    for (auto q : slice) {
                        q.tick += rep * 2 * STEP;
                        q.grid = q.tick;
                        q.vel = std::max(20, q.vel - 14 * rep);
                        if (q.tick - x.base < B.ticks()) h.push_back(q);
                    }
            } else if (op == 4) {                                  // a percussion stab off the grid of eighths
                int st = 1 + 2 * ((int)(x.nz(L_TOMS, 960 + i, x.vk()) * (B.steps / 2)) % std::max(1, B.steps / 2));
                if (st < B.steps) { Hit q; q.tick = q.grid = x.base + st * STEP; q.role = i % 2 ? PercLow : PercHigh; q.vel = 84; h.push_back(q); }
            }
        }
        x.out->insert(x.out->end(), h.begin(), h.end());
    } else if (type == 1) {
        int kk = std::min(B.steps, 2 + ng / 2 + c / 4), ks = std::max(1, ng / 2 + (c >= 6 ? 1 : 0));
        int kh = std::clamp(B.steps / 2 + c / 2 - 1 + (int)(x.nz(L_CYMBALS, 1) * 3), 1, B.steps);
        std::string ek = euclid(kk, B.steps), es = euclid(ks, B.steps), eh = euclid(kh, B.steps);
        int rs = B.gl[0] + (x.nz(L_SNARE, 2) < 0.5 ? 0 : 2), rh = x.nz(L_CYMBALS, 3) < 0.5 ? 0 : 1;
        for (int st = 0; st < B.steps; ++st) {
            if (ek[(size_t)st] == 'x') x.add(Kick, st, st == 0 ? 112 : 100);
            if (es[(size_t)((st - rs % B.steps + B.steps) % B.steps)] == 'x') x.add(c >= 4 ? Clap : Snare, st, 104);
            if (eh[(size_t)((st - rh + B.steps) % B.steps)] == 'x') x.add(HatClosed, st, st % 4 == 0 ? 84 : 60);
        }
        if (c >= 3) {
            // a voice whose cycle is one step shorter than the bar drifts against it
            int m = std::max(3, B.steps - 1 - 2 * ((int)(x.nz(L_TOMS, 4) * 3) % 3));
            std::string ep = euclid(2 + c / 3, m);
            for (int st = 0; st < B.steps; ++st)
                if (ep[(size_t)((x.globalStep() + st) % m)] == 'x') x.add(PercHigh, st, 76);
            if (c >= 6) {
                int m2 = std::max(3, m - 2);
                std::string e2 = euclid(2 + c / 4, m2);
                for (int st = 0; st < B.steps; ++st)
                    if (e2[(size_t)((x.globalStep() + st) % m2)] == 'x') x.add(PercLow, st, 68);
            }
        }
    } else {
        // the pattern of Clapping Music when the bar has twelve steps and the variant is the main one
        double pv = x.nz(L_TOMS, 660);
        std::string pat = B.steps == 12 && x.s.grooveVariant == 0 ? "xxx.xx.x.xx." : euclid(std::max(2, (int)std::lround(B.steps * (0.45 + 0.3 * pv))), B.steps);
        std::rotate(pat.begin(), pat.begin() + (long)((int)(x.nz(L_TOMS, 661) * B.steps) % B.steps), pat.end());
        if (pat[0] != 'x') pat[0] = 'x';
        int hold = c >= 6 ? 1 : c >= 3 ? 2 : 4;
        int shift = (x.barIndex / hold) % B.steps;
        for (int st = 0; st < B.steps; ++st) {
            if (pat[(size_t)st] == 'x') x.add(PercHigh, st, 92);
            if (pat[(size_t)((st + shift) % B.steps)] == 'x') x.add(PercLow, st, 82);
        }
        if (c >= 4) x.add(Kick, 0, 92);
        if (c >= 7) for (int g = 0; g < ng; ++g) x.add(HatClosed, B.gs[(size_t)g], 50);
    }
}

// ---------------------------------------------------------------- the style table
// New rows go at the end only: the index of a style is stored in projects.
const Spec SPECS[] = {
    // rock
    {"rock8", "Рок: восьмые", K_ROCK, beat, {BB_NORMAL, KB_ROCK, HT_8, Snare, 1, 2}, 0, 0},
    {"rock16", "Рок: шестнадцатые", K_ROCK, beat, {BB_NORMAL, KB_ROCK, HT_16, Snare, 1, 2}, 0, 0},
    {"rockhalf", "Рок: полуход", K_ROCK, beat, {BB_HALF, KB_ROCK, HT_8, Snare, 1, 2}, 0, 0},
    {"rockride", "Рок: райд", K_ROCK, beat, {BB_NORMAL, KB_ROCK, HT_RIDE, Snare, 1, 2}, 0, 0},
    {"shuffle", "Шаффл", K_ROCK, beat, {BB_NORMAL, KB_ROCK, HT_SHUFFLE, Snare, 0, 1}, 0, 0},
    {"toms", "Рок: томы", K_ROCK, beat, {BB_NORMAL, KB_ROCK, HT_TOM, Snare, 0, 1}, 0, 0},
    {"punk", "Панк: двойной темп", K_ROCK, beat, {BB_DOUBLE, KB_FOUR, HT_8, Snare, 0, 1}, 0, 0},
    // pop
    {"pop", "Поп: ровный", K_POP, beat, {BB_NORMAL, KB_ROCK, HT_8, Snare, 0, 1}, 0, 0},
    {"disco", "Поп: бочка в четверть", K_POP, beat, {BB_NORMAL, KB_FOUR, HT_OFF, Snare, 0, 1}, 0, 0},
    {"popstick", "Поп: римклик", K_POP, beat, {BB_NORMAL, KB_ROCK, HT_8, SideStick, 0, 1}, 0, 0},
    {"funk", "Фанк: шестнадцатые", K_POP, beat, {BB_NORMAL, KB_ROCK, HT_16, Snare, 3, 3}, 0, 12},
    {"motown", "Мотаун: малый на каждую долю", K_POP, beat, {BB_ALL, KB_ROCK, HT_8, Snare, 0, 1}, 0, 0},
    // metal
    {"metal8", "Метал: прямой", K_METAL, beat, {BB_NORMAL, KB_ROCK, HT_8, Snare, 0, 3}, 0, 0},
    {"double16", "Метал: двойная бочка", K_METAL, beat, {BB_NORMAL, KB_DOUBLE, HT_RIDE, Snare, 0, 0}, 0, 0},
    {"gallop", "Метал: галоп", K_METAL, beat, {BB_NORMAL, KB_GALLOP, HT_8, Snare, 0, 0}, 0, 0},
    {"thrash", "Трэш: скэнк", K_METAL, beat, {BB_DOUBLE, KB_FOUR, HT_8, Snare, 0, 1}, 0, 0},
    {"dbeat", "Ди-бит", K_METAL, beat, {BB_NORMAL, KB_DBEAT, HT_8, Snare, 0, 1}, 0, 0},
    {"breakdown", "Брейкдаун", K_METAL, beat, {BB_HALF, KB_ROCK, HT_4, Snare, 0, 3}, 0, 0},
    {"blast", "Бласт: традиционный", K_METAL, blast, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"bomb", "Бласт: с двойной бочкой", K_METAL, blast, {1, 0, 0, 0, 0, 0}, 0, 0},
    {"hammer", "Бласт: молот", K_METAL, blast, {2, 0, 0, 0, 0, 0}, 0, 0},
    // cinematic
    {"hybrid", "Кино: гибридный пульс", K_CINEMATIC, cine, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"taiko", "Кино: ансамбль тайко", K_CINEMATIC, cine, {1, 0, 0, 0, 0, 0}, 0, 0},
    {"clock", "Кино: часы", K_CINEMATIC, cine, {2, 0, 0, 0, 0, 0}, 0, 0},
    {"trailer", "Трейлер: удары", K_CINEMATIC, cine, {3, 0, 0, 0, 0, 0}, 0, 0},
    // orchestral
    {"timpani", "Оркестр: литавры и акценты", K_ORCHESTRAL, orch, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"march", "Оркестр: марш", K_ORCHESTRAL, orch, {1, 0, 0, 0, 0, 0}, 0, 0},
    {"bolero", "Оркестр: остинато малого", K_ORCHESTRAL, orch, {2, 0, 0, 0, 0, 0}, 0, 0},
    {"build", "Оркестр: нарастание", K_ORCHESTRAL, orch, {3, 0, 0, 0, 0, 0}, 0, 0},
    // electronic
    {"house", "Хаус", K_ELECTRONIC, beat, {BB_NORMAL, KB_FOUR, HT_OFF, Clap, 0, 1}, 0, 0},
    {"techno", "Техно", K_ELECTRONIC, beat, {BB_NORMAL, KB_FOUR, HT_OFF, Clap, 0, 2}, 0, 0},
    {"electro", "Электро", K_ELECTRONIC, beat, {BB_NORMAL, KB_SPARSE, HT_16, Clap, 0, 3}, 0, 0},
    {"halfstep", "Халфстеп", K_ELECTRONIC, beat, {BB_HALF, KB_ROCK, HT_8, Snare, 1, 2}, 0, 0},
    {"breakbeat", "Брейкбит", K_ELECTRONIC, brkStyle, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"dnb", "Драм-н-бейс", K_ELECTRONIC, brkStyle, {1, 0, 0, 0, 0, 0}, 0, 0},
    {"trap", "Трэп", K_ELECTRONIC, trap, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"glitch", "IDM: глитч", K_ELECTRONIC, idm, {0, 0, 0, 0, 0, 0}, 0, 0},
    {"euclid", "IDM: евклидовы слои", K_ELECTRONIC, idm, {1, 0, 0, 0, 0, 0}, 0, 0},
    {"phase", "IDM: фазовый сдвиг", K_ELECTRONIC, idm, {2, 0, 0, 0, 0, 0}, 0, 0},
};
const int NUM_SPECS = (int)(sizeof(SPECS) / sizeof(SPECS[0]));

}  // namespace

const Spec& spec(int style) { return SPECS[std::clamp(style, 0, NUM_SPECS - 1)]; }
int numStyles() { return NUM_SPECS; }
const char* styleId(int s) { return spec(s).id; }
const char* styleLabel(int s) { return spec(s).label; }
int styleKit(int s) { return spec(s).kit; }
int findStyle(const std::string& id) {
    for (int i = 0; i < NUM_SPECS; ++i) if (id == SPECS[i].id) return i;
    return -1;
}
const char* kitLabel(int kit) {
    static const char* n[] = {"Рок", "Поп", "Метал", "Кино", "Оркестр", "Электроника"};
    return n[std::clamp(kit, 0, NUM_KITS - 1)];
}
const char* polyLabel(int p) {
    static const char* n[] = {"Выключен", "Цикл ног", "N на такт", "Группы по N", "Метрическая модуляция", "Тала: тихаи", "Евклид"};
    return n[std::clamp(p, 0, NUM_POLY - 1)];
}
const char* roleName(int r) {
    static const char* n[] = {"Kick", "Snare", "Side Stick", "Clap", "Hi-Hat Closed", "Hi-Hat Pedal", "Hi-Hat Open", "Tom High",
        "Tom Mid", "Tom Floor", "Ride", "Ride Bell", "Crash 1", "Crash 2", "Splash", "China", "Tambourine", "Shaker",
        "Cowbell", "Perc High", "Perc Low", "Timpani", "Bass Drum", "Orch Snare", "Tenor Drum", "Piatti", "Sus Cymbal",
        "Tam Tam", "Triangle", "Taiko High", "Taiko Low", "Anvil", "Ticker"};
    return n[std::clamp(r, 0, NUM_ROLES - 1)];
}
int roleLayer(int r) {
    switch (r) {
        case Kick: case BassDrum: case TaikoLow: case Timpani: return L_LOW;
        case Snare: case SideStick: case Clap: case OrchSnare: case TenorDrum: case TaikoHigh: return L_SNARE;
        case TomHigh: case TomMid: case TomFloor: case PercHigh: case PercLow: case Cowbell: case Anvil: return L_TOMS;
        default: return L_CYMBALS;
    }
}

}  // namespace drums
