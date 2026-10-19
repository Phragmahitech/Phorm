#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    void drawLabel (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour,
                    juce::Rectangle<float> area, juce::Justification just)
    {
        g.setColour (colour);
        g.setFont (font);
        g.drawText (text, area, just, true);
    }

    float textWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    // Disposition de la façade (pixels de la maquette 1000 x 720)
    constexpr int headerHeight = 64;
    constexpr int footerTop = 634;
    constexpr int leftWidth = 520;
    constexpr int rightX = 552, rightWidth = 416;
    constexpr int knobRowY = 428, knobSize = 44, knobColumn = 66, knobGap = 4;
    constexpr int displayY = 126, displayHeight = 400;
}

//==============================================================================
PhormLookAndFeel::PhormLookAndFeel()
{
    michroma    = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
    plexRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    plexMedium  = juce::Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);

    setColour (juce::ResizableWindow::backgroundColourId, ground());
    setColour (juce::TextButton::buttonColourId, ground());
    setColour (juce::TextButton::textColourOffId, ink());
}

juce::Font PhormLookAndFeel::title (float height, float tracking) const
{
    return juce::Font (juce::FontOptions (michroma).withPointHeight (height)).withExtraKerningFactor (tracking);
}

juce::Font PhormLookAndFeel::mono (float height, float tracking, bool medium) const
{
    return juce::Font (juce::FontOptions (medium ? plexMedium : plexRegular).withPointHeight (height)).withExtraKerningFactor (tracking);
}

void PhormLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto box = juce::Rectangle<float> (size, size).withCentre (centre);
    const bool active = slider.isMouseOverOrDragging();

    // Couronne pointillée
    juce::Path ring;
    ring.addEllipse (box.reduced (0.5f));
    juce::Path dashed;
    const float dashes[] = { 2.5f, 2.5f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, dashes, 2);
    g.setColour (active ? juce::Colour (0xff6a6a6a) : juce::Colour (0xff3d3d3d));
    g.fillPath (dashed);

    // Disque
    const auto disc = box.reduced (size * 6.0f / 44.0f);
    g.setColour (juce::Colour (0xff121212));
    g.fillEllipse (disc);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    // Aiguille
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float rOuter = size * 0.5f - size * 3.0f / 44.0f;
    const float rInner = rOuter - size * 14.0f / 44.0f;
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    g.setColour (white());
    g.drawLine ({ centre + dir * rInner, centre + dir * rOuter }, 1.2f);
}

void PhormLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (outline());
    g.fillRect ((float) x, cy - 0.5f, (float) w, 1.0f);
    g.setColour (juce::Colour (0xff9a9a9a));
    g.fillRect ((float) x, cy - 0.5f, sliderPos - (float) x, 1.0f);

    const bool active = slider.isMouseOverOrDragging();
    g.setColour (active ? juce::Colours::white : white());
    g.fillRect (juce::Rectangle<float> (11.0f, 21.0f).withCentre ({ sliderPos, cy }));
}

void PhormLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool highlighted, bool down)
{
    const auto area = button.getLocalBounds().toFloat();
    const bool primary = button.getComponentID() == "primary";
    const bool on = button.getToggleState();

    if (primary)
    {
        g.setColour (down ? ground() : (highlighted ? juce::Colours::white : white()));
        g.fillRect (area);
        g.setColour (white());
        g.drawRect (area, 1.0f);
        return;
    }

    if (on)
    {
        g.setColour (white());
        g.fillRect (area);
    }

    const bool enabled = button.isEnabled();
    g.setColour (on || (highlighted && enabled) ? white() : (enabled ? outline() : juce::Colour (0xff262626)));
    g.drawRect (area, 1.0f);
}

void PhormLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    const auto id = button.getComponentID();
    const auto area = button.getLocalBounds().toFloat();

    if (id == "previous" || id == "next")
    {
        const float dir = id == "previous" ? -1.0f : 1.0f;
        const auto c = area.getCentre();
        juce::Path chevron;
        chevron.startNewSubPath (c.x - dir * 4.0f, c.y - 8.0f);
        chevron.lineTo (c.x + dir * 4.0f, c.y);
        chevron.lineTo (c.x - dir * 4.0f, c.y + 8.0f);
        g.setColour (! button.isEnabled() ? juce::Colour (0xff333333) : (highlighted || down ? juce::Colours::white : ink()));
        g.strokePath (chevron, juce::PathStrokeType (1.4f));
        return;
    }

    const bool primary = id == "primary";
    const bool dark = primary ? ! down : button.getToggleState();

    g.setColour (! button.isEnabled() ? juce::Colour (0xff3a3a3a) : (dark ? ground() : ink()));
    g.setFont (primary ? title (12.0f, 0.34f) : mono (10.0f, 0.2f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
void TableDisplay::paint (juce::Graphics& g)
{
    using LF = PhormLookAndFeel;
    const auto t = table;

    if (t == nullptr || t->numFrames < 1)
        return;

    const int F = t->numFrames;
    const int traces = juce::jmin (F, 48);
    constexpr int points = 180;

    // Géométrie : la frame 1 devant (en bas à gauche), la dernière au fond (en haut à droite)
    auto lerp = [] (float a, float b, float d) { return a + (b - a) * d; };
    struct Geometry { float baseY, x0, w, amp; };
    auto geometry = [&] (float d) { return Geometry { lerp (296.0f, 112.0f, d), lerp (44.0f, 150.0f, d), lerp (372.0f, 326.0f, d), lerp (64.0f, 46.0f, d) }; };

    // Arêtes de la « boîte »
    {
        const auto front = geometry (0.0f), back = geometry (1.0f);
        g.setColour (juce::Colour (0xff1c1c1c));
        g.drawLine (front.x0, front.baseY, back.x0, back.baseY, 1.0f);
        g.drawLine (front.x0 + front.w, front.baseY, back.x0 + back.w, back.baseY, 1.0f);
    }

    struct Trace { float depth; bool current; };
    std::vector<Trace> list;
    list.reserve ((size_t) traces + 1);
    for (int i = 0; i < traces; ++i)
        list.push_back ({ traces == 1 ? 0.0f : (float) i / (float) (traces - 1), false });
    list.push_back ({ juce::jlimit (0.0f, 1.0f, position), true });

    std::stable_sort (list.begin(), list.end(), [] (const Trace& a, const Trace& b)
    {
        if (std::abs (a.depth - b.depth) > 1.0e-6f) return a.depth > b.depth;
        return ! a.current && b.current;    // à profondeur égale, la frame jouée passe devant
    });

    for (const auto& trace : list)
    {
        const auto geo = geometry (trace.depth);
        const float fp = trace.depth * (float) (F - 1);
        const int f0 = juce::jmin (F - 1, (int) fp);
        const int f1 = juce::jmin (F - 1, f0 + 1);
        const float frac = trace.current ? fp - (float) f0 : 0.0f;
        const float* a = t->frame (f0);
        const float* b = t->frame (f1);

        juce::Path wave;
        for (int k = 0; k <= points; ++k)
        {
            const int idx = juce::jmin (phorm::frameSize - 1, k * phorm::frameSize / points);
            const float v = a[idx] + (b[idx] - a[idx]) * frac;
            const float x = geo.x0 + geo.w * (float) k / (float) points;
            const float y = geo.baseY - v * geo.amp;
            if (k == 0) wave.startNewSubPath (x, y);
            else        wave.lineTo (x, y);
        }

        // Cache ce qui est derrière (lignes cachées)
        juce::Path mask (wave);
        mask.lineTo (geo.x0 + geo.w, geo.baseY + geo.amp + 6.0f);
        mask.lineTo (geo.x0, geo.baseY + geo.amp + 6.0f);
        mask.closeSubPath();
        g.setColour (LF::ground());
        g.fillPath (mask);

        if (trace.current)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (wave, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
            g.setColour (juce::Colours::white);
            g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
        }
        else
        {
            g.setColour (LF::white().withAlpha (0.16f + 0.34f * (1.0f - trace.depth)));
            g.strokePath (wave, juce::PathStrokeType (1.0f));
        }
    }
}

//==============================================================================
void DragTile::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const bool over = isMouseOverOrDragging();

    g.setColour (over ? PhormLookAndFeel::white() : PhormLookAndFeel::outline());
    const float dashes[] = { 3.0f, 3.0f };
    juce::Path border, dashed;
    border.addRectangle (area.reduced (0.5f));
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, border, dashes, 2);
    g.fillPath (dashed);

    // Petite forme d'onde
    juce::Path icon;
    const float cx = area.getX() + 26.0f, cy = area.getCentreY();
    for (int k = 0; k <= 24; ++k)
    {
        const float x = cx - 12.0f + (float) k;
        const float y = cy - 7.0f * std::sin ((float) k / 24.0f * juce::MathConstants<float>::twoPi) * (k < 12 ? 1.0f : 0.55f);
        if (k == 0) icon.startNewSubPath (x, y);
        else        icon.lineTo (x, y);
    }
    g.setColour (over ? juce::Colours::white : PhormLookAndFeel::ink());
    g.strokePath (icon, juce::PathStrokeType (1.2f));

    g.setFont (lf.mono (10.0f, 0.2f));
    g.drawText ("DRAG", area.withTrimmedLeft (48.0f).withTrimmedBottom (area.getHeight() * 0.5f - 2.0f), juce::Justification::bottomLeft, false);
    g.setColour (PhormLookAndFeel::label());
    g.setFont (lf.mono (9.0f, 0.14f));
    g.drawText ("TO SERUM", area.withTrimmedLeft (48.0f).withTrimmedTop (area.getHeight() * 0.5f + 2.0f), juce::Justification::topLeft, false);
}

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6 || onDragStart == nullptr)
        return;

    dragging = true;
    const auto file = onDragStart();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
