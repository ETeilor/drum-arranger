// Key maps of the libraries and the Standard MIDI File reader and writer.
#include <map>
#include "Internal.h"

namespace drums {

namespace {

struct KeyMap {
    const char* label;
    int note[NUM_ROLES];
};

// -1: the library has no such instrument, the nearest relative is used instead (see ALT)
KeyMap makeMap(const char* label, std::initializer_list<std::pair<int, int>> keys) {
    KeyMap m;
    m.label = label;
    for (int& n : m.note) n = -1;
    for (auto& kv : keys) m.note[kv.first] = kv.second;
    return m;
}

const KeyMap& keyMap(int map) {
    static const KeyMap maps[NUM_MAPS] = {
        // General MIDI percussion; Logic's Drum Kit Designer and Drum Machine Designer follow it
        makeMap("GM · Logic Drum Kit Designer", {{Kick, 36}, {Snare, 38}, {SideStick, 37}, {Clap, 39}, {HatClosed, 42},
            {HatPedal, 44}, {HatOpen, 46}, {TomHigh, 50}, {TomMid, 47}, {TomFloor, 43}, {Ride, 51}, {RideBell, 53},
            {Crash1, 49}, {Crash2, 57}, {Splash, 55}, {China, 52}, {Tambourine, 54}, {Shaker, 70}, {Cowbell, 56},
            {PercHigh, 63}, {PercLow, 64}, {BassDrum, 35}, {Triangle, 81}, {TaikoLow, 41}, {TaikoHigh, 45}, {Ticker, 76}}),
        // MixWave Gojira: Mario Duplantier, default mapping from the user guide (C1 = 36)
        makeMap("MixWave Mario Duplantier", {{Kick, 36}, {Snare, 38}, {SideStick, 37}, {HatClosed, 42}, {HatPedal, 44},
            {HatOpen, 46}, {TomHigh, 50}, {TomMid, 47}, {TomFloor, 43}, {Ride, 51}, {RideBell, 53}, {Crash1, 49},
            {Crash2, 57}, {Splash, 55}, {China, 54}, {Cowbell, 56}, {PercHigh, 12}, {PercLow, 14}}),
        // Spitfire BBC Symphony Orchestra, the Untuned Percussion patch (C2..C3 with middle C = C3 = 60)
        makeMap("BBC SO Untuned Percussion", {{BassDrum, 48}, {TenorDrum, 50}, {OrchSnare, 52}, {SusCymbal, 53},
            {Anvil, 57}, {TamTam, 59}, {Tambourine, 60}}),
    };
    return maps[std::clamp(map, 0, NUM_MAPS - 1)];
}

// nearest relatives of a role, tried in order when a library lacks it
const int ALT[NUM_ROLES][3] = {
    /*Kick*/ {BassDrum, -1, -1}, /*Snare*/ {OrchSnare, -1, -1}, /*SideStick*/ {Snare, -1, -1}, /*Clap*/ {Snare, Tambourine, -1},
    /*HatClosed*/ {Ticker, Tambourine, Triangle}, /*HatPedal*/ {HatClosed, -1, -1}, /*HatOpen*/ {HatClosed, SusCymbal, -1},
    /*TomHigh*/ {TenorDrum, Snare, -1}, /*TomMid*/ {TenorDrum, TomHigh, -1}, /*TomFloor*/ {TenorDrum, TomMid, BassDrum},
    /*Ride*/ {HatClosed, SusCymbal, -1}, /*RideBell*/ {Ride, Triangle, -1}, /*Crash1*/ {Piatti, SusCymbal, -1},
    /*Crash2*/ {Crash1, -1, -1}, /*Splash*/ {Crash1, -1, -1}, /*China*/ {Crash1, TamTam, -1}, /*Tambourine*/ {HatClosed, -1, -1},
    /*Shaker*/ {Tambourine, HatClosed, -1}, /*Cowbell*/ {Anvil, RideBell, -1}, /*PercHigh*/ {TomHigh, -1, -1},
    /*PercLow*/ {TomMid, -1, -1}, /*Timpani*/ {TomFloor, BassDrum, -1}, /*BassDrum*/ {Kick, -1, -1}, /*OrchSnare*/ {Snare, -1, -1},
    /*TenorDrum*/ {TomMid, -1, -1}, /*Piatti*/ {Crash1, SusCymbal, -1}, /*SusCymbal*/ {Crash2, Crash1, -1}, /*TamTam*/ {China, -1, -1},
    /*Triangle*/ {RideBell, Tambourine, -1}, /*TaikoHigh*/ {TomMid, TenorDrum, -1}, /*TaikoLow*/ {TomFloor, BassDrum, -1},
    /*Anvil*/ {Cowbell, -1, -1}, /*Ticker*/ {HatClosed, SideStick, Tambourine}};

int resolve(const KeyMap& m, int role, int depth) {
    if (role < 0) return -1;
    if (m.note[role] >= 0) return m.note[role];
    if (depth <= 0) return -1;
    for (int a : ALT[role]) {
        int n = resolve(m, a, depth - 1);
        if (n >= 0) return n;
    }
    return -1;
}

void putVlq(std::vector<unsigned char>& out, uint32_t v) {
    unsigned char buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    while (v >>= 7) buf[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n) out.push_back(buf[--n]);
}
void put32(std::vector<unsigned char>& o, uint32_t v) { for (int sh = 24; sh >= 0; sh -= 8) o.push_back((unsigned char)(v >> sh)); }
void putTrack(std::vector<unsigned char>& out, const std::vector<unsigned char>& body) {
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    put32(out, (uint32_t)body.size());
    out.insert(out.end(), body.begin(), body.end());
}
void putName(std::vector<unsigned char>& body, const std::string& name) {
    body.insert(body.end(), {0x00, 0xFF, 0x03});
    putVlq(body, (uint32_t)name.size());
    body.insert(body.end(), name.begin(), name.end());
}

struct Ev {
    int tick;
    bool on;
    int note, vel;
};
std::vector<unsigned char> noteTrack(const std::string& name, std::vector<Ev> evs, int channel, int endTick) {
    std::vector<unsigned char> body;
    putName(body, name);
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on); });
    int t = 0;
    for (auto& e : evs) {
        putVlq(body, (uint32_t)(e.tick - t));
        t = e.tick;
        body.push_back((unsigned char)((e.on ? 0x90 : 0x80) | channel));
        body.push_back((unsigned char)e.note);
        body.push_back((unsigned char)(e.on ? e.vel : 0));
    }
    putVlq(body, (uint32_t)std::max(0, endTick - t));
    body.insert(body.end(), {0xFF, 0x2F, 0x00});
    return body;
}

}  // namespace

