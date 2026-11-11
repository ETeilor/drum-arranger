#include "LiveEditor.h"

namespace {
constexpr int kTop = 56, kSide = 268;
const char* const KEY_NAMES[] = {"Грув", "Вариант", "Филл", "Брейк", "Стоп"};
const int KEY_OFFSETS[] = {0, 1, 2, 4, 5};
juce::String noteName(int note) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(n[note % 12]) + juce::String(note / 12 - 2);   // Logic: middle C (60) is C3
}
}  // namespace

void TakeChip::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hover && enabled ? ui::lift(ui::panelHi, 0.07f) : ui::panelHi);
    g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
    g.setColour(enabled ? ui::accent : ui::textDim);
    g.fillEllipse(r.getX() + 12.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
    g.setColour(enabled ? ui::text : ui::textDim);
    g.setFont(ui::font(12.5f));
    g.drawText(label, getLocalBounds().withTrimmedLeft(28).withTrimmedRight(10), juce::Justification::centredLeft);
}

void TakeChip::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || !enabled || e.getDistanceFromDragStart() < 4 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

DrumLiveEditor::DrumLiveEditor(DrumLiveProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    auto& ps = proc.params;
    titleLbl.setText("Drum Arranger Live", juce::dontSendNotification);
    titleLbl.setFont(ui::font(16.0f, true));
    addAndMakeVisible(titleLbl);

    int lastKit = -1;
    for (int i = 0; i < drums::numStyles(); ++i) {
        if (drums::styleKit(i) != lastKit) { lastKit = drums::styleKit(i); styleBox.addSectionHeading(U(drums::kitLabel(lastKit))); }
        styleBox.addItem(U(drums::styleLabel(i)), i + 1);
    }
    for (int i = 0; i < drums::NUM_MAPS; ++i) mapBox.addItem(U(drums::mapLabel(i)), i + 1);
    keysBox.addItemList({U("от C0"), U("от C1"), U("от C2"), U("от C3"), U("от C4"), U("от C5")}, 1);
    barsBox.addItemList({U("1 такт"), U("2 такта"), U("4 такта"), U("8 тактов")}, 1);
    fillLenBox.addItemList({U("филл: доля"), U("филл: две доли"), U("филл: такт")}, 1);
    for (int i = 0; i < drums::NUM_POLY; ++i) polyBox.addItem(U(drums::polyLabel(i)), i + 1);
    for (auto* c : {&styleBox, &mapBox, &keysBox, &barsBox, &fillLenBox, &polyBox}) addAndMakeVisible(*c);
    styleA = std::make_unique<CA>(ps, "style", styleBox);
    mapA = std::make_unique<CA>(ps, "map", mapBox);
    keysA = std::make_unique<CA>(ps, "keys", keysBox);
    barsA = std::make_unique<CA>(ps, "bars", barsBox);
    fillLenA = std::make_unique<CA>(ps, "filllen", fillLenBox);
    polyA = std::make_unique<CA>(ps, "poly", polyBox);
    keysBox.setTooltip(U("Пять клавиш подряд от выбранной до: грув, вариант, филл, брейк, стоп. Остальные клавиши проходят к инструменту"));

    DrumLook::sectionLabel(styleLbl, U("Стиль"));
    DrumLook::sectionLabel(mapLbl, U("Библиотека"));
    DrumLook::sectionLabel(keysLbl, U("Клавиши запуска"));
    DrumLook::sectionLabel(phraseLbl, U("Длина"));
    DrumLook::sectionLabel(complexityLbl, U("Сложность"));
    DrumLook::sectionLabel(polyLbl, U("Полиритм"));
    for (auto* l : {&styleLbl, &mapLbl, &keysLbl, &phraseLbl, &complexityLbl, &polyLbl}) addAndMakeVisible(*l);
    polyNLbl.setText("N", juce::dontSendNotification);
    polyNLbl.setFont(ui::font(13.0f));
    polyNLbl.setColour(juce::Label::textColourId, ui::textDim);
    polyNLbl.setBorderSize(juce::BorderSize<int>(0));
    polyNLbl.setTooltip(U("Длина цикла или число ударов для полиритма; в режиме «Тала» это гати"));
    addAndMakeVisible(polyNLbl);
    for (auto* s : {&complexity, &polyN}) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour(juce::Slider::textBoxTextColourId, ui::textDim);
        addAndMakeVisible(*s);
    }
    complexityA = std::make_unique<SA>(ps, "complexity", complexity);
    polyNA = std::make_unique<SA>(ps, "polyn", polyN);
    addAndMakeVisible(velBtn);
    velA = std::make_unique<BA>(ps, "velcomplex", velBtn);
    velBtn.setTooltip(U("Тихое нажатие даёт простой рисунок, сильное насыщенный"));

    take.label = U("Сыгранное: пусто");
    take.setTooltip(U("Перетащите на дорожку: всё сыгранное с последней очистки одним MIDI-файлом"));
    take.makeFile = [this] { return proc.writeRecordingFile(); };
    addAndMakeVisible(take);
    clearBtn.getProperties().set("ghost", true);
    clearBtn.onClick = [this] { proc.clearRecording(); };
    addAndMakeVisible(clearBtn);

    setSize(820, 500);
    startTimerHz(30);
}

