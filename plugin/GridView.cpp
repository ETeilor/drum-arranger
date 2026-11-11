#include "GridView.h"
#include <set>

namespace {
// top to bottom: cymbals, snares, toms and percussion, low drums
int laneOrder(int role) {
    using namespace drums;
    static const int order[] = {Crash1, Crash2, China, Splash, Piatti, SusCymbal, TamTam, Ride, RideBell, HatOpen, HatClosed,
        HatPedal, Ticker, Triangle, Tambourine, Shaker, Cowbell, Anvil, Clap, SideStick, Snare, OrchSnare, TenorDrum, TaikoHigh,
        PercHigh, PercLow, TomHigh, TomMid, TomFloor, Timpani, TaikoLow, BassDrum, Kick};
    for (int i = 0; i < (int)(sizeof(order) / sizeof(order[0])); ++i) if (order[i] == role) return i;
    return 99;
}
// the bar plus one sixteenth at the end, where a landing sits
double spanTicks(const drums::Result& r) { return r.length + drums::STEP * 2.0; }
}  // namespace

std::vector<int> GridView::lanes(const drums::Result& r) {
    std::set<int> roles;
    for (auto& h : r.hits) roles.insert(h.role);
    std::vector<int> v(roles.begin(), roles.end());
    std::sort(v.begin(), v.end(), [](int a, int b) { return laneOrder(a) < laneOrder(b); });
    return v;
}

void GridView::drawHits(juce::Graphics& g, const drums::Result& r, juce::Rectangle<float> a, bool detailed) {
    auto ls = lanes(r);
    if (ls.empty() || r.length <= 0) return;
    const float laneH = a.getHeight() / (float)ls.size();
    const double span = spanTicks(r);
    auto xOf = [&](double tick) { return a.getX() + (float)(tick / span) * a.getWidth(); };
    const int barTicks = r.meter.num * 4 * drums::TPB / r.meter.den;
    if (detailed) {
        for (size_t i = 0; i < ls.size(); ++i)
            if (i % 2) { g.setColour(ui::bg.withAlpha(0.35f)); g.fillRect(a.getX(), a.getY() + laneH * (float)i, a.getWidth(), laneH); }
        // beats, then bar lines on top
        g.setColour(ui::line.withAlpha(0.5f));
        for (int t = 0; t < r.length; t += drums::TPB) g.fillRect(xOf(t), a.getY(), 1.0f, a.getHeight());
    }
    g.setColour(detailed ? ui::line.brighter(0.25f) : ui::line);
    for (int t = barTicks; t <= r.length; t += barTicks) g.fillRect(xOf(t), a.getY(), 1.0f, a.getHeight());
    const float w = std::max(detailed ? 4.0f : 2.0f, (float)(drums::STEP * 0.62 / span) * a.getWidth());
    for (auto& h : r.hits) {
        int lane = (int)(std::find(ls.begin(), ls.end(), h.role) - ls.begin());
        float hh = std::max(2.0f, (laneH - (detailed ? 6.0f : 2.0f)) * (0.3f + 0.7f * (float)h.vel / 127.0f));
        float y = a.getY() + laneH * (float)lane + (laneH - hh) / 2.0f;
        g.setColour(ui::roleColour(h.role).withAlpha(0.45f + 0.55f * (float)h.vel / 127.0f));
        g.fillRoundedRectangle(xOf(h.tick), y, w, hh, detailed ? 2.0f : 1.0f);
    }
}

void GridView::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    g.setColour(ui::panel);
    g.fillRoundedRectangle(b, 10.0f);
    if (!result || result->hits.empty()) return;
    auto ls = lanes(*result);
    auto area = b.reduced(10.0f, 22.0f).withTrimmedLeft(labelWidth);
    area.setTop(b.getY() + 26.0f);
    const float laneH = std::min(46.0f, area.getHeight() / (float)ls.size());
    area.setHeight(laneH * (float)ls.size());
    // bar numbers
    const int barTicks = result->meter.num * 4 * drums::TPB / result->meter.den;
    g.setFont(ui::font(11.0f));
    g.setColour(ui::textDim);
    for (int bar = 0; bar * barTicks < result->length; ++bar)
        g.drawText(juce::String(bar + 1), juce::Rectangle<float>(area.getX() + (float)(bar * barTicks / spanTicks(*result)) * area.getWidth() + 4.0f, b.getY() + 8.0f, 40.0f, 14.0f),
                   juce::Justification::centredLeft);
    drawHits(g, *result, area, true);
    g.setFont(ui::font(12.0f));
    for (size_t i = 0; i < ls.size(); ++i) {
        auto row = juce::Rectangle<float>(b.getX() + 12.0f, area.getY() + laneH * (float)i, labelWidth - 8.0f, laneH);
        g.setColour(ui::roleColour(ls[i]));
        g.fillEllipse(row.getX(), row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(ui::textDim);
        g.drawText(drums::roleName(ls[i]), row.withTrimmedLeft(14.0f), juce::Justification::centredLeft);
    }
    if (playhead >= 0) {
        g.setColour(ui::accent);
        g.fillRect(area.getX() + (float)(playhead / spanTicks(*result)) * area.getWidth(), area.getY() - 4.0f, 1.5f, area.getHeight() + 8.0f);
    }
}

void GridView::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 6 || !makeDragFile || !result || result->hits.empty()) return;
    dragging = true;
    auto f = makeDragFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}