PhormFacade::PhormFacade (PhormAudioProcessor& p)
    : audioProcessor (p), dragTile (lookAndFeel)
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);
    setOpaque (true);

    display.setInterceptsMouseClicks (false, false);
    display.setBounds (0, displayY, leftWidth, displayHeight);
    addAndMakeVisible (display);

    // STYLE (pour la prochaine table)
    const float segWidth = (rightWidth - 18) / 4.0f;
    for (int i = 0; i <= phorm::numStyles; ++i)
    {
        styleButtons[i].setButtonText (i == 0 ? juce::String ("RANDOM") : juce::String (phorm::styleName (i - 1)));
        const int row = i / 4, col = i % 4;
        setupSegment (styleButtons[i], juce::Rectangle<float> (rightX + col * (segWidth + 6.0f), 98.0f + row * 42.0f, segWidth, 36.0f).toNearestInt());
        styleButtons[i].onClick = [this, i]
        {
            audioProcessor.setStyleChoice (i == 0 ? PhormAudioProcessor::styleRandom : i - 1);
            refreshControls();
        };
    }

    // BRIGHT (table en cours)
    brightSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    brightSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    brightSlider.setRange (0.0, 100.0, 1.0);
    brightSlider.setDoubleClickReturnValue (true, 75.0);
    brightSlider.setTitle ("Bright");
    brightSlider.setBounds (rightX - 6, 208, rightWidth + 12, 28);
    brightSlider.onValueChange = [this] { audioProcessor.setBright ((float) brightSlider.getValue() / 100.0f); };
    addAndMakeVisible (brightSlider);

    // MORPH, FRAMES (table en cours)
    const float morphWidth = (rightWidth - 12) / 3.0f;
    for (int i = 0; i < phorm::numMorphs; ++i)
    {
        morphButtons[i].setButtonText (phorm::morphName (i));
        setupSegment (morphButtons[i], juce::Rectangle<float> (rightX + i * (morphWidth + 6.0f), 280.0f, morphWidth, 34.0f).toNearestInt());
        morphButtons[i].onClick = [this, i] { audioProcessor.setMorph (i); refreshControls(); };
    }

    const float framesWidth = (rightWidth - 24) / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        framesButtons[i].setButtonText (juce::String (PhormAudioProcessor::frameChoices[i]));
        setupSegment (framesButtons[i], juce::Rectangle<float> (rightX + i * (framesWidth + 6.0f), 346.0f, framesWidth, 34.0f).toNearestInt());
        framesButtons[i].onClick = [this, i] { audioProcessor.setFrames (PhormAudioProcessor::frameChoices[i]); refreshControls(); };
    }

    // PLAY
    Knob* row[6] = { &position, &scan, &rate, &attack, &release, &level };
    const char* ids[6] = { PhormAudioProcessor::positionId, PhormAudioProcessor::scanId, PhormAudioProcessor::rateId,
                           PhormAudioProcessor::attackId, PhormAudioProcessor::releaseId, PhormAudioProcessor::levelId };
    const char* names[6] = { "POSITION", "SCAN", "RATE", "ATTACK", "RELEASE", "LEVEL" };
    for (int i = 0; i < 6; ++i)
    {
        const int x = rightX + i * (knobColumn + knobGap) + (knobColumn - knobSize) / 2;
        setupKnob (*row[i], ids[i], names[i], { x, knobRowY, knobSize, knobSize });
    }

    // ‹ GENERATE ›
    previousButton.setComponentID ("previous");
    nextButton.setComponentID ("next");
    generateButton.setComponentID ("primary");
    previousButton.setTitle ("Previous table");
    nextButton.setTitle ("Next table");
    setupSegment (previousButton, { 64, 548, 52, 56 });
    setupSegment (generateButton, { 124, 548, 272, 56 });
    setupSegment (nextButton, { 404, 548, 52, 56 });

    generateButton.onClick = [this] { audioProcessor.generate(); status = {}; refreshControls(); };
    previousButton.onClick = [this] { audioProcessor.stepHistory (-1); refreshControls(); };
    nextButton.onClick = [this] { audioProcessor.stepHistory (+1); refreshControls(); };

    // EXPORT, DRAG, FOLDER
    exportButton.setComponentID ("primary");
    setupSegment (exportButton, { rightX, 552, 190, 52 });
    exportButton.onClick = [this] { doExport(); };

    dragTile.setBounds (rightX + 198, 552, 150, 52);
    dragTile.onDragStart = [this]
    {
        const auto result = audioProcessor.exportCurrent();
        refreshControls();
        repaint();
        return result.files.isEmpty() ? juce::File() : result.files.getFirst();
    };
    addAndMakeVisible (dragTile);

    setupSegment (folderButton, { rightX + 356, 552, 60, 52 });
    folderButton.onClick = [this]
    {
        bool serum = false;
        const auto folders = PhormAudioProcessor::exportFolders (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), serum);
        if (! folders.isEmpty())
        {
            folders.getFirst().createDirectory();
            folders.getFirst().startAsProcess();
        }
    };

    // Destination de l'export (affichée sous le titre de la section)
    {
        bool serum = false;
        const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const auto folders = PhormAudioProcessor::exportFolders (documents, serum);
        if (serum)
        {
            // « Serum Presets » -> SERUM, « Serum 2 Presets » -> SERUM 2
            juce::StringArray serumNames;
            for (const auto& f : folders)
                serumNames.add (f.getParentDirectory().getParentDirectory().getFileName().toUpperCase().replace (" PRESETS", "").trim());
            exportTarget = serumNames.joinIntoString (" + ") + dot + "TABLES / PHORM";
        }
        else
        {
            exportTarget = "SERUM NOT FOUND" + dot + "DOCUMENTS / PHORM";
        }
    }

    refreshControls();
    timerCallback();
    startTimerHz (30);
}