DrumLiveEditor::~DrumLiveEditor() { setLookAndFeel(nullptr); }

juce::Rectangle<float> DrumLiveEditor::keyBox(int i) const {
    auto area = getLocalBounds().withTrimmedTop(kTop + 1).withTrimmedLeft(kSide + 1).reduced(24).toFloat();
    float w = (area.getWidth() - 4 * 10.0f) / 5.0f;
    return {area.getX() + (float)i * (w + 10.0f), area.getY() + 170.0f, w, 92.0f};
}

void DrumLiveEditor::timerCallback() {
    auto s = proc.snapshot();
    int hits = proc.recordedHits(), key = proc.firstKey();
    if (hits != shownHits) {
        take.enabled = hits > 0;
        take.label = hits > 0 ? U("Сыгранное: нот ") + juce::String(hits) : U("Сыгранное: пусто");
        take.repaint();
    }
    if (s.mode != shown.mode || s.bar != shown.bar || s.complexity != shown.complexity || s.variant != shown.variant ||
        std::abs(s.barProgress - shown.barProgress) > 0.01 || hits != shownHits || key != shownKey) {
        shown = s;
        shownHits = hits;
        shownKey = key;
        repaint();
    }
}

void DrumLiveEditor::mouseUp(const juce::MouseEvent& e) {
    for (int i = 0; i < DrumLiveProcessor::tCount; ++i)
        if (keyBox(i).contains(e.position)) proc.press(i, 100);
}

void DrumLiveEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::line);
    g.fillRect(0, kTop, getWidth(), 1);
    g.fillRect(kSide, kTop, 1, getHeight() - kTop);
    auto area = getLocalBounds().withTrimmedTop(kTop + 1).withTrimmedLeft(kSide + 1).reduced(24).toFloat();

    static const char* modes[] = {"Стоп", "Грув", "Филл", "Брейк"};
    g.setColour(shown.mode == DrumLiveProcessor::mStopped ? ui::textDim : ui::text);
    g.setFont(ui::font(44.0f, true));
    g.drawText(U(modes[shown.mode]), area.withHeight(56.0f), juce::Justification::centredLeft);
    g.setFont(ui::font(13.0f));
    g.setColour(ui::textDim);
    g.drawText(U("сложность ") + juce::String(shown.complexity) + U("  ·  вариант ") + juce::String(shown.variant + 1), area.withY(area.getY() + 62.0f).withHeight(18.0f),
               juce::Justification::centredLeft);
    // the bars of the loop, the current one filling as it plays
    const float bw = 54.0f;
    for (int i = 0; i < shown.bars; ++i) {
        juce::Rectangle<float> r(area.getX() + (float)i * (bw + 6.0f), area.getY() + 100.0f, bw, 8.0f);
        g.setColour(ui::panelHi);
        g.fillRoundedRectangle(r, 4.0f);
        if (shown.mode != DrumLiveProcessor::mStopped && i == shown.bar) {
            g.setColour(ui::accent);
            g.fillRoundedRectangle(r.withWidth(std::max(8.0f, bw * (float)shown.barProgress)), 4.0f);
        }
    }
    // the five keys; they also work as buttons
    const int base = proc.firstKey();
    for (int i = 0; i < DrumLiveProcessor::tCount; ++i) {
        auto r = keyBox(i);
        bool active = (i == DrumLiveProcessor::tGroove && shown.mode == DrumLiveProcessor::mGroove) ||
                      (i == DrumLiveProcessor::tFill && shown.mode == DrumLiveProcessor::mFill) ||
                      (i == DrumLiveProcessor::tBreak && shown.mode == DrumLiveProcessor::mBreak);
        g.setColour(active ? ui::accent : ui::panelHi);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(active ? ui::onAccent : ui::text);
        g.setFont(ui::font(15.0f, true));
        g.drawText(U(KEY_NAMES[i]), r.reduced(12.0f, 14.0f).removeFromTop(20.0f), juce::Justification::centredLeft);
        g.setColour(active ? ui::onAccent : ui::textDim);
        g.setFont(ui::font(12.5f));
        g.drawText(noteName(base + KEY_OFFSETS[i]), r.reduced(12.0f, 14.0f).removeFromBottom(18.0f), juce::Justification::centredLeft);
    }
    g.setColour(ui::textDim);
    g.setFont(ui::font(12.5f));
    g.drawFittedText(U("Играйте клавишами или щёлкайте по плашкам. Филл встаёт в конец текущего такта, брейк занимает следующий такт. "
                       "Когда проект играет, рисунок идёт по его тактам."),
                     area.withY(keyBox(0).getBottom() + 14.0f).withHeight(40.0f).toNearestInt(), juce::Justification::topLeft, 3);
}

void DrumLiveEditor::resized() {
    auto top = getLocalBounds().removeFromTop(kTop).reduced(20, 13);
    titleLbl.setBounds(top.removeFromLeft(260));
    auto side = getLocalBounds().withTrimmedTop(kTop + 1).removeFromLeft(kSide).reduced(20, 16);
    auto block = [&](juce::Label& l, juce::Component& c, int h = 30) {
        l.setBounds(side.removeFromTop(14));
        side.removeFromTop(5);
        c.setBounds(side.removeFromTop(h));
        side.removeFromTop(12);
    };
    block(styleLbl, styleBox);
    block(mapLbl, mapBox);
    block(keysLbl, keysBox);
    phraseLbl.setBounds(side.removeFromTop(14));
    side.removeFromTop(5);
    {
        auto rw = side.removeFromTop(30);
        barsBox.setBounds(rw.removeFromLeft(100));
        rw.removeFromLeft(6);
        fillLenBox.setBounds(rw);
    }
    side.removeFromTop(12);
    block(complexityLbl, complexity, 24);
    side.removeFromTop(-6);
    velBtn.setBounds(side.removeFromTop(22));
    side.removeFromTop(12);
    polyLbl.setBounds(side.removeFromTop(14));
    side.removeFromTop(5);
    polyBox.setBounds(side.removeFromTop(30));
    side.removeFromTop(4);
    {
        auto rw = side.removeFromTop(24);
        polyNLbl.setBounds(rw.removeFromLeft(24));
        polyN.setBounds(rw);
    }

    auto bottom = getLocalBounds().withTrimmedLeft(kSide + 1).reduced(24).removeFromBottom(30);
    clearBtn.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    take.setBounds(bottom.removeFromLeft(240));
}
