#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include "Drums.h"

// The built-in preview kit: every instrument is a pitched tone with a falling pitch plus filtered noise.
// It is there to hear the rhythm, not to replace a library.
class DrumSynth {
public:
    void prepare(double sampleRate) { sr = sampleRate; for (auto& v : voices) v.on = false; }
    void allOff() { for (auto& v : voices) v.on = false; }

    void trigger(int role, int vel, int pitch) {
        Voice* v = &voices[0];
        for (auto& c : voices) {
            if (!c.on) { v = &c; break; }
            if (c.t > v->t) v = &c;       // steal the oldest
        }
        // a closed or pedalled hi-hat chokes an open one
        if (role == drums::HatClosed || role == drums::HatPedal)
            for (auto& c : voices) if (c.on && c.role == drums::HatOpen) c.noiseDecay = 0.02f;
        *v = Voice();
        v->on = true;
        v->role = role;
        v->amp = 0.9f * std::pow((float)vel / 127.0f, 1.6f);
        v->seed = 0x9E3779B9u * (uint32_t)(role + 1) + (uint32_t)vel;
        using namespace drums;
        auto tone = [&](double f0, double f1, double sweep, float decay, float amt) {
            v->f0 = f0, v->f1 = f1, v->sweep = sweep, v->toneDecay = decay, v->toneAmt = amt;
        };
        auto noise = [&](float decay, float amt, float hp) { v->noiseDecay = decay, v->noiseAmt = amt, v->hp = hp; };
        switch (role) {
            case Kick: tone(150, 48, 0.035, 0.22f, 1.0f); noise(0.01f, 0.25f, 0.2f); break;
            case BassDrum: tone(90, 42, 0.06, 0.7f, 1.0f); noise(0.03f, 0.15f, 0.05f); break;
            case TaikoLow: tone(120, 62, 0.05, 0.5f, 1.0f); noise(0.04f, 0.3f, 0.1f); break;
            case TaikoHigh: tone(220, 130, 0.04, 0.28f, 0.9f); noise(0.03f, 0.3f, 0.2f); break;
            case Snare: tone(230, 180, 0.02, 0.09f, 0.5f); noise(0.16f, 0.8f, 0.5f); break;
            case OrchSnare: tone(210, 170, 0.02, 0.08f, 0.35f); noise(0.22f, 0.9f, 0.45f); break;
            case TenorDrum: tone(170, 120, 0.03, 0.3f, 0.9f); noise(0.05f, 0.2f, 0.2f); break;
            case SideStick: tone(1700, 1700, 1, 0.02f, 0.6f); noise(0.012f, 0.5f, 0.8f); break;
            case Clap: noise(0.14f, 0.9f, 0.6f); v->clap = true; break;
            case HatClosed: case Ticker: noise(role == Ticker ? 0.02f : 0.04f, 0.6f, 0.95f); break;
            case HatPedal: noise(0.03f, 0.4f, 0.95f); break;
            case HatOpen: noise(0.32f, 0.6f, 0.95f); break;
            case Shaker: case Tambourine: noise(0.07f, 0.5f, 0.97f); break;
            case TomHigh: tone(300, 200, 0.05, 0.25f, 1.0f); noise(0.02f, 0.2f, 0.3f); break;
            case TomMid: tone(220, 145, 0.05, 0.3f, 1.0f); noise(0.02f, 0.2f, 0.3f); break;
            case TomFloor: tone(150, 95, 0.06, 0.38f, 1.0f); noise(0.02f, 0.2f, 0.3f); break;
            case PercHigh: tone(520, 430, 0.02, 0.1f, 0.9f); break;
            case PercLow: tone(330, 270, 0.02, 0.14f, 0.9f); break;
            case Ride: tone(5200, 5200, 1, 0.5f, 0.12f); noise(0.5f, 0.3f, 0.97f); v->metal = true; break;
            case RideBell: case Triangle: tone(role == Triangle ? 4200 : 3100, role == Triangle ? 4200 : 3100, 1, 0.8f, 0.4f); v->metal = true; break;
            case Cowbell: case Anvil: tone(role == Anvil ? 2100 : 560, role == Anvil ? 2100 : 560, 1, 0.16f, 0.6f); v->metal = true; break;
            case Crash1: case Crash2: case Piatti: noise(1.1f, 0.6f, 0.9f); break;
            case Splash: noise(0.4f, 0.55f, 0.95f); break;
            case China: noise(0.8f, 0.65f, 0.8f); tone(900, 900, 1, 0.3f, 0.1f); v->metal = true; break;
            case SusCymbal: noise(0.5f, 0.45f, 0.9f); break;
            case TamTam: noise(2.5f, 0.6f, 0.3f); tone(110, 110, 1, 2.0f, 0.3f); break;
            case Timpani: {
                double f = juce::MidiMessage::getMidiNoteInHertz(pitch >= 0 ? pitch : 43);
                tone(f * 1.08, f, 0.03, 0.9f, 1.0f);
                noise(0.02f, 0.15f, 0.1f);
                v->fifth = true;
                break;
            }
            default: noise(0.05f, 0.5f, 0.8f); break;
        }
    }