PhormFacade::~PhormFacade()
{
    stopTimer();
    attachments.clear();
    setLookAndFeel (nullptr);
}

void PhormFacade::setupKnob (Knob& knob, const char* id, const char* name, juce::Rectangle<int> bounds)
{
    knob.id = id;
    knob.name = name;

    auto& s = knob.slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (180);
    s.setTitle (name);
    s.setBounds (bounds);
    addAndMakeVisible (s);
    attachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));

    if (auto* param = audioProcessor.apvts.getParameter (id))
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void PhormFacade::setupSegment (juce::TextButton& b, juce::Rectangle<int> bounds)
{
    b.setClickingTogglesState (false);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    b.setBounds (bounds);
    addAndMakeVisible (b);
}

juce::String PhormFacade::valueText (const char* id) const
{
    if (auto* param = audioProcessor.apvts.getParameter (id))
        return param->getCurrentValueAsText();

    return {};
}

void PhormFacade::doExport()
{
    const auto result = audioProcessor.exportCurrent();
    juce::ignoreUnused (result);
    refreshControls();
    repaint();
}

void PhormFacade::refreshControls()
{
    const auto settings = audioProcessor.getCurrentSettings();
    const int choice = audioProcessor.getStyleChoice();
    const int index = audioProcessor.getHistoryIndex();
    const int size = audioProcessor.getHistorySize();

    for (int i = 0; i <= phorm::numStyles; ++i)
        styleButtons[i].setToggleState ((choice == PhormAudioProcessor::styleRandom && i == 0) || choice == i - 1, juce::dontSendNotification);

    for (int i = 0; i < phorm::numMorphs; ++i)
        morphButtons[i].setToggleState (settings.morph == i, juce::dontSendNotification);

    for (int i = 0; i < 5; ++i)
        framesButtons[i].setToggleState (settings.frames == PhormAudioProcessor::frameChoices[i], juce::dontSendNotification);

    if (! brightSlider.isMouseButtonDown())
        brightSlider.setValue (std::round (settings.bright * 100.0f), juce::dontSendNotification);

    previousButton.setEnabled (index > 0);
    nextButton.setEnabled (index < size - 1);

    // Message d'export : seulement s'il concerne la table affichée
    const auto last = audioProcessor.getLastExport();
    status = {};
    statusDetail = {};
    if (! last.files.isEmpty() || last.error.isNotEmpty())
    {
        const auto expected = juce::String (phorm::fileStem (settings)) + ".wav";
        if (! last.files.isEmpty() && last.files.getFirst().getFileName() == expected)
        {
            status = "EXPORTED " + expected.toUpperCase();
            juce::StringArray where;
            for (const auto& f : last.files)
                where.add (f.getParentDirectory().getFullPathName());
            statusDetail = where.joinIntoString ("   +   ");
        }
        else if (last.error.isNotEmpty())
        {
            status = "EXPORT FAILED";
            statusDetail = last.error;
        }
    }

    shownSettings = settings;
    shownChoice = choice;
    shownIndex = index;
    shownSize = size;
    repaint();
}

