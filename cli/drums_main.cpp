// Command-line front end of the engine: every check goes through it.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include "Drums.h"

using namespace drums;

static void usage() {
    std::puts("drums_cli [--list] [--style=rock8] [--kind=groove|fill|break|phrase] [--meter=4/4] [--bpm=120] [--bars=4]\n"
              "          [--complexity=5] [--feel=3] [--swing=0] [--variant=0] [--fill-variant=0] [--break-variant=0]\n"
              "          [--fill-every=4] [--fill-len=0..3] [--no-landing] [--poly=off|feet|cross|groups|modulation|tala|euclid]\n"
              "          [--poly-n=5] [--tonic=0] [--map=gm|duplantier|bbcso] [--per-instrument] [--follow=in.mid]\n"
              "          [--out=out.mid] [--grid] [--hash]");
}

// text picture of the result: one row per instrument, one column per sixteenth
static void printGrid(const Result& r) {
    std::map<int, std::string> rows;
    int steps = r.length / STEP;
    for (auto& h : r.hits) {
        auto& row = rows[h.role];
        if (row.empty()) row.assign((size_t)steps + 1, '.');
        int st = std::min(steps, (h.grid + STEP / 2) / STEP);
        char ch = h.grid % STEP ? '+' : (h.vel >= 100 ? 'X' : h.vel >= 60 ? 'x' : 'o');
        if (row[(size_t)st] == '.' || ch == 'X') row[(size_t)st] = ch;
    }
    int barSteps = 1;
    {
        Meter m = r.meter;
        barSteps = m.num * 16 / m.den;
    }
    for (auto& kv : rows) {
        std::printf("%-14s ", roleName(kv.first));
        for (int i = 0; i <= steps; ++i) {
            if (i && i % barSteps == 0) std::printf("|");
            std::printf("%c", kv.second[(size_t)i]);
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    Settings s;
    Kind kind = Kind::Groove;
    MidiOptions mo;
    std::string out, follow;
    bool grid = false, hash = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&](const char* key) -> const char* {
            size_t n = std::strlen(key);
            return a.compare(0, n, key) == 0 ? a.c_str() + n : nullptr;
        };
        const char* v;
        if (a == "--list") {
            for (int k = 0; k < numStyles(); ++k) std::printf("%-10s %s (%s)\n", styleId(k), styleLabel(k), kitLabel(styleKit(k)));
            return 0;
        } else if (a == "--help") { usage(); return 0; }
        else if ((v = val("--style="))) {
            s.style = findStyle(v);
            if (s.style < 0) { std::fprintf(stderr, "unknown style %s\n", v); return 2; }
        } else if ((v = val("--kind="))) {
            std::string k = v;
            kind = k == "fill" ? Kind::Fill : k == "break" ? Kind::Break : k == "phrase" ? Kind::Phrase : Kind::Groove;
        } else if ((v = val("--meter="))) { if (std::sscanf(v, "%d/%d", &s.meter.num, &s.meter.den) != 2) { usage(); return 2; } }
        else if ((v = val("--bpm="))) s.bpm = std::atof(v);
        else if ((v = val("--bars="))) s.bars = std::atoi(v);
        else if ((v = val("--complexity="))) s.complexity = std::atoi(v);
        else if ((v = val("--feel="))) s.feel = std::atoi(v);
        else if ((v = val("--swing="))) s.swing = std::atoi(v);
        else if ((v = val("--variant="))) s.grooveVariant = std::atoi(v);
        else if ((v = val("--fill-variant="))) s.fillVariant = std::atoi(v);
        else if ((v = val("--break-variant="))) s.breakVariant = std::atoi(v);
        else if ((v = val("--fill-every="))) s.fillEvery = std::atoi(v);
        else if ((v = val("--fill-len="))) s.fillLen = std::atoi(v);
        else if (a == "--no-landing") s.landing = false;
        else if ((v = val("--poly="))) {
            static const char* names[] = {"off", "feet", "cross", "groups", "modulation", "tala", "euclid"};
            s.poly = -1;
            for (int k = 0; k < NUM_POLY; ++k) if (std::string(v) == names[k]) s.poly = k;
            if (s.poly < 0) { usage(); return 2; }
        } else if ((v = val("--poly-n="))) s.polyN = std::atoi(v);
        else if ((v = val("--tonic="))) s.tonic = std::atoi(v);
        else if ((v = val("--map="))) { std::string m = v; mo.map = m == "duplantier" ? M_DUPLANTIER : m == "bbcso" ? M_BBCSO : M_GM; }
        else if (a == "--per-instrument") mo.perInstrument = true;
        else if ((v = val("--follow="))) follow = v;
        else if ((v = val("--out="))) out = v;
        else if (a == "--grid") grid = true;
        else if (a == "--hash") hash = true;
        else { usage(); return 2; }
    }
    if (!follow.empty()) {
        std::ifstream f(follow, std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Meter m;
        double bpm = 120;
        if (!readMidi(bytes.data(), bytes.size(), s.follow, m, bpm)) { std::fprintf(stderr, "not a MIDI file: %s\n", follow.c_str()); return 2; }
    }
    Result r = generate(kind, s);
    if (!r.ok()) { std::fprintf(stderr, "%s\n", r.error.c_str()); return 1; }
    std::cout << r.report;
    if (grid) printGrid(r);
    if (hash) std::printf("hash %08x hits %zu\n", soundHash(r), r.hits.size());
    if (!out.empty()) {
        auto bytes = writeMidi(r, mo);
        std::ofstream f(out, std::ios::binary);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    }
    return 0;
}
