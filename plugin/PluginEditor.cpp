#include "PluginEditor.h"

namespace {
constexpr int kTop = 56, kSide = 268;
}

// ---------------------------------------------------------------- card
juce::Rectangle<int> Card::arrow(int dir) const {
    auto r = getLocalBounds().removeFromTop(34).removeFromRight(56).reduced(4, 6);
    return dir < 0 ? r.removeFromLeft(22) : r.removeFromRight(22);
}

void Card::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.75f);
    g.setColour(hover && !selected ? ui::lift(ui::panelHi, 0.07f) : ui::panelHi);
    g.fillRoundedRectangle(r, 10.0f);
    if (selected) {
        g.setColour(ui::accent);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
    }
    g.setColour(selected ? ui::accent : ui::textDim);
    g.setFont(ui::font(10.5f, true).withExtraKerningFactor(0.09f));
    g.drawText(title.toUpperCase(), 14, 10, 90, 14, juce::Justification::centredLeft);
    g.setColour(ui::text);
    g.setFont(ui::font(12.0f));
    g.drawText(caption, 14, getHeight() - 26, getWidth() - 28, 16, juce::Justification::centredLeft);
    if (hasVariants) {
        DrumLook::drawIcon(g, "prev", arrow(-1).toFloat().withSizeKeepingCentre(12, 12), ui::textDim);
        DrumLook::drawIcon(g, "next", arrow(1).toFloat().withSizeKeepingCentre(12, 12), ui::textDim);
    }
    if (result) GridView::drawHits(g, *result, juce::Rectangle<float>(14.0f, 34.0f, (float)getWidth() - 28.0f, (float)getHeight() - 68.0f), false);
}

void Card::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 5 || !makeFile) return;
    dragging = true;
    auto f = makeFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

void Card::mouseUp(const juce::MouseEvent& e) {
    if (!dragging && e.getDistanceFromDragStart() < 5) {
        if (hasVariants && arrow(-1).contains(e.getPosition())) { if (onStep) onStep(-1); }
        else if (hasVariants && arrow(1).contains(e.getPosition())) { if (onStep) onStep(1); }
        else if (onSelect) onSelect();
    }
    dragging = false;
}

