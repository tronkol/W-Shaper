#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace ui
{
// Una skin = solo colori (stesso stile di Smoker)
struct Palette
{
    const char* name;
    juce::Colour bg, band, text, strong, body, mark, arc, fill, onFill, field, accent, hot;
    juce::Colour specIn, specOut, gr, curve;
    std::array<juce::Colour, 3> cls; // S, F, C
};

const std::vector<Palette>& skins();
juce::Font font (float size, bool bold = false);
}

class FlatLook : public juce::LookAndFeel_V4
{
public:
    FlatLook();
    void setSkin (int index);
    int getSkin() const { return skin; }
    const ui::Palette& pal() const { return ui::skins()[(size_t) skin]; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 8; }
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

private:
    int skin = 0;
};

const ui::Palette& palOf (const juce::Component&);


//==============================================================================
// IN / GR / OUT nel piede
class Meters : public juce::Component, private juce::Timer
{
public:
    explicit Meters (WShaperProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    WShaperProcessor& proc;
    float in = -90.0f, out = -90.0f, gr = 0.0f, inPk = -90.0f, outPk = -90.0f;
    int inHold = 0, outHold = 0;
};

// N-SHAPER: spettro della banda, rumore bersaglio, correzione, bordi trascinabili
class NsView : public juce::Component, private juce::Timer
{
public:
    explicit NsView (WShaperProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    float xOf (float hz) const;
    float hzOf (float x) const;
    int hitEdge (float x) const;
    WShaperProcessor& proc;
    static constexpr int P = wsk::Vis::kPts;
    std::array<float, P> sS {}, sT {}, sG {};
    int drag = -1, hover = -1;
};

// N-SHAPER: livello della banda rispetto a FLOOR e intervento del momento
class NsMeter : public juce::Component, private juce::Timer
{
public:
    explicit NsMeter (WShaperProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    WShaperProcessor& proc;
    float level = -120.0f, ref = -60.0f, amt = 0.0f;
};


// REFERENCE: forma d'onda dell'a cappella, con il punto che sta usando adesso
class RefView : public juce::Component, private juce::Timer
{
public:
    explicit RefView (WShaperProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    WShaperProcessor& proc;
    static constexpr int kTrail = 24;
    std::array<float, kTrail> trail {};
    int head = 0;
    float quality = 0.0f;
};

// riga di stato nel piede: modello, apprendimento della tua voce, latenza
class StatusRow : public juce::Component, private juce::Timer
{
public:
    explicit StatusRow (WShaperProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    WShaperProcessor& proc;
};

//==============================================================================
class WShaperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WShaperEditor (WShaperProcessor&);
    ~WShaperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseW = 422, baseH = 640;
    static constexpr int pageY = 86, pageH = 434;
    static constexpr double pageAnimMs = 600.0;
    static constexpr int kPages = 2;
    static constexpr float tabW = 195.0f;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    class Content : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
        float tabX = 16.0f;
    };
    class Page : public juce::Component
    {
    public:
        explicit Page (int i) : index (i) {}
        void paint (juce::Graphics&) override;
        int index;
    };

    void setupKnob (Knob&, juce::Component& parent, const juce::String& paramId, const juce::String& label, const juce::String& tip, bool big = false);
    void setupToggle (Toggle&, juce::Component& parent, const juce::String& paramId, const juce::String& text, const juce::String& tip, const juce::String& kind = {});
    static void placeKnob (Knob&, juce::Rectangle<int> cell);
    void showPage (int page, bool animate);
    void applySkin (int index);
    void setParam (const char* id, float value);
    std::vector<Knob*> allKnobs();
    void chooseReference();
    void refreshRefLabel();

    WShaperProcessor& proc;
    FlatLook look;
    Content content;
    juce::Component clip, strip;
    Page pTimbre { 0 }, pRef { 1 };

    NsView nsView;
    NsMeter nsMeter;
    RefView refView;
    Meters meters;
    StatusRow status;

    // TIMBRE
    juce::Label refShort;
    juce::ComboBox resBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> resAtt;
    juce::Slider range { juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox };
    juce::Label startLbl, endLbl;
    bool rangeDragging = false;
    Knob slope, amount, makeup, attack, release, detail, match, loudness, formant, smooth, gate;
    // REFERENCE
    juce::TextButton loadBtn { "LOAD A CAPPELLA" }, clearBtn { "X" };
    juce::Label refName;
    // piede
    Knob mix, output;
    Toggle delta;

    std::array<juce::TextButton, kPages> tabs;
    juce::ComboBox presetBox, skinBox;
    juce::TooltipWindow tooltips { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;

    int page = 0, tick = 0, lastRefVersion = -1;
    float stripShift = 0.0f, animFrom = 0.0f, animTo = 0.0f;
    double animStart = 0.0;
    bool animating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WShaperEditor)
};