const char* mapLabel(int map) { return keyMap(map).label; }

int noteFor(int map, int role) {
    int n = resolve(keyMap(map), std::clamp(role, 0, NUM_ROLES - 1), 3);
    return n >= 0 ? n : 36;
}

std::vector<unsigned char> writeMidi(const Result& r, const MidiOptions& o) {
    // group the hits into tracks: timpani always get their own, with real pitches
    struct Trk { std::string name; int channel; std::vector<Ev> evs; };
    std::map<int, Trk> tracks;   // key: order
    std::map<std::pair<int, int>, int> lastOn;   // (track, note) -> index of its last note-off event
    for (auto& h : r.hits) {
        if (o.only >= 0 && h.role != o.only) continue;
        bool pitched = h.role == Timpani && h.pitch >= 0;
        int key = pitched ? 1000 : (o.perInstrument ? h.role : 0);
        auto& t = tracks[key];
        if (t.name.empty()) {
            t.name = pitched ? "Timpani" : (o.perInstrument ? roleName(h.role) : "Drums");
            t.channel = pitched ? 0 : 9;
        }
        int note = pitched ? h.pitch : noteFor(o.map, h.role);
        // a note that is struck again ends first
        auto lk = std::make_pair(key, note);
        auto it = lastOn.find(lk);
        if (it != lastOn.end() && t.evs[(size_t)it->second].tick > h.tick) t.evs[(size_t)it->second].tick = h.tick;
        t.evs.push_back({h.tick, true, note, h.vel});
        t.evs.push_back({h.tick + std::max(1, h.len), false, note, 0});
        lastOn[lk] = (int)t.evs.size() - 1;
    }
    int endTick = r.length;
    for (auto& kv : tracks) for (auto& e : kv.second.evs) endTick = std::max(endTick, e.tick);

    std::vector<unsigned char> out{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    out.push_back((unsigned char)(((int)tracks.size() + 1) >> 8));
    out.push_back((unsigned char)(((int)tracks.size() + 1) & 0xFF));
    out.push_back((unsigned char)(TPB >> 8));
    out.push_back((unsigned char)(TPB & 0xFF));

    std::vector<unsigned char> meta;
    putName(meta, "Drum Arranger");
    uint32_t us = (uint32_t)std::lround(60000000.0 / std::max(1.0, r.bpm));
    meta.insert(meta.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(us >> 16), (unsigned char)(us >> 8), (unsigned char)us});
    int dd = 0;
    for (int d = r.meter.den; d > 1; d >>= 1) ++dd;
    meta.insert(meta.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)r.meter.num, (unsigned char)dd, 24, 8});
    putVlq(meta, (uint32_t)endTick);
    meta.insert(meta.end(), {0xFF, 0x2F, 0x00});
    putTrack(out, meta);
    for (auto& kv : tracks) putTrack(out, noteTrack(kv.second.name, kv.second.evs, kv.second.channel, endTick));
    return out;
}