// ---------------------------------------------------------------- editor
DrumEditor::DrumEditor(DrumProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&look);
    auto& ps = proc.params;

    titleLbl.setText("Drum Arranger", juce::dontSendNotification);
    titleLbl.setFont(ui::font(16.0f, true));
    addAndMakeVisible(titleLbl);

    for (auto* b : {&sourceBtn, &clearBtn, &playBtn, &exportBtn}) addAndMakeVisible(*b);
    sourceBtn.getProperties().set("icon", "open");
    sourceBtn.getProperties().set("ghost", true);
    sourceBtn.setTooltip(U("MIDI-файл с басом или остинато: бочка пойдёт за его атаками, литавры возьмут его нижние ноты"));
    sourceBtn.onClick = [this] { openSource(); };
    clearBtn.getProperties().set("icon", "close");
    clearBtn.getProperties().set("ghost", true);
    clearBtn.setTooltip(U("Убрать источник ритма"));
    clearBtn.onClick = [this] { proc.clearSource(); };
    playBtn.getProperties().set("icon", "play");
    playBtn.setTooltip(U("Проиграть выбранную карточку по кругу встроенным набором"));
    playBtn.onClick = [this] { proc.setPreview(!proc.isPreviewing()); };
    exportBtn.getProperties().set("icon", "export");
    exportBtn.setTooltip(U("Сохранить выбранную карточку в MIDI-файл"));
    exportBtn.onClick = [this] { exportSelected(); };

    // style menu, grouped by kit
    int lastKit = -1;
    for (int i = 0; i < drums::numStyles(); ++i) {
        if (drums::styleKit(i) != lastKit) {
            lastKit = drums::styleKit(i);
            styleBox.addSectionHeading(U(drums::kitLabel(lastKit)));
        }
        styleBox.addItem(U(drums::styleLabel(i)), i + 1);
    }
    for (int i = 0; i < drums::NUM_MAPS; ++i) mapBox.addItem(U(drums::mapLabel(i)), i + 1);
    outBox.addItemList({U("Весь набор на одной дорожке"), U("Дорожка на инструмент")}, 1);
    for (int i = 1; i <= 32; ++i) numBox.addItem(juce::String(i), i);
    denBox.addItemList({"2", "4", "8", "16"}, 1);
    barsBox.addItemList({U("1 такт"), U("2 такта"), U("4 такта"), U("8 тактов"), U("16 тактов")}, 1);
    fillEveryBox.addItemList({U("без филлов"), U("филл: 2 т."), U("филл: 4 т."), U("филл: 8 т.")}, 1);
    fillLenBox.addItemList({U("доля"), U("две доли"), U("такт"), U("два такта")}, 1);
    for (int i = 0; i < drums::NUM_POLY; ++i) polyBox.addItem(U(drums::polyLabel(i)), i + 1);
    tonicBox.addItemList({"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}, 1);
    for (auto* c : {&styleBox, &mapBox, &outBox, &numBox, &denBox, &barsBox, &fillEveryBox, &fillLenBox, &polyBox, &tonicBox}) addAndMakeVisible(*c);
    styleA = std::make_unique<CA>(ps, "style", styleBox);
    mapA = std::make_unique<CA>(ps, "map", mapBox);
    outA = std::make_unique<CA>(ps, "outmode", outBox);
    numA = std::make_unique<CA>(ps, "num", numBox);
    denA = std::make_unique<CA>(ps, "den", denBox);
    barsA = std::make_unique<CA>(ps, "bars", barsBox);
    fillEveryA = std::make_unique<CA>(ps, "fillevery", fillEveryBox);
    fillLenA = std::make_unique<CA>(ps, "filllen", fillLenBox);
    polyA = std::make_unique<CA>(ps, "poly", polyBox);
    tonicA = std::make_unique<CA>(ps, "tonic", tonicBox);
    mapBox.setTooltip(U("На каких клавишах лежат инструменты в вашей библиотеке"));
    outBox.setTooltip(U("Одна дорожка для набора целиком или отдельная дорожка на каждый инструмент"));
    barsBox.setTooltip(U("Длина грува и фразы"));
    fillEveryBox.setTooltip(U("Во фразе филл закрывает каждые столько тактов"));
    fillLenBox.setTooltip(U("Длина филла"));
    polyBox.setTooltip(U("Слой полиритма поверх грува; в режиме «Тала» филл становится тихаи"));
    tonicBox.setTooltip(U("Тоника, на которую настроены литавры"));

    DrumLook::sectionLabel(styleLbl, U("Стиль"));
    DrumLook::sectionLabel(mapLbl, U("Библиотека и выход"));
    DrumLook::sectionLabel(meterLbl, U("Размер и темп"));
    DrumLook::sectionLabel(phraseLbl, U("Фраза"));
    DrumLook::sectionLabel(feelLbl, U("Характер"));
    DrumLook::sectionLabel(polyLbl, U("Полиритм"));
    DrumLook::sectionLabel(lockLbl, U("Закрепить слои"));
    DrumLook::sectionLabel(tonicLbl, U("Строй литавр"));
    for (auto* l : {&styleLbl, &mapLbl, &meterLbl, &phraseLbl, &feelLbl, &polyLbl, &lockLbl, &tonicLbl}) addAndMakeVisible(*l);
    slashLbl.setText("/", juce::dontSendNotification);
    slashLbl.setJustificationType(juce::Justification::centred);
    slashLbl.setColour(juce::Label::textColourId, ui::textDim);
    addAndMakeVisible(slashLbl);

    auto row = [&](Row& r, const char* id, const char* name, const char* tip) {
        r.name.setText(U(name), juce::dontSendNotification);
        r.name.setFont(ui::font(13.0f));
        r.value.setFont(ui::font(13.0f));
        r.value.setJustificationType(juce::Justification::centredRight);
        r.value.setColour(juce::Label::textColourId, ui::textDim);
        r.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        r.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        r.slider.setTooltip(U(tip));
        addAndMakeVisible(r.name);
        addAndMakeVisible(r.value);
        addAndMakeVisible(r.slider);
        r.att = std::make_unique<SA>(ps, id, r.slider);
    };
    row(tempo, "tempo", "Темп", "Темп для прослушивания и для MIDI-файла");
    row(complexity, "complexity", "Сложность", "0: по учебнику, 10: насыщенная партия. От сложности зависят и рудименты в филлах");
    row(feel, "feel", "Живость", "Насколько удары отходят от сетки по времени и силе. Записывается в MIDI");
    row(swing, "swing", "Свинг", "Сдвиг слабых восьмых и шестнадцатых. Записывается в MIDI");
    row(polyN, "polyn", "N", "Длина цикла или число ударов для полиритма; в режиме «Тала» это гати");

    addAndMakeVisible(hostBtn);
    hostA = std::make_unique<BA>(ps, "hostsync", hostBtn);
    hostBtn.setTooltip(U("Брать темп и размер из проекта, когда плагин стоит в DAW"));
    addAndMakeVisible(landingBtn);
    landingA = std::make_unique<BA>(ps, "landing", landingBtn);
    landingBtn.setTooltip(U("Тарелка с бочкой на первую долю после филла и брейка"));
    static const char* lockNames[] = {"Низ", "Малый", "Тарелки", "Томы и перкуссия"};
    static const char* lockIds[] = {"lock0", "lock1", "lock2", "lock3"};
    for (int i = 0; i < drums::NUM_LAYERS; ++i) {
        lockBtn[i].setButtonText(U(lockNames[i]));
        lockBtn[i].setClickingTogglesState(true);
        lockBtn[i].setTooltip(U("Закреплённый слой не меняется, когда вы листаете варианты грува"));
        addAndMakeVisible(lockBtn[i]);
        lockA[i] = std::make_unique<BA>(ps, lockIds[i], lockBtn[i]);
    }

    static const char* titles[] = {"Грув", "Филл", "Брейк", "Фраза"};
    static const char* tipsText[] = {"Грув: щёлкните, чтобы выбрать, тяните на дорожку", "Филл: такт с переходом в конце, тяните на дорожку",
                                     "Брейк: такт остановки или сбивки, тяните на дорожку", "Фраза: грув с филлами, тяните на дорожку"};
    for (int k = 0; k < DrumProcessor::kNumKinds; ++k) {
        auto& c = cards[k];
        c.title = U(titles[k]);
        c.hasVariants = k != DrumProcessor::kPhrase;
        c.setTooltip(U(tipsText[k]));
        c.onSelect = [this, k] { proc.select(k); };
        c.onStep = [this, k](int dir) { proc.select(k); proc.stepVariant(k, dir); };
        c.makeFile = [this, k] { return proc.writeFile(k); };
        addAndMakeVisible(c);
    }
    addAndMakeVisible(grid);
    grid.makeDragFile = [this] { return proc.writeFile(proc.selected()); };

    report.setFont(ui::font(12.5f));
    report.setColour(juce::Label::textColourId, ui::textDim);
    report.setJustificationType(juce::Justification::topLeft);
    report.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(report);

    proc.onResultChanged = [this] { refresh(); };
    setResizable(true, true);
    setResizeLimits(980, 720, 1800, 1200);
    setSize(1120, 760);
    refresh();
    startTimerHz(30);
}

DrumEditor::~DrumEditor() {
    proc.onResultChanged = nullptr;
    setLookAndFeel(nullptr);
}

void DrumEditor::refresh() {
    auto s = proc.settings();
    const int sel = proc.selected();
    for (int k = 0; k < DrumProcessor::kNumKinds; ++k) {
        auto& c = cards[k];
        c.result = &proc.result(k);
        c.selected = k == sel;
        c.caption = k == DrumProcessor::kPhrase ? U("тактов: ") + juce::String(s.bars) : U("вариант ") + juce::String(proc.variant(k) + 1);
        c.repaint();
    }
    grid.setResult(&proc.result(sel));
    auto text = U(proc.result(sel).ok() ? proc.result(sel).report : proc.result(sel).error);
    if (proc.sourceNotes() > 0) text << U("Источник ритма: ") << proc.sourceName() << U(", нот: ") << proc.sourceNotes() << "\n";
    report.setText(text, juce::dontSendNotification);
    tempo.value.setText(juce::String((int)std::lround(s.bpm)), juce::dontSendNotification);
    complexity.value.setText(juce::String(s.complexity), juce::dontSendNotification);
    feel.value.setText(juce::String(s.feel), juce::dontSendNotification);
    swing.value.setText(juce::String(s.swing), juce::dontSendNotification);
    polyN.value.setText(juce::String(s.polyN), juce::dontSendNotification);
    const bool poly = s.poly != drums::P_OFF;
    polyN.slider.setEnabled(poly);
    polyN.name.setColour(juce::Label::textColourId, poly ? ui::text : ui::textDim);
    // the timpani tuning only matters where there are timpani
    const bool timp = drums::styleKit(s.style) == drums::K_ORCHESTRAL;
    tonicLbl.setVisible(timp);
    tonicBox.setVisible(timp);
    clearBtn.setVisible(proc.sourceNotes() > 0);
    sourceBtn.setButtonText(proc.sourceNotes() > 0 ? proc.sourceName().upToLastOccurrenceOf(".", false, false).substring(0, 18) : U("Ритм из MIDI"));
    resized();
}

void DrumEditor::timerCallback() {
    const bool on = proc.isPreviewing();
    if (playBtn.getToggleState() != on) {
        playBtn.setToggleState(on, juce::dontSendNotification);
        playBtn.getProperties().set("icon", on ? "stop" : "play");
        playBtn.setButtonText(on ? U("Стоп") : U("Слушать"));
        playBtn.repaint();
    }
    grid.setPlayhead(on ? proc.previewPositionTicks() : -1.0);
}

void DrumEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::line);
    g.fillRect(0, kTop, getWidth(), 1);
    g.fillRect(kSide, kTop, 1, getHeight() - kTop);
    if (dropHover) {
        g.setColour(ui::accent.withAlpha(0.8f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(6.0f), 10.0f, 1.5f);
    }
}

