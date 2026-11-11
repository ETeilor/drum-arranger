// generate(): grooves, then fills, breaks and landings on top, then the pass that makes a kit part
// playable by two hands and two feet, then swing and feel.
#include <map>
#include <tuple>
#include "Internal.h"

namespace drums {

namespace {

bool isFoot(int role) { return role == Kick || role == HatPedal; }
bool isGrace(const Hit& h) { return h.len <= 20; }

int handPriority(int role) {
    switch (role) {
        case Crash1: case Crash2: case China: case Splash: return 6;
        case Snare: case SideStick: return 5;
        case TomHigh: case TomMid: case TomFloor: return 4;
        case Cowbell: case RideBell: return 3;
        case Ride: case HatOpen: return 2;
        default: return 1;
    }
}

double ticksPerMs(double bpm) { return bpm * TPB / 60000.0; }

void dedupe(std::vector<Hit>& hits) {
    std::map<std::tuple<int, int, int>, Hit> best;
    for (auto& h : hits) {
        auto key = std::make_tuple(h.tick, h.role, h.pitch);
        auto it = best.find(key);
        if (it == best.end() || it->second.vel < h.vel) best[key] = h;
    }
    hits.clear();
    for (auto& kv : best) hits.push_back(kv.second);
}

// two hands, two feet: at any instant keep the two most important hand strokes; the hi-hat foot gives
// way to a struck hi-hat and to fast double kicks
void makePlayable(std::vector<Hit>& hits, double bpm) {
    std::map<int, std::vector<size_t>> at;
    for (size_t i = 0; i < hits.size(); ++i) at[hits[i].grid].push_back(i);
    std::vector<char> drop(hits.size(), 0);
    std::vector<int> kicks;
    for (auto& h : hits) if (h.role == Kick) kicks.push_back(h.grid);
    std::sort(kicks.begin(), kicks.end());
    const int fast = (int)(110 * ticksPerMs(bpm));
    for (auto& kv : at) {
        std::vector<size_t> hands;
        bool hatStruck = false;
        for (size_t i : kv.second) {
            if (!isFoot(hits[i].role) && !isGrace(hits[i])) hands.push_back(i);
            if (hits[i].role == HatOpen || hits[i].role == HatClosed) hatStruck = true;
        }
        bool open = false;
        for (size_t i : kv.second) if (hits[i].role == HatOpen) open = true;
        for (size_t i : kv.second) if (open && hits[i].role == HatClosed) drop[i] = 1;
        hands.erase(std::remove_if(hands.begin(), hands.end(), [&](size_t i) { return drop[i] != 0; }), hands.end());
        if (hands.size() > 2) {
            std::stable_sort(hands.begin(), hands.end(), [&](size_t a, size_t b) {
                int pa = handPriority(hits[a].role), pb = handPriority(hits[b].role);
                return pa != pb ? pa > pb : hits[a].vel > hits[b].vel;
            });
            for (size_t j = 2; j < hands.size(); ++j) drop[hands[j]] = 1;
        }
        for (size_t i : kv.second) {
            if (hits[i].role != HatPedal) continue;
            auto lo = std::lower_bound(kicks.begin(), kicks.end(), kv.first - fast);
            bool busyFeet = false;
            for (auto it = lo; it != kicks.end() && *it <= kv.first + fast; ++it) if (*it != kv.first) busyFeet = true;
            if (hatStruck || busyFeet) drop[i] = 1;
        }
    }
    std::vector<Hit> kept;
    for (size_t i = 0; i < hits.size(); ++i) if (!drop[i]) kept.push_back(hits[i]);
    hits.swap(kept);
}

std::string num(double v) {
    char b[32];
    if (std::fabs(v - std::round(v)) < 0.05) std::snprintf(b, sizeof b, "%d", (int)std::lround(v));
    else std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

}  // namespace

std::vector<std::string> playabilityProblems(const Result& r, int kit) {
    std::vector<std::string> out;
    if (!acousticKit(kit)) return out;
    Bar bar = makeBar(r.meter);
    auto where = [&](int tick) {
        return "такт " + std::to_string(tick / bar.ticks() + 1) + ", шестнадцатая " + std::to_string((tick % bar.ticks()) / STEP + 1);
    };
    std::map<int, int> hands;
    std::map<int, bool> pedal, struck;
    std::vector<int> kicks, handTicks;
    for (auto& h : r.hits) {
        if (isGrace(h)) continue;
        if (h.role == Kick) kicks.push_back(h.grid);
        else if (h.role == HatPedal) pedal[h.grid] = true;
        else { ++hands[h.grid]; handTicks.push_back(h.grid); }
        if (h.role == HatOpen || h.role == HatClosed) struck[h.grid] = true;
    }
    for (auto& kv : hands) if (kv.second > 2) out.push_back(where(kv.first) + ": три удара руками сразу");
    for (auto& kv : pedal) if (struck.count(kv.first)) out.push_back(where(kv.first) + ": педаль хэта вместе с ударом по хэту");
    std::sort(handTicks.begin(), handTicks.end());
    handTicks.erase(std::unique(handTicks.begin(), handTicks.end()), handTicks.end());
    for (size_t i = 0; i + 4 < handTicks.size(); ++i) {
        double sec = (handTicks[i + 4] - handTicks[i]) / (ticksPerMs(r.bpm) * 1000.0);
        if (sec > 0 && 4.0 / sec > 22.0) { out.push_back(where(handTicks[i]) + ": руки быстрее 22 ударов в секунду"); break; }
    }
    return out;
}

uint32_t soundHash(const Result& r) {
    uint32_t h = 17;
    for (auto& x : r.hits) h = mix(mix(mix(h, (uint32_t)x.grid), (uint32_t)x.role), (uint32_t)(x.vel / 12 + (x.pitch + 1) * 16));
    return h;
}

void addLanding(const Settings& s, int kit, int tick, std::vector<Hit>& hits) {
    KitRoles k = kitRoles(kit);
    auto put = [&](int role, int vel, int pitch = -1) {
        Hit h;
        h.tick = h.grid = tick;
        h.role = role;
        h.vel = vel;
        h.len = defaultLen(role);
        h.pitch = pitch;
        hits.push_back(h);
    };
    put(k.cym, 116);
    put(k.low, 118);
    if (kit == K_CINEMATIC) put(TaikoLow, 118);
    if (kit == K_ORCHESTRAL) put(Timpani, 110, timpaniPitch(s.tonic));
}

Result generate(Kind kind, const Settings& in) {
    Settings s = in;
    Result r;
    r.meter = s.meter;
    r.bpm = s.bpm;
    if (s.meter.den != 2 && s.meter.den != 4 && s.meter.den != 8 && s.meter.den != 16) {
        r.error = "Размер: знаменатель должен быть 2, 4, 8 или 16";
        return r;
    }
    if (s.meter.num < 1 || s.meter.num > 32) {
        r.error = "Размер: числитель от 1 до 32";
        return r;
    }
    s.style = std::clamp(s.style, 0, numStyles() - 1);
    s.complexity = std::clamp(s.complexity, 0, 10);
    s.feel = std::clamp(s.feel, 0, 10);
    s.swing = std::clamp(s.swing, 0, 100);
    s.bars = std::clamp(s.bars, 1, 64);
    s.bpm = std::clamp(s.bpm, 20.0, 400.0);
    s.fillLen = std::clamp(s.fillLen, 0, 3);
    s.polyN = std::clamp(s.polyN, 2, 32);
    s.tonic = ((s.tonic % 12) + 12) % 12;

    const Spec& sp = spec(s.style);
    const int kit = sp.kit;
    const Bar bar = makeBar(s.meter);
    const int BT = bar.ticks();
    const int nBars = (kind == Kind::Groove || kind == Kind::Phrase) ? s.bars : (kind == Kind::Fill && s.fillLen == 3 ? 2 : 1);
    std::vector<Hit> hits;
    std::string rep;

    // ---- 1. the groove
    const bool modulate = s.poly == P_MODULATION && (kind == Kind::Groove || kind == Kind::Phrase) && nBars >= 2;
    const int split = modulate ? nBars - nBars / 2 : nBars;
    for (int b = 0; b < split; ++b) {
        Ctx x{s, sp, bar, b, nBars, b * BT, s.complexity, &hits};
        sp.fn(x);
    }
    if (modulate) {
        // the second half is one long span whose beat is N sixteenths, running over the bar lines
        Bar lb;
        lb.steps = (nBars - split) * bar.steps;
        for (int st = 0; st < lb.steps; st += s.polyN) {
            int len = std::min(s.polyN, lb.steps - st);
            if (len == 1 && !lb.gl.empty()) { lb.gl.back() += 1; break; }
            lb.gs.push_back(st);
            lb.gl.push_back(len);
        }
        Ctx x{s, sp, lb, 0, 1, split * BT, s.complexity, &hits};
        sp.fn(x);
        rep += "Модуляция: с такта " + std::to_string(split + 1) + " пульс идёт каждые " + std::to_string(s.polyN) +
               " шестнадцатых. Ноты записаны в прежнем темпе, ощущаемый темп " + num(s.bpm * 4.0 / s.polyN) + " BPM\n";
    }
    // ---- 2. the kick follows a rhythm source
    if (!s.follow.empty()) {
        KitRoles k = kitRoles(kit);
        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.role == k.low || h.role == Kick; }), hits.end());
        int count = 0;
        for (auto& n : s.follow) {
            int t = ((n.start + STEP / 2) / STEP) * STEP;
            if (t < 0 || t >= nBars * BT) continue;
            Hit h;
            h.tick = h.grid = t;
            h.role = k.low;
            h.vel = std::clamp(n.vel + 8, 60, 120);
            h.len = defaultLen(k.low);
            hits.push_back(h);
            ++count;
        }
        rep += "Бочка идёт за источником ритма: " + std::to_string(count) + " атак\n";
    }
    // ---- 3. polyrhythm layers
    if (kind != Kind::Break) applyPoly(s, bar, kit, nBars, hits, rep);

    // ---- 4. fills, breaks, landings
    auto fillTicks = [&](int len) {
        int ng = bar.ng();
        if (len == 0) return bar.gl[(size_t)(ng - 1)] * STEP;
        if (len == 1) return ng >= 2 ? (bar.gl[(size_t)(ng - 1)] + bar.gl[(size_t)(ng - 2)]) * STEP : BT;
        return len == 2 ? BT : 2 * BT;
    };
    auto placeFill = [&](int endTick, int lenTicks, int variant) {
        lenTicks = std::min(lenTicks, endTick);
        int start = endTick - lenTicks;
        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.tick >= start && h.tick < endTick; }), hits.end());
        FillEnv e{s, bar, kit, start, lenTicks, variant, &hits, &rep};
        if (s.poly == P_TALA && buildTihai(e)) return;       // a tihai lands by itself
        buildFill(e);
        if (s.landing) addLanding(s, kit, endTick, hits);
    };
    if (kind == Kind::Fill) placeFill(nBars * BT, fillTicks(s.fillLen), s.fillVariant);
    if (kind == Kind::Phrase && s.fillEvery > 0) {
        int k = 0;
        for (int end = s.fillEvery; end <= nBars; end += s.fillEvery, ++k) {
            // the fill that closes the whole phrase is one size longer
            int len = (end + s.fillEvery > nBars && s.fillLen < 2) ? s.fillLen + 1 : s.fillLen;
            placeFill(end * BT, std::min(fillTicks(len), s.fillEvery * BT), s.fillVariant + k);
        }
    }
    if (kind == Kind::Break) {
        buildBreak(s, bar, kit, 0, hits, rep);
        if (s.landing) addLanding(s, kit, BT, hits);
    }
    r.length = nBars * BT;

    // ---- 5. playable, then swing and feel
    hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& h) { return h.tick < 0 || h.tick > r.length; }), hits.end());
    dedupe(hits);
    if (acousticKit(kit)) makePlayable(hits, s.bpm);
    const int sw8 = std::clamp(sp.swing8 + s.swing, 0, 100), sw16 = std::clamp(sp.swing16 + s.swing / 2, 0, 100);
    const bool canSwing = s.meter.den <= 4;
    const double looseness = (acousticKit(kit) || kit == K_CINEMATIC || kit == K_ORCHESTRAL) ? 1.0 : 0.3;
    for (auto& h : hits) {
        if (h.grid == r.length) continue;                    // a landing stays on the bar line
        int t = h.grid;
        if (canSwing) {
            if (t % TPB == TPB / 2) t += sw8 * 80 / 100;
            else if (t % (TPB / 2) == STEP) t += sw16 * 40 / 100;
        }
        uint32_t hv = mix(mix((uint32_t)h.grid, (uint32_t)(h.role * 131 + 7)), (uint32_t)(h.pitch + 1));
        t += (int)std::lround((unit(hv) - 0.5) * 2.0 * s.feel * looseness);
        if ((kit == K_ROCK || kit == K_POP) && h.role == Snare && h.vel >= 100) t += s.feel / 2;   // the backbeat sits back
        h.tick = std::clamp(t, 0, r.length - 1);
        h.vel = std::clamp(h.vel + (int)std::lround((unit(mix(hv, 99)) - 0.5) * 2.0 * s.feel * 0.8), 1, 127);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.tick != b.tick ? a.tick < b.tick : a.role < b.role; });
    r.hits = hits;

    // ---- 6. report
    static const char* kinds[] = {"Грув", "Филл", "Брейк", "Фраза"};
    std::string head = std::string(kinds[(int)kind]) + ": " + sp.label + " · " + meterGrouping(s.meter) + " · " + num(s.bpm) +
                       " BPM · тактов: " + std::to_string(nBars) + " · сложность " + std::to_string(s.complexity) + "\n";
    int perLayer[NUM_LAYERS] = {0, 0, 0, 0};
    for (auto& h : r.hits) ++perLayer[roleLayer(h.role)];
    head += "Ударов " + std::to_string(r.hits.size()) + ": низ " + std::to_string(perLayer[L_LOW]) + ", малый " +
            std::to_string(perLayer[L_SNARE]) + ", тарелки " + std::to_string(perLayer[L_CYMBALS]) + ", томы и перкуссия " +
            std::to_string(perLayer[L_TOMS]) + "\n";
    r.report = head + rep;
    for (auto& p : playabilityProblems(r, kit)) r.report += "Проверьте: " + p + "\n";
    return r;
}

LiveBars liveBars(const Settings& s) {
    LiveBars lb;
    Bar bar = makeBar(s.meter);
    lb.barTicks = bar.ticks();
    Result g = generate(Kind::Groove, s);
    if (!g.ok()) return lb;
    lb.groove.resize((size_t)std::max(1, g.length / lb.barTicks));
    for (auto h : g.hits) {
        int b = std::min((int)lb.groove.size() - 1, h.tick / lb.barTicks);
        h.tick -= b * lb.barTicks;
        lb.groove[(size_t)b].push_back(h);
    }
    Settings f = s;
    f.fillLen = std::min(f.fillLen, 2);
    lb.fill = generate(Kind::Fill, f).hits;
    lb.brk = generate(Kind::Break, s).hits;
    return lb;
}

}  // namespace drums