void PhormFacade::timerCallback()
{
    const auto version = audioProcessor.getTableVersion();
    if (version != shownVersion)
    {
        shownVersion = version;
        display.setTable (audioProcessor.getDisplayTable());
        repaint();
    }

    // Position jouée : celle des voix si une note est tenue, sinon celle du potard
    const float knobPosition = audioProcessor.apvts.getRawParameterValue (PhormAudioProcessor::positionId)->load() * 0.01f;
    const bool playing = audioProcessor.getActiveVoices() > 0;
    const float shownPosition = playing ? audioProcessor.getPlayedPosition() : knobPosition;
    display.setPosition (shownPosition);

    // Ligne « FRAME x / y » sous l'afficheur
    const int frameLine = juce::roundToInt (shownPosition * (float) (shownSettings.frames - 1)) * 2 + (playing ? 1 : 0);
    if (frameLine != shownFrameLine)
    {
        shownFrameLine = frameLine;
        repaint (0, 520, leftWidth, 22);
    }

    if (audioProcessor.getCurrentSettings() != shownSettings || audioProcessor.getStyleChoice() != shownChoice
        || audioProcessor.getHistoryIndex() != shownIndex || audioProcessor.getHistorySize() != shownSize)
        refreshControls();

    // Valeurs des potards
    const auto& params = audioProcessor.getParameters();
    bool changed = lastValues.size() != (size_t) params.size();
    lastValues.resize ((size_t) params.size(), -1.0f);
    for (int i = 0; i < params.size(); ++i)
    {
        const float v = params[i]->getValue();
        if (std::abs (v - lastValues[(size_t) i]) > 1.0e-7f)
        {
            lastValues[(size_t) i] = v;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void PhormFacade::paint (juce::Graphics& g)
{
    using LF = PhormLookAndFeel;
    const auto& lf = lookAndFeel;
    g.fillAll (LF::ground());

    const auto label10 = lf.mono (10.0f, 0.14f);
    const auto label9 = lf.mono (9.0f, 0.14f);
    const auto value11 = lf.mono (11.0f, 0.04f);
    const auto value10 = lf.mono (10.0f, 0.04f);

    // En-tête
    const auto wordmark = lf.title (20.0f, 0.36f);
    drawLabel (g, "PHORM", wordmark, juce::Colour (0xfff4f4f4), { 34.0f, 18.0f, 400.0f, 28.0f }, juce::Justification::centredLeft);
    const float afterWordmark = 34.0f + textWidth (wordmark, "PHORM") + 22.0f;
    drawLabel (g, "WAVETABLE GENERATOR", label10, LF::dim(), { afterWordmark, 22.0f, 300.0f, 22.0f }, juce::Justification::centredLeft);
    const auto versionText = "v" + juce::String (PHORM_VERSION);
    drawLabel (g, versionText, value11, LF::label(), { 866.0f, 22.0f, 100.0f, 22.0f }, juce::Justification::centredRight);
    const float byRight = 966.0f - textWidth (value11, versionText) - 16.0f;
    drawLabel (g, "BY PHRAGMA", label10, LF::label(), { byRight - 130.0f, 22.0f, 130.0f, 22.0f }, juce::Justification::centredRight);

    g.setColour (LF::line());
    g.fillRect (0, headerHeight - 1, width, 1);
    g.fillRect (0, footerTop, width, 1);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (leftWidth, headerHeight, 1, footerTop - headerHeight);

    // Titre de la table
    const auto s = shownSettings;
    const auto table = audioProcessor.getDisplayTable();
    const bool building = table == nullptr || table->settings != s.sanitised();
    drawLabel (g, juce::String (phorm::styleName (s.style)) + dot + juce::String ((int) s.seed), lf.title (14.0f, 0.34f),
               juce::Colour (0xfff4f4f4), { 0.0f, 80.0f, (float) leftWidth, 22.0f }, juce::Justification::centred);

    const int harmonics = phorm::harmonicsForBright (s.bright);
    drawLabel (g, juce::String (s.frames) + " FRAMES" + dot + juce::String (harmonics) + " HARMONICS" + dot + phorm::morphName (s.morph)
                   + dot + juce::String (shownIndex + 1) + " / " + juce::String (shownSize),
               label10, LF::dim(), { 0.0f, 104.0f, (float) leftWidth, 16.0f }, juce::Justification::centred);

    // Sous l'afficheur
    {
        const float pos = audioProcessor.getActiveVoices() > 0 ? audioProcessor.getPlayedPosition()
                                                               : audioProcessor.apvts.getRawParameterValue (PhormAudioProcessor::positionId)->load() * 0.01f;
        const int frame = juce::roundToInt (pos * (float) (s.frames - 1)) + 1;
        const auto line = building ? juce::String (juce::CharPointer_UTF8 ("BUILDING THE TABLE\xe2\x80\xa6"))
                                   : "FRAME " + juce::String (frame) + " / " + juce::String (s.frames)
                                     + (audioProcessor.getActiveVoices() > 0 ? dot + "PLAYING" : juce::String());
        drawLabel (g, line, label9, LF::faint(), { 0.0f, 524.0f, (float) leftWidth, 14.0f }, juce::Justification::centred);
    }

    // Colonne de droite
    const float rx = (float) rightX, rw = (float) rightWidth;
    drawLabel (g, "STYLE", label10, LF::ink(), { rx, 78.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "FOR THE NEXT TABLE", label9, LF::faint(), { rx, 78.0f, rw, 14.0f }, juce::Justification::centredRight);

    drawLabel (g, "BRIGHT", label10, LF::ink(), { rx, 190.0f, rw, 16.0f }, juce::Justification::centredLeft);
    drawLabel (g, juce::String (juce::roundToInt (s.bright * 100.0f)) + " %" + dot + juce::String (harmonics) + " harmonics",
               value11, LF::ink(), { rx, 190.0f, rw, 16.0f }, juce::Justification::centredRight);
    drawLabel (g, "DARK", label10, LF::label(), { rx, 238.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "FULL", label10, LF::label(), { rx, 238.0f, rw, 14.0f }, juce::Justification::centredRight);

    drawLabel (g, "MORPH", label10, LF::ink(), { rx, 262.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "FRAMES", label10, LF::ink(), { rx, 328.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "BRIGHT, MORPH, FRAMES: THIS TABLE", label9, LF::faint(), { rx, 262.0f, rw, 14.0f }, juce::Justification::centredRight);

    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (rx, 396.0f, rw, 1.0f);

    drawLabel (g, "PLAY", label10, LF::ink(), { rx, 406.0f, rw, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, "ANY MIDI NOTE", label9, LF::faint(), { rx, 406.0f, rw, 14.0f }, juce::Justification::centredRight);

    for (const Knob* k : { &position, &scan, &rate, &attack, &release, &level })
    {
        const auto b = k->slider.getBounds().toFloat();
        const auto column = juce::Rectangle<float> (b.getCentreX() - knobColumn * 0.5f, b.getBottom(), (float) knobColumn, 14.0f);
        drawLabel (g, k->name, label9, LF::label(), column.translated (0.0f, 8.0f), juce::Justification::centred);
        drawLabel (g, valueText (k->id), value10, LF::ink(), column.translated (0.0f, 22.0f), juce::Justification::centred);
    }

    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRect (rx, 522.0f, rw, 1.0f);

    drawLabel (g, "EXPORT TO SERUM", label10, LF::ink(), { rx, 532.0f, 140.0f, 14.0f }, juce::Justification::centredLeft);
    drawLabel (g, exportTarget, label9, LF::faint(), { rx + 140.0f, 532.0f, rw - 140.0f, 14.0f }, juce::Justification::centredRight);

    // Pied
    if (status.isNotEmpty())
    {
        drawLabel (g, status, label10, LF::ink(), { 34.0f, 652.0f, 932.0f, 16.0f }, juce::Justification::centredLeft);
        drawLabel (g, statusDetail, label9, LF::dim(), { 34.0f, 672.0f, 932.0f, 14.0f }, juce::Justification::centredLeft);
    }
    else
    {
        drawLabel (g, "GENERATE, PLAY A NOTE TO HEAR IT, THEN EXPORT", label10, LF::label(), { 34.0f, 652.0f, 932.0f, 16.0f }, juce::Justification::centredLeft);
        drawLabel (g, "EXPORT WRITES A 2048-SAMPLE WAVETABLE (.WAV) INTO SERUM'S TABLES FOLDER" + dot + "OR DRAG THE TILE ONTO A SERUM OSCILLATOR",
                   label9, LF::faint(), { 34.0f, 672.0f, 932.0f, 14.0f }, juce::Justification::centredLeft);
    }
}

//==============================================================================
PhormAudioProcessorEditor::PhormAudioProcessorEditor (PhormAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", PhormFacade::width);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (PhormFacade::width * 0.6), juce::roundToInt (PhormFacade::height * 0.6),
                     juce::roundToInt (PhormFacade::width * 1.5), juce::roundToInt (PhormFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) PhormFacade::width / (double) PhormFacade::height);

    const int shownWidth = juce::jlimit (juce::roundToInt (PhormFacade::width * 0.6), juce::roundToInt (PhormFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * PhormFacade::height / PhormFacade::width));
    sized = true;
    resized();
}

PhormAudioProcessorEditor::~PhormAudioProcessorEditor() = default;

void PhormAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) PhormFacade::width;
    facade.setTransform (juce::AffineTransform::scale (scale));

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