void DrumEditor::resized() {
    auto top = getLocalBounds().removeFromTop(kTop).reduced(20, 13);
    titleLbl.setBounds(top.removeFromLeft(200));
    exportBtn.setBounds(top.removeFromRight(86));
    top.removeFromRight(8);
    playBtn.setBounds(top.removeFromRight(112));
    top.removeFromRight(16);
    if (clearBtn.isVisible()) clearBtn.setBounds(top.removeFromRight(30));
    sourceBtn.setBounds(top.removeFromRight(168));

    auto side = getLocalBounds().withTrimmedTop(kTop + 1).removeFromLeft(kSide).reduced(20, 16);
    auto label = [&](juce::Label& l) { l.setBounds(side.removeFromTop(14)); side.removeFromTop(5); };
    auto sliderRow = [&](Row& r) {
        auto rw = side.removeFromTop(24);
        r.name.setBounds(rw.removeFromLeft(84));
        r.value.setBounds(rw.removeFromRight(34));
        r.slider.setBounds(rw);
    };
    label(styleLbl);
    styleBox.setBounds(side.removeFromTop(30));
    side.removeFromTop(12);
    label(mapLbl);
    mapBox.setBounds(side.removeFromTop(30));
    side.removeFromTop(6);
    outBox.setBounds(side.removeFromTop(30));
    side.removeFromTop(12);
    label(meterLbl);
    {
        auto rw = side.removeFromTop(30);
        numBox.setBounds(rw.removeFromLeft(58));
        slashLbl.setBounds(rw.removeFromLeft(14));
        denBox.setBounds(rw.removeFromLeft(58));
        rw.removeFromLeft(10);
        hostBtn.setBounds(rw);
    }
    side.removeFromTop(4);
    sliderRow(tempo);
    side.removeFromTop(10);
    label(phraseLbl);
    {
        auto rw = side.removeFromTop(30);
        barsBox.setBounds(rw.removeFromLeft(108));
        rw.removeFromLeft(6);
        fillEveryBox.setBounds(rw);
    }
    side.removeFromTop(6);
    {
        auto rw = side.removeFromTop(30);
        fillLenBox.setBounds(rw.removeFromLeft(108));
        rw.removeFromLeft(10);
        landingBtn.setBounds(rw);
    }
    side.removeFromTop(12);
    label(feelLbl);
    sliderRow(complexity);
    sliderRow(feel);
    sliderRow(swing);
    side.removeFromTop(10);
    label(polyLbl);
    polyBox.setBounds(side.removeFromTop(30));
    side.removeFromTop(4);
    sliderRow(polyN);
    side.removeFromTop(10);
    label(lockLbl);
    {
        // two rows: the low and the snare layer, the cymbals and the toms
        for (int rowI = 0; rowI < 2; ++rowI) {
            auto rw = side.removeFromTop(26);
            side.removeFromTop(6);
            int w = rowI == 0 ? (rw.getWidth() - 6) / 2 : 84;
            lockBtn[rowI * 2].setBounds(rw.removeFromLeft(w));
            rw.removeFromLeft(6);
            lockBtn[rowI * 2 + 1].setBounds(rw);
        }
    }
    side.removeFromTop(6);
    label(tonicLbl);
    tonicBox.setBounds(side.removeFromTop(30).removeFromLeft(108));

    auto main = getLocalBounds().withTrimmedTop(kTop + 1).withTrimmedLeft(kSide + 1).reduced(18);
    auto cardRow = main.removeFromTop(132);
    int cw = (cardRow.getWidth() - 36) / 4;
    for (int k = 0; k < DrumProcessor::kNumKinds; ++k) {
        cards[k].setBounds(cardRow.removeFromLeft(cw));
        cardRow.removeFromLeft(12);
    }
    main.removeFromTop(14);
    report.setBounds(main.removeFromBottom(86));
    main.removeFromBottom(10);
    grid.setBounds(main);
}

bool DrumEditor::isInterestedInFileDrag(const juce::StringArray& files) { return files.size() == 1; }

void DrumEditor::filesDropped(const juce::StringArray& files, int, int) {
    dropHover = false;
    repaint();
    juce::String err;
    if (files.size() == 1 && !proc.loadSource(juce::File(files[0]), err)) showError(err);
}

void DrumEditor::openSource() {
    chooser = std::make_unique<juce::FileChooser>(U("MIDI-файл с ритмом"), juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        juce::String err;
        if (f.existsAsFile() && !proc.loadSource(f, err)) showError(err);
    });
}

void DrumEditor::exportSelected() {
    const int kind = proc.selected();
    auto tmp = proc.writeFile(kind);
    chooser = std::make_unique<juce::FileChooser>(U("Сохранить MIDI"), juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(tmp.getFileName()), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, tmp](const juce::FileChooser& fc) {
                             auto f = fc.getResult();
                             if (f != juce::File() && !tmp.copyFileTo(f.withFileExtension("mid"))) showError(U("Не удалось сохранить файл: ") + f.getFullPathName());
                         });
}

void DrumEditor::showError(const juce::String& err) {
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Drum Arranger", err);
}
