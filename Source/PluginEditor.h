#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ScribaLookAndFeel.h"
#include "Lang.h"

// Grafico della risposta in frequenza catturata
class ResponseDisplay : public juce::Component
{
public:
    void setCapture (std::shared_ptr<const CaptureResult> c) { capture = std::move (c); repaint(); }
    void paint (juce::Graphics& g) override;

    // disegna il grafico in un'area qualsiasi (usato anche per l'esportazione PNG); k = scala dei testi
    static void paintResponse (juce::Graphics& g, juce::Rectangle<float> bounds, const CaptureResult* c, float k);

private:
    std::shared_ptr<const CaptureResult> capture;
};

// Grafico della cattura DYN: curva statica in alto, attacco e rilascio in basso
class DynDisplay : public juce::Component
{
public:
    void setResult (std::shared_ptr<const DynResult> r) { result = std::move (r); repaint(); }
    void paint (juce::Graphics& g) override;

    // disegna il grafico in un'area qualsiasi (usato anche per l'esportazione PNG); k = scala dei testi
    static void paintTransfer (juce::Graphics& g, juce::Rectangle<float> bounds, const DynResult* r, float k);
    static void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> bounds, const DynResult* r, bool attack, float k);

private:
    std::shared_ptr<const DynResult> result;
};

// Pulsante in stile Scriba: principale (gradiente) o secondario (scuro con icona)
class ScribaButton : public juce::Button
{
public:
    enum class Kind { Primary, Secondary };
    ScribaButton (const juce::String& text, Kind k, ScribaIcon i = ScribaIcon::None)
        : juce::Button (text), kind (k), icon (i) {}

    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override;

private:
    Kind kind;
    ScribaIcon icon;
};

// Card di selezione del modo (come le card REFERENCE di Scriba Align)
class ModeCard : public juce::Button
{
public:
    ModeCard (const juce::String& text, ScribaIcon i) : juce::Button (text), icon (i) {}
    void paintButton (juce::Graphics& g, bool isOver, bool isDown) override;

    void mouseEnter (const juce::MouseEvent& e) override { juce::Button::mouseEnter (e); if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent& e) override  { juce::Button::mouseExit (e);  if (onHover) onHover (false); }

    std::function<void (bool)> onHover;

private:
    ScribaIcon icon;
};

// Legenda che compare passando il mouse sulle card SWEEP e NAM
class LegendPanel : public juce::Component
{
public:
    struct Item { bool good; juce::String text; };

    LegendPanel() { setInterceptsMouseClicks (false, false); }

    void setContent (const juce::String& newTitle, std::vector<Item> newItems, const juce::String& newIdeal);
    int getIdealHeight() const;
    void paint (juce::Graphics& g) override;

    static constexpr int margin = 14;   // spazio per l'ombra

private:
    juce::String title, ideal;
    std::vector<Item> items;
};

// Manopola con etichetta
struct ScribaKnob : public juce::Component
{
    ScribaKnob (const juce::String& name, bool bipolar);
    void resized() override;

    juce::Slider slider;
    juce::Label  label;
};

// Selettore della lingua dell'interfaccia: IT | EN
class LangSwitch : public juce::Component
{
public:
    LangSwitch() { setRepaintsOnMouseActivity (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ScribaColours::field);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (isMouseOver() ? ScribaColours::label : ScribaColours::edge);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        const bool english = ScribaLang::get() == ScribaLang::Language::English;
        auto left = r.withWidth (r.getWidth() * 0.5f), right = r.withTrimmedLeft (r.getWidth() * 0.5f);
        g.setColour (ScribaColours::cardOn);
        g.fillRoundedRectangle ((english ? right : left).reduced (2.0f), (r.getHeight() - 4.0f) * 0.5f);

        g.setFont (scribaFont (11.0f, true, 0.12f));
        g.setColour (english ? ScribaColours::label : ScribaColours::teal);
        g.drawText ("IT", left, juce::Justification::centred);
        g.setColour (english ? ScribaColours::teal : ScribaColours::label);
        g.drawText ("EN", right, juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto lang = e.x < getWidth() / 2 ? ScribaLang::Language::Italian : ScribaLang::Language::English;
        if (lang != ScribaLang::get())
        {
            ScribaLang::set (lang);
            repaint();
            if (onChange) onChange();
        }
    }

    std::function<void()> onChange;
};

class ScribaSweepAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit ScribaSweepAudioProcessorEditor (ScribaSweepAudioProcessor&);
    ~ScribaSweepAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setMode (int index);
    void onStartClicked();
    void loadClicked();
    void saveClicked();
    void exportClicked();
    void loadNamInputClicked();
    void chooseNamInputFile();
    void saveNamOutputClicked();
    void paintNamInfo (juce::Graphics& g);
    void showLegend (int mode, bool show);
    void downloadClicked();
    void downloadDynClicked();
    void saveDynClicked();
    void paintDynControls (juce::Graphics& g);
    juce::Image renderDynReport() const;
    bool writeDynPng (const juce::File& file) const;
    juce::Image renderResponseReport() const;
    bool writeResponsePng (const juce::File& file) const;
    bool writeResponseCsv (const juce::File& file) const;

