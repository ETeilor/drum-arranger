#pragma once
// Drum Arranger engine: grooves, fills and breaks built by rules. Plain C++17, no JUCE.
// Nothing here is random: every choice is a hash of the variant number, so the same settings always
// give the same notes.
#include <cstdint>
#include <string>
#include <vector>

namespace drums {

constexpr int TPB = 480;        // ticks per quarter note
constexpr int STEP = TPB / 4;   // one sixteenth

enum Role : int {
    Kick, Snare, SideStick, Clap, HatClosed, HatPedal, HatOpen, TomHigh, TomMid, TomFloor,
    Ride, RideBell, Crash1, Crash2, Splash, China, Tambourine, Shaker, Cowbell, PercHigh, PercLow,
    // orchestral and cinematic
    Timpani, BassDrum, OrchSnare, TenorDrum, Piatti, SusCymbal, TamTam, Triangle, TaikoHigh, TaikoLow,
    Anvil, Ticker,
    NUM_ROLES
};

// layers that can be locked on their own: the variant of a locked layer stays put
enum Layer : int { L_LOW, L_SNARE, L_CYMBALS, L_TOMS, NUM_LAYERS };

enum Kit : int { K_ROCK, K_POP, K_METAL, K_CINEMATIC, K_ORCHESTRAL, K_ELECTRONIC, NUM_KITS };

enum class Kind : int { Groove, Fill, Break, Phrase };

enum Poly : int {
    P_OFF,
    P_FEET,        // the kick repeats a cycle of N sixteenths under hands that stay in the meter
    P_CROSS,       // N evenly spaced strokes per bar on an extra voice (3 over 4, 5 over 4...)
    P_GROUPS,      // accents every N sixteenths, running over the bar lines
    P_MODULATION,  // second half of the phrase is felt with a pulse of N sixteenths (implied modulation)
    P_TALA,        // fills become a tihai: a phrase three times, landing on the downbeat; N = gati
    P_EUCLID,      // an extra voice plays N strokes spread evenly over the steps of the bar
    NUM_POLY
};

enum Map : int { M_GM, M_DUPLANTIER, M_BBCSO, NUM_MAPS };

struct Meter {
    int num = 4, den = 4;
};

struct Hit {
    int tick = 0;      // from the start of the region
    int role = Kick;
    int vel = 100;
    int len = 60;      // ticks
    int pitch = -1;    // only for pitched roles (timpani)
    int grid = 0;      // the tick before feel and swing moved it (playability is judged on the grid)
};

struct SourceNote {    // rhythm source: a bass line or an ostinato the kick should follow
    int start = 0, end = 0, pitch = 60, vel = 100;
};

struct Settings {
    int style = 0;
    Meter meter;
    double bpm = 120.0;
    int bars = 4;              // length of a groove or a phrase
    int complexity = 5;        // 0..10
    int feel = 3;              // 0..10: how far timing and velocity move off the grid
    int swing = 0;             // 0..100, on top of what the style swings by itself
    int grooveVariant = 0, fillVariant = 0, breakVariant = 0;
    int layerVariant[NUM_LAYERS] = {-1, -1, -1, -1};   // -1: follow grooveVariant
    int fillEvery = 4;         // phrase: a fill closes every N bars, 0 = no fills
    int fillLen = 2;           // 0 one beat, 1 two beats, 2 one bar, 3 two bars
    bool landing = true;       // crash and kick on the downbeat after a fill or a break
    int poly = P_OFF;
    int polyN = 5;
    int tonic = 0;             // pitch class the timpani are tuned to (0 = C)
    std::vector<SourceNote> follow;
};

struct Result {
    std::vector<Hit> hits;     // sorted by tick
    int length = 0;            // ticks; a landing hit sits exactly at length
    Meter meter;
    double bpm = 120.0;
    std::string report;        // what was built and by which rule
    std::string error;
    bool ok() const { return error.empty(); }
};

Result generate(Kind kind, const Settings& s);

// ---- catalogue
int numStyles();
const char* styleId(int style);       // for the command line
const char* styleLabel(int style);    // Russian, for the menu
int styleKit(int style);
const char* kitLabel(int kit);
const char* polyLabel(int poly);
const char* mapLabel(int map);
const char* roleName(int role);       // English: track names
int roleLayer(int role);
int findStyle(const std::string& id); // -1 when unknown

// ---- checks used by the tests and by the report
// hands and feet a drummer has; empty when a kit player can play it (ensemble kits are not checked)
std::vector<std::string> playabilityProblems(const Result& r, int kit);
uint32_t soundHash(const Result& r);  // two results that sound the same hash the same
std::string meterGrouping(const Meter& m);   // "7/8 = 2+2+3"
// a cycle of n sixteenths against the bar: bars until both start together again
int realignBars(int n, const Meter& m);
std::string euclid(int k, int n);     // 'x' and '.', starting on a stroke

// ---- MIDI
int noteFor(int map, int role);       // the key this role sits on in a library
struct MidiOptions {
    int map = M_GM;
    bool perInstrument = false;   // one track per instrument instead of the whole kit on one track
    int only = -1;                // per-instrument: write just this role
};
std::vector<unsigned char> writeMidi(const Result& r, const MidiOptions& o);
// reads any standard MIDI file into notes with ticks scaled to TPB; false when it is not MIDI
bool readMidi(const unsigned char* data, size_t size, std::vector<SourceNote>& notes, Meter& meter, double& bpm);

// ---- live mode: one bar at a time
struct LiveBars {
    std::vector<std::vector<Hit>> groove;   // per bar of the loop, ticks relative to the bar
    std::vector<Hit> fill, brk;             // one bar each; a landing hit has tick == barTicks
    int barTicks = 4 * TPB;
};
LiveBars liveBars(const Settings& s);

}  // namespace drums
