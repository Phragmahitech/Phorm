#pragma once

#include "PluginProcessor.h"

/**
    Look noir et blanc (comme HyperHats) : fond presque noir, traits fins gris, texte blanc.
    Polices embarquées : Michroma (titres) et IBM Plex Mono (étiquettes, valeurs), licence OFL.
*/
class PhormLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PhormLookAndFeel();

    static juce::Colour ground()      { return juce::Colour (0xff050505); }
    static juce::Colour ink()         { return juce::Colour (0xffefefef); }
    static juce::Colour white()       { return juce::Colour (0xfff2f2f2); }
    static juce::Colour label()       { return juce::Colour (0xff8c8c8c); }
    static juce::Colour dim()         { return juce::Colour (0xff6f6f6f); }
    static juce::Colour faint()       { return juce::Colour (0xff5f5f5f); }
    static juce::Colour line()        { return juce::Colour (0xff1f1f1f); }
    static juce::Colour outline()     { return juce::Colour (0xff4a4a4a); }

    juce::Font title (float height, float tracking) const;
    juce::Font mono (float height, float tracking, bool medium = false) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::Typeface::Ptr michroma, plexRegular, plexMedium;
};

//==============================================================================
/** Vue 3D de la table : les frames empilées en profondeur, la frame jouée en blanc. */
class TableDisplay final : public juce::Component
{
public:
    void setTable (std::shared_ptr<const phorm::Wavetable> t)    { table = std::move (t); repaint(); }
    void setPosition (float p)
    {
        if (std::abs (p - position) > 1.0e-4f)
        {
            position = p;
            repaint();
        }
    }

    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const phorm::Wavetable> table;
    float position = 0.0f;
};

//==============================================================================
/** Tuile « DRAG TO SERUM » : exporte la table puis la fait glisser hors du plugin. */
class DragTile final : public juce::Component
{
public:
    explicit DragTile (PhormLookAndFeel& l) : lf (l)       { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    /** Appelé au début du glisser : renvoie le fichier à glisser (ou rien). */
    std::function<juce::File()> onDragStart;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override       { dragging = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override         { dragging = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override      { repaint(); }
    void mouseExit (const juce::MouseEvent&) override       { repaint(); }

private:
    PhormLookAndFeel& lf;
    bool dragging = false;
};

//==============================================================================
/** La façade à taille fixe (1000 x 720) ; l'éditeur la met à l'échelle. */
class PhormFacade final : public juce::Component,
                          private juce::Timer
{
public:
    static constexpr int width = 1000, height = 720;

    explicit PhormFacade (PhormAudioProcessor&);
    ~PhormFacade() override;

    void paint (juce::Graphics&) override;

    // Pour les tests
    juce::TextButton& getGenerateButton() noexcept          { return generateButton; }
    juce::TextButton& getPreviousButton() noexcept          { return previousButton; }
    juce::TextButton& getStyleButton (int i) noexcept       { return styleButtons[i]; }
    juce::TextButton& getMorphButton (int i) noexcept       { return morphButtons[i]; }
    juce::TextButton& getFramesButton (int i) noexcept      { return framesButtons[i]; }
    juce::TextButton& getSymmetryButton (int i) noexcept    { return symmetryButtons[i]; }
    juce::Slider& getBrightSlider() noexcept                { return brightSlider; }
    void refreshNow()                                       { timerCallback(); }

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        const char* id = nullptr;
        juce::String name;
    };

    void setupKnob (Knob&, const char* id, const char* name, juce::Rectangle<int> bounds);
    void setupSegment (juce::TextButton&, juce::Rectangle<int> bounds);
    void timerCallback() override;
    void refreshControls();
    void doExport();
    juce::String valueText (const char* id) const;

    PhormAudioProcessor& audioProcessor;
    PhormLookAndFeel lookAndFeel;

    TableDisplay display;
    juce::TextButton styleButtons[phorm::numStyles + 1];
    juce::Slider brightSlider;
    juce::TextButton morphButtons[phorm::numMorphs];
    juce::TextButton symmetryButtons[phorm::numSymmetries];
    juce::TextButton framesButtons[5];
    Knob position, scan, rate, attack, release, level;
    juce::TextButton previousButton, generateButton { "GENERATE" }, nextButton;
    juce::TextButton exportButton { "EXPORT" }, folderButton { "FOLDER" };
    DragTile dragTile;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> attachments;

    uint32_t shownVersion = 0;
    phorm::TableSettings shownSettings;
    int shownChoice = -2, shownIndex = -1, shownSize = -1, shownFrameLine = -1;
    std::vector<float> lastValues;
    juce::String status, statusDetail;
    juce::String exportTarget;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhormFacade)
};

//==============================================================================
class PhormAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PhormAudioProcessorEditor (PhormAudioProcessor&);
    ~PhormAudioProcessorEditor() override;

    void resized() override;

    PhormFacade& getFacade() noexcept               { return facade; }

private:
    PhormAudioProcessor& audioProcessor;
    PhormFacade facade;
    bool sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhormAudioProcessorEditor)
};