    void render(juce::AudioBuffer<float>& out, int start, int num) {
        for (auto& v : voices) {
            if (!v.on) continue;
            for (int i = 0; i < num; ++i) {
                const double sec = v.t / sr;
                float s = 0;
                if (v.toneAmt > 0) {
                    double f = v.f1 + (v.f0 - v.f1) * std::exp(-sec / v.sweep);
                    v.phase += f / sr;
                    double x = std::sin(juce::MathConstants<double>::twoPi * v.phase);
                    if (v.metal) x = 0.5 * x + 0.3 * std::sin(juce::MathConstants<double>::twoPi * v.phase * 1.51) +
                                     0.2 * std::sin(juce::MathConstants<double>::twoPi * v.phase * 2.37);
                    if (v.fifth) x = 0.75 * x + 0.25 * std::sin(juce::MathConstants<double>::twoPi * v.phase * 1.5);
                    s += v.toneAmt * (float)(x * std::exp(-sec / v.toneDecay));
                }
                if (v.noiseAmt > 0) {
                    v.seed ^= v.seed << 13, v.seed ^= v.seed >> 17, v.seed ^= v.seed << 5;
                    float n = (float)(v.seed & 0xFFFF) / 32768.0f - 1.0f;
                    v.lp += (1.0f - v.hp) * (n - v.lp);
                    float y = v.hp > 0 ? n - v.lp : n;
                    float env = (float)std::exp(-sec / v.noiseDecay);
                    if (v.clap) env *= (sec < 0.03 ? (float)(0.4 + 0.6 * std::fabs(std::sin(sec * 330.0))) : 1.0f);
                    s += v.noiseAmt * y * env;
                }
                float o = s * v.amp * 0.5f;
                for (int ch = 0; ch < out.getNumChannels(); ++ch) out.addSample(ch, start + i, o);
                v.t += 1.0;
            }
            double life = std::max((double)v.toneDecay * (v.toneAmt > 0 ? 1 : 0), (double)v.noiseDecay * (v.noiseAmt > 0 ? 1 : 0));
            if (v.t / sr > life * 7.0) v.on = false;
        }
    }

private:
    struct Voice {
        bool on = false, metal = false, clap = false, fifth = false;
        int role = 0;
        double t = 0, phase = 0, f0 = 0, f1 = 0, sweep = 1;
        float amp = 0, toneDecay = 0.1f, toneAmt = 0, noiseDecay = 0.1f, noiseAmt = 0, hp = 0, lp = 0;
        uint32_t seed = 1;
    };
    std::array<Voice, 40> voices;
    double sr = 44100.0;
};