bool readMidi(const unsigned char* b, size_t size, std::vector<SourceNote>& notes, Meter& meter, double& bpm) {
    notes.clear();
    if (size < 14 || b[0] != 'M' || b[1] != 'T' || b[2] != 'h' || b[3] != 'd') return false;
    auto u32 = [&](size_t p) { return ((uint32_t)b[p] << 24) | ((uint32_t)b[p + 1] << 16) | ((uint32_t)b[p + 2] << 8) | b[p + 3]; };
    size_t p = 8 + u32(4);
    int division = (b[12] << 8) | b[13];
    if (division <= 0 || (division & 0x8000)) division = 480;
    bool gotTempo = false, gotMeter = false;
    while (p + 8 <= size) {
        bool isTrack = b[p] == 'M' && b[p + 1] == 'T' && b[p + 2] == 'r' && b[p + 3] == 'k';
        size_t len = u32(p + 4), q = p + 8, end = std::min(size, q + len);
        p = end;
        if (!isTrack) continue;
        long long tick = 0;
        int status = 0;
        std::map<int, std::pair<long long, int>> open;   // channel*128 + note -> start, velocity
        auto vlq = [&]() {
            uint32_t v = 0;
            while (q < end) {
                unsigned char c = b[q++];
                v = (v << 7) | (c & 0x7F);
                if (!(c & 0x80)) break;
            }
            return v;
        };
        while (q < end) {
            tick += vlq();
            if (q >= end) break;
            unsigned char c = b[q];
            if (c == 0xFF) {
                if (q + 2 > end) break;
                int type = b[q + 1];
                q += 2;
                uint32_t l = vlq();
                if (type == 0x51 && l == 3 && q + 3 <= end && !gotTempo) {
                    uint32_t us = ((uint32_t)b[q] << 16) | ((uint32_t)b[q + 1] << 8) | b[q + 2];
                    if (us) { bpm = 60000000.0 / us; gotTempo = true; }
                }
                if (type == 0x58 && l >= 2 && q + 2 <= end && !gotMeter) {
                    meter.num = b[q];
                    meter.den = 1 << b[q + 1];
                    gotMeter = true;
                }
                q += l;
                continue;
            }
            if (c == 0xF0 || c == 0xF7) { ++q; uint32_t l = vlq(); q += l; continue; }
            if (c & 0x80) { status = c; ++q; }
            int hi = status & 0xF0, ch = status & 0x0F;
            if (hi == 0xC0 || hi == 0xD0) { ++q; continue; }
            if (q + 2 > end) break;
            int d1 = b[q], d2 = b[q + 1];
            q += 2;
            if (hi == 0x90 && d2 > 0) open[ch * 128 + d1] = {tick, d2};
            else if (hi == 0x80 || hi == 0x90) {
                auto it = open.find(ch * 128 + d1);
                if (it != open.end()) {
                    SourceNote n;
                    n.start = (int)(it->second.first * TPB / division);
                    n.end = (int)(tick * TPB / division);
                    n.pitch = d1;
                    n.vel = it->second.second;
                    notes.push_back(n);
                    open.erase(it);
                }
            }
        }
    }
    std::stable_sort(notes.begin(), notes.end(), [](const SourceNote& a, const SourceNote& b2) { return a.start < b2.start; });
    return true;
}

}  // namespace drums