    void refreshTexts();   // aggiorna i testi dei componenti nella lingua scelta
    void paintHeader (juce::Graphics& g);
    void paintLeftPanelFooter (juce::Graphics& g);
    void paintDetails (juce::Graphics& g);
    static void paintPanel (juce::Graphics& g, juce::Rectangle<int> r);
    static void paintSectionLabel (juce::Graphics& g, const juce::String& text, int x, int y, juce::Colour c);

    ScribaSweepAudioProcessor& proc;
    ScribaLookAndFeel lnf;
    juce::Image logo;

    ModeCard playCard    { "PLAY", ScribaIcon::Play };
    ModeCard captureCard { "SWEEP", ScribaIcon::Capture };
    ModeCard namCard     { "NAM", ScribaIcon::Nam };
    ModeCard dynCard     { "DYN", ScribaIcon::Dyn };

    juce::Slider   levelSlider;
    juce::ComboBox sweepBox, tailBox;
    juce::ToggleButton dynToggle { "TEST DINAMICA" };
    juce::ToggleButton rawToggle { "REG. GREZZA" };

    // controlli del modo DYN
    juce::Slider   dynLevelSlider;
    juce::ComboBox dynSpeedBox;
    juce::ToggleButton dynApplyToggle { "APPLICA A PLAY" };

    ScribaButton startButton  { "AVVIA CATTURA", ScribaButton::Kind::Primary };
    ScribaButton loadButton   { "CARICA", ScribaButton::Kind::Secondary, ScribaIcon::Load };
    ScribaButton saveButton   { "SALVA", ScribaButton::Kind::Secondary, ScribaIcon::Save };
    ScribaButton exportButton { "IR WAV", ScribaButton::Kind::Secondary, ScribaIcon::Export };
    ScribaButton downloadButton { "", ScribaButton::Kind::Secondary, ScribaIcon::Save };
    juce::TooltipWindow tooltipWindow { this, 500 };
    LangSwitch langSwitch;

    ResponseDisplay display;
    DynDisplay dynDisplay;
    LegendPanel legend;

    ScribaKnob inputKnob { "INPUT", true }, harmKnob { "ARMONICHE", false }, dynKnob { "DINAMICA", false },
               mixKnob { "MIX", false }, outputKnob { "OUTPUT", true };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> levelAtt, inputAtt, harmAtt, dynAtt, mixAtt, outputAtt, dynLevelAtt;
    std::unique_ptr<ComboAtt>  sweepAtt, tailAtt, dynSpeedAtt;
    std::unique_ptr<ButtonAtt> dynTestAtt, rawAtt, dynApplyAtt;

    std::unique_ptr<juce::FileChooser> chooser;

    std::shared_ptr<const CaptureResult> shownCapture;
    std::shared_ptr<const DynResult> shownDyn;
    int lastDynVersion = -1;
    juce::Rectangle<int> leftPanel, playPanel, barArea;
    float meterLevel = 0.0f;
    int lastVersion = -1;
    int modeIndex = 0;
    juce::Rectangle<int> namInfoArea;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaSweepAudioProcessorEditor)
};
