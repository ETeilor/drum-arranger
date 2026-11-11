#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "Drums.h"

namespace drums {

// One bar on a grid of sixteenths, split into beat groups (4/4 = 4+4+4+4, 7/8 = 4+4+6 sixteenths).
struct Bar {
    int steps = 16;
    std::vector<int> gs, gl;   // group starts and lengths, in sixteenths
    int ticks() const { return steps * STEP; }
    int ng() const { return (int)gs.size(); }
    int groupOf(int step) const {
        for (int g = ng() - 1; g >= 0; --g)
            if (step >= gs[(size_t)g]) return g;
        return 0;
    }
    int mid(int g) const { return gs[(size_t)g] + gl[(size_t)g] / 2; }
    // a position given on a 4-step beat, scaled to the length of this group
    int at(int g, int pos4) const {
        int l = gl[(size_t)g];
        return gs[(size_t)g] + std::min(l - 1, pos4 * l / 4);
    }
};
Bar makeBar(const Meter& m);

uint32_t mix(uint32_t a, uint32_t b);
inline double unit(uint32_t h) { return (double)(h >> 8) / 16777216.0; }

struct Spec;

// what a style writes into: one bar at a time
struct Ctx {
    const Settings& s;
    const Spec& sp;
    Bar bar;
    int barIndex = 0, nBars = 1;
    int base = 0;              // tick of the bar start
    int c = 5;                 // complexity
    std::vector<Hit>* out = nullptr;

    double nz(int layer, int a, int b = 0) const;
    // bars vary more as the complexity grows: every bar the same, two alternating, four different
    int vk() const { return barIndex % (c >= 7 ? 4 : c >= 4 ? 2 : 1); }
    int globalStep() const { return barIndex * bar.steps; }
    void add(int role, int step, int vel, int len = 0) const { addT(role, step * STEP, vel, len); }
    void addT(int role, int tick, int vel, int len = 0, int pitch = -1) const;
};

struct Spec {
    const char* id;
    const char* label;
    int kit;
    void (*fn)(const Ctx&);
    int p[6];
    int swing8, swing16;       // what the style swings by itself, 0..100
};
const Spec& spec(int style);

// the drums a fill or a break is laid out on, per kit
struct KitRoles {
    int snare, t1, t2, t3, cym, low, hat;
};
KitRoles kitRoles(int kit);
inline bool acousticKit(int kit) { return kit == K_ROCK || kit == K_POP || kit == K_METAL; }
int defaultLen(int role);
int timpaniPitch(int pitchClass);          // inside the range where timpani sound best
int timpaniDominant(int tonicPitch);

// Fills.cpp
struct FillEnv {
    const Settings& s;
    Bar bar;
    int kit;
    int base;        // tick where the fill starts
    int lenTicks;    // its length; the landing sits at base + lenTicks
    int variant;
    std::vector<Hit>* out;
    std::string* report;
};
void buildFill(const FillEnv& e);
void buildBreak(const Settings& s, const Bar& bar, int kit, int base, std::vector<Hit>& hits, std::string& report);
void addLanding(const Settings& s, int kit, int tick, std::vector<Hit>& hits);
// Poly.cpp
void applyPoly(const Settings& s, const Bar& bar, int kit, int nBars, std::vector<Hit>& hits, std::string& report);
bool buildTihai(const FillEnv& e);

}  // namespace drums
