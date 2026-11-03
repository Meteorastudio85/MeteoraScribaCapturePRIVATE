#include "Lang.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace C = ScribaColours;

//==============================================================================
void ResponseDisplay::paint (juce::Graphics& g)
{
    paintResponse (g, getLocalBounds().toFloat(), capture.get(), 1.0f);
}

void ResponseDisplay::paintResponse (juce::Graphics& g, juce::Rectangle<float> bounds, const CaptureResult* c, float k)
{
    auto plot = bounds.withTrimmedLeft (26.0f * k).withTrimmedBottom (16.0f * k).withTrimmedTop (6.0f * k).withTrimmedRight (6.0f * k);
    const float rangeDb = 24.0f;

    auto xFor = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
    auto yFor = [&] (float db) { return plot.getCentreY() - juce::jlimit (-rangeDb, rangeDb, db) / rangeDb * plot.getHeight() * 0.5f; };

    g.setFont (scribaFont (10.0f * k));
    const double freqs[] = { 20, 100, 1000, 10000, 20000 };
    const char* names[]  = { "20", "100", "1k", "10k", "20k" };
    const double minor[] = { 30, 40, 50, 60, 70, 80, 90, 200, 300, 400, 500, 600, 700, 800, 900,
                             2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000 };

    g.setColour (C::grid);
    for (auto f : minor)
        g.drawLine (xFor (f), plot.getY(), xFor (f), plot.getBottom(), 0.6f * k);

    for (int i = 0; i < 5; ++i)
    {
        const float x = xFor (freqs[i]);
        g.setColour (C::grid.brighter (0.2f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f * k);
        g.setColour (C::dim);
        g.drawText (names[i], juce::Rectangle<float> (x - 18.0f * k, plot.getBottom() + 3.0f * k, 36.0f * k, 12.0f * k),
                    juce::Justification::centred);
    }

    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yFor ((float) db);
        g.setColour (db == 0 ? C::edge.brighter (0.25f) : C::grid);
        g.drawLine (plot.getX(), y, plot.getRight(), y, (db == 0 ? 1.2f : 0.8f) * k);
        if (db % 12 == 0)
        {
            g.setColour (C::dim);
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (plot.getX() - 28.0f * k, y - 6.0f * k, 24.0f * k, 12.0f * k),
                        juce::Justification::centredRight);
        }
    }

    if (c == nullptr || c->responseDb.empty())
    {
        g.setColour (C::label);
        g.setFont (scribaFont (13.0f * k));
        g.drawFittedText (TR ("Nessuna cattura.\nPassa in SWEEP e premi AVVIA CATTURA."),
                          plot.toNearestInt().reduced (10), juce::Justification::centred, 3);
        return;
    }

    juce::Path curve, fill;
    const auto& r = c->responseDb;
    const int n = (int) r.size();
    for (int i = 0; i < n; ++i)
    {
        if (r[(size_t) i] < -110.0f)
            break;

        const double f = 20.0 * std::pow (1000.0, (double) i / (double) (n - 1));
        const float x = xFor (f);
        const float y = yFor (r[(size_t) i] - c->gain1kDb);

        if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); }
        else          curve.lineTo (x, y);
        fill.lineTo (x, y);
    }

    fill.lineTo (curve.getCurrentPosition().x, plot.getBottom());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (C::teal.withAlpha (0.22f), 0.0f, plot.getY(),
                                             C::teal.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (C::teal);
    g.strokePath (curve, juce::PathStrokeType (2.4f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
namespace
{
    juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2).replaceCharacter ('.', ',') + " s";
        return juce::String (ms, ms < 10.0f ? 1 : 0).replaceCharacter ('.', ',') + " ms";
    }

    juce::String formatDb (float db, int decimals = 1)
    {
        return juce::String (db, decimals).replaceCharacter ('.', ',');
    }
}

void DynDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto top = b.removeFromTop (b.getHeight() * 0.58f);
    b.removeFromTop (8.0f);
    auto left = b.removeFromLeft (b.getWidth() * 0.5f - 5.0f);
    b.removeFromLeft (10.0f);

    paintTransfer (g, top, result.get(), 1.0f);
    paintEnvelope (g, left, result.get(), true, 1.0f);
    paintEnvelope (g, b, result.get(), false, 1.0f);
}

void DynDisplay::paintTransfer (juce::Graphics& g, juce::Rectangle<float> bounds, const DynResult* r, float k)
{
    auto plot = bounds.withTrimmedLeft (28.0f * k).withTrimmedBottom (16.0f * k).withTrimmedTop (4.0f * k).withTrimmedRight (6.0f * k);
    const float hi = r != nullptr ? r->settings.maxLevelDb : -6.0f;
    const float lo = hi - DynConst::rangeDb;

    auto xFor = [&] (float db) { return plot.getX() + plot.getWidth() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo); };
    auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo); };

    g.setFont (scribaFont (10.0f * k));
    for (float v = hi; v >= lo - 0.01f; v -= 12.0f)
    {
        g.setColour (C::grid);
        g.drawLine (xFor (v), plot.getY(), xFor (v), plot.getBottom(), 0.8f * k);
        g.drawLine (plot.getX(), yFor (v), plot.getRight(), yFor (v), 0.8f * k);

        g.setColour (C::dim);
        const auto text = juce::String (juce::roundToInt (v));
        g.drawText (text, juce::Rectangle<float> (xFor (v) - 18.0f * k, plot.getBottom() + 3.0f * k, 36.0f * k, 12.0f * k),
                    juce::Justification::centred);
        g.drawText (text, juce::Rectangle<float> (plot.getX() - 30.0f * k, yFor (v) - 6.0f * k, 26.0f * k, 12.0f * k),
                    juce::Justification::centredRight);
    }

    // diagonale 1:1 (nessuna compressione)
    {
        const float dashes[] = { 4.0f * k, 4.0f * k };
        g.setColour (C::edge.brighter (0.35f));
        g.drawDashedLine (juce::Line<float> (xFor (lo), yFor (lo), xFor (hi), yFor (hi)), dashes, 2, 1.0f * k);
    }

    if (r == nullptr)
    {
        g.setColour (C::label);
        g.setFont (scribaFont (13.0f * k));
        g.drawFittedText (TR ("Nessuna cattura DYN.\nPassa in DYN e premi AVVIA CATTURA DYN."),
                          plot.toNearestInt().reduced (10), juce::Justification::centred, 3);
        return;
    }

    // modello adattato (uscita senza il guadagno sotto soglia, cosi' la diagonale e' il riferimento)
    if (r->compresses)
    {
        juce::Path model;
        for (float x = lo; x <= hi + 0.01f; x += 0.25f)
        {
            const float y = x - r->modelGrDb (x);
            if (x <= lo) model.startNewSubPath (xFor (x), yFor (y));
            else         model.lineTo (xFor (x), yFor (y));
        }
        g.setColour (C::ice);
        g.strokePath (model, juce::PathStrokeType (2.0f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float tx = xFor (r->thresholdDb);
        const float dashes[] = { 3.0f * k, 3.0f * k };
        g.setColour (C::amber.withAlpha (0.8f));
        g.drawDashedLine (juce::Line<float> (tx, plot.getY(), tx, plot.getBottom()), dashes, 2, 1.0f * k);
        g.setFont (scribaFont (10.5f * k, true));
        g.drawText (TR ("SOGLIA ") + formatDb (r->thresholdDb),
                    juce::Rectangle<float> (tx - 90.0f * k, plot.getY() + 2.0f * k, 86.0f * k, 12.0f * k),
                    juce::Justification::centredRight);
    }
    else
    {
        g.setColour (C::amber);
        g.setFont (scribaFont (11.0f * k, true));
        g.drawText (TR ("nessuna compressione"), plot.reduced (8.0f * k, 6.0f * k), juce::Justification::topLeft);
    }

    // punti misurati
    const float pr = 2.6f * k;
    for (int i = 0; i < DynConst::numSteps; ++i)
    {
        const float x = r->inDb[(size_t) i];
        const float y = r->outDb[(size_t) i] - r->makeupDb;
        g.setColour (r->usable[(size_t) i] ? C::teal : C::dim);
        g.fillEllipse (xFor (x) - pr, yFor (y) - pr, pr * 2.0f, pr * 2.0f);
    }
}

void DynDisplay::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> bounds, const DynResult* r, bool attack, float k)
{
    auto plot = bounds.withTrimmedLeft (22.0f * k).withTrimmedBottom (15.0f * k).withTrimmedTop (18.0f * k).withTrimmedRight (4.0f * k);

    const bool hasTrace = r != nullptr && r->displayBurst >= 0 && ! (attack ? r->attackTrace : r->releaseTrace).empty();
    const DynBurst* B = hasTrace ? &r->bursts[(size_t) r->displayBurst] : nullptr;

    g.setColour (C::label);
    g.setFont (scribaFont (10.0f * k, true, 0.15f));
    g.drawText (attack ? TR ("ATTACCO") : TR ("RILASCIO"), bounds.withHeight (14.0f * k), juce::Justification::centredLeft);
    if (B != nullptr)
    {
        g.setColour (C::text);
        g.setFont (scribaFont (11.0f * k, true));
        g.drawText (formatMs (attack ? B->attackMs : B->releaseMs), bounds.withHeight (14.0f * k), juce::Justification::centredRight);
    }

    g.setColour (C::field);
    g.fillRoundedRectangle (plot, 6.0f * k);
    g.setColour (C::grid);
    g.drawRoundedRectangle (plot, 6.0f * k, 0.8f * k);

    if (B == nullptr)
    {
        g.setColour (C::dim);
        g.setFont (scribaFont (12.0f * k, true));
        g.drawText ("--", plot, juce::Justification::centred);
        return;
    }

    const auto& tr = attack ? r->attackTrace : r->releaseTrace;
    const float hop = attack ? r->attackHopMs : r->releaseHopMs;
    const float traceMs = hop * (float) (tr.size() - 1);
    if (traceMs <= 0.0f)
        return;
    const float t90 = attack ? B->attack90Ms : B->release90Ms;
    const float tau = attack ? B->attackMs : B->releaseMs;
    const float tMax = juce::jlimit (juce::jmin (attack ? 5.0f : 50.0f, traceMs), traceMs, t90 * 3.0f);
    const float grMax = juce::jmax (1.0f, B->grDb * 1.15f);

    auto xFor = [&] (float t)  { return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, t / tMax); };
    auto yFor = [&] (float gr) { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, gr / grMax); };

    // gain reduction verso il basso, come un meter GR
    const int last = juce::jmin ((int) tr.size() - 1, (int) std::ceil (tMax / hop));
    const int stride = juce::jmax (1, last / 400);

    juce::Path p, fill;
    fill.startNewSubPath (xFor (0.0f), plot.getY());
    for (int i = 0; i <= last; i += stride)
    {
        const float x = xFor (hop * (float) i), y = yFor (tr[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        fill.lineTo (x, y);
    }
    fill.lineTo (p.getCurrentPosition().x, plot.getY());
    fill.closeSubPath();

    g.setColour (C::teal.withAlpha (0.16f));
    g.fillPath (fill);
    g.setColour (C::teal);
    g.strokePath (p, juce::PathStrokeType (1.8f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float dashes[] = { 3.0f * k, 3.0f * k };
    g.setColour (C::amber.withAlpha (0.8f));
    g.drawDashedLine (juce::Line<float> (xFor (tau), plot.getY(), xFor (tau), plot.getBottom()), dashes, 2, 1.0f * k);

    g.setColour (C::dim);
    g.setFont (scribaFont (9.5f * k));
    g.drawText ("0", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f * k, 30.0f * k, 12.0f * k), juce::Justification::centredLeft);
    g.drawText (formatMs (tMax), juce::Rectangle<float> (plot.getRight() - 60.0f * k, plot.getBottom() + 2.0f * k, 60.0f * k, 12.0f * k),
                juce::Justification::centredRight);
    g.drawText ("0", juce::Rectangle<float> (plot.getX() - 22.0f * k, plot.getY() - 2.0f * k, 18.0f * k, 12.0f * k), juce::Justification::centredRight);
    g.drawText ("-" + juce::String (juce::roundToInt (grMax)), juce::Rectangle<float> (plot.getX() - 22.0f * k, plot.getBottom() - 10.0f * k, 18.0f * k, 12.0f * k),
                juce::Justification::centredRight);
}

//==============================================================================
void ScribaButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    if (kind == Kind::Primary)
    {
        auto top = isOver ? C::tealLight.brighter (0.05f) : C::tealLight;
        auto bottom = isDown ? C::teal.darker (0.15f) : C::teal;
        g.setGradientFill (juce::ColourGradient (top.withMultipliedAlpha (alpha), r.getX(), r.getY(),
                                                 bottom.withMultipliedAlpha (alpha), r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 16.0f);

        const auto text = getButtonText();
        const bool longText = text.length() > 13;
        const auto font = scribaFont (longText ? 16.0f : 19.0f, true, longText ? 0.14f : 0.22f);
        g.setFont (font);
        const float charW = longText ? 11.5f : 14.0f;
        const float tw = juce::jmin (r.getWidth() - 60.0f, charW * (float) text.length() + 10.0f);
        auto content = juce::Rectangle<float> (tw + 34.0f, r.getHeight()).withCentre (r.getCentre());

        g.setColour (C::tealDark.withMultipliedAlpha (alpha));
        g.fillEllipse (content.getX(), content.getCentreY() - 9.0f, 18.0f, 18.0f);
        g.drawText (text, content.withTrimmedLeft (34.0f), juce::Justification::centredLeft, false);
        return;
    }

    g.setColour ((isDown ? C::field.darker (0.2f) : C::field).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 11.0f);
    g.setColour ((isOver ? C::teal.withAlpha (0.6f) : C::edge).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, 11.0f, 1.0f);

    auto content = r.reduced (12.0f, 0.0f);
    if (getButtonText().isEmpty() && icon != ScribaIcon::None)
    {
        drawScribaIcon (g, icon, r.withSizeKeepingCentre (18.0f, 18.0f), (isOver ? C::teal : C::textSoft).withMultipliedAlpha (alpha), 2.0f);
        return;
    }

    if (icon != ScribaIcon::None)
    {
        auto iconArea = content.removeFromLeft (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
        drawScribaIcon (g, icon, iconArea, C::textSoft.withMultipliedAlpha (alpha), 2.0f);
        content.removeFromLeft (8.0f);
    }

    g.setColour (C::text.withMultipliedAlpha (alpha));
    g.setFont (scribaFont (12.0f, true, 0.12f));
    g.drawText (getButtonText(), content, juce::Justification::centredLeft, false);
}

void ModeCard::paintButton (juce::Graphics& g, bool isOver, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();

    g.setColour (on ? C::cardOn : C::field);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (on ? C::teal : (isOver ? C::label : C::edge));
    g.drawRoundedRectangle (r, 14.0f, on ? 2.5f : 1.0f);

    const auto col = on ? C::teal : C::textSoft;
    auto iconArea = r.withHeight (r.getHeight() * 0.6f).withSizeKeepingCentre (26.0f, 26.0f).translated (0.0f, 4.0f);
    drawScribaIcon (g, icon, iconArea, col, 2.6f);

    g.setColour (col);
    g.setFont (scribaFont (12.0f, true, 0.12f));
    g.drawText (getButtonText(), r.removeFromBottom (r.getHeight() * 0.36f).translated (0.0f, -4.0f), juce::Justification::centred);
}

//==============================================================================
void LegendPanel::setContent (const juce::String& newTitle, std::vector<Item> newItems, const juce::String& newIdeal)
{
    title = newTitle;
    items = std::move (newItems);
    ideal = newIdeal;
    repaint();
}

int LegendPanel::getIdealHeight() const
{
    return margin * 2 + 18 + 26 + (int) items.size() * 25 + 12 + 46 + 14;
}

void LegendPanel::paint (juce::Graphics& g)
{
    auto box = getLocalBounds().reduced (margin).toFloat();

    juce::Path shape;
    shape.addRoundedRectangle (box, 14.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 18, { 0, 6 }).drawForPath (g, shape);

    g.setColour (C::field);
    g.fillPath (shape);
    g.setColour (C::teal.withAlpha (0.55f));
    g.strokePath (shape, juce::PathStrokeType (1.2f));

    auto area = box.reduced (18.0f, 16.0f);

    g.setColour (C::teal);
    g.setFont (scribaFont (12.0f, true, 0.2f));
    g.drawText (title, area.removeFromTop (18.0f), juce::Justification::centredLeft);
    area.removeFromTop (12.0f);

    for (const auto& item : items)
    {
        auto row = area.removeFromTop (25.0f);
        auto icon = row.removeFromLeft (22.0f).withSizeKeepingCentre (14.0f, 14.0f);

        juce::Path p;
        if (item.good)
        {
            p.startNewSubPath (icon.getX(), icon.getCentreY());
            p.lineTo (icon.getX() + 5.0f, icon.getBottom() - 2.0f);
            p.lineTo (icon.getRight(), icon.getY() + 1.0f);
            g.setColour (C::teal);
        }
        else
        {
            p.startNewSubPath (icon.getX() + 1.0f, icon.getCentreY());
            p.lineTo (icon.getRight() - 1.0f, icon.getCentreY());
            g.setColour (C::amber);
        }
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (scribaFont (14.0f));
        g.setColour (item.good ? C::textSoft : C::label);
        g.drawText (item.text, row.withTrimmedLeft (6.0f), juce::Justification::centredLeft, true);
    }

    area.removeFromTop (8.0f);
    g.setColour (C::edge);
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());
    area.removeFromTop (10.0f);

    g.setColour (C::label);
    g.setFont (scribaFont (11.0f, true, 0.2f));
    g.drawText (TR ("IDEALE PER"), area.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setColour (C::text);
    g.setFont (scribaFont (14.0f, true));
    g.drawText (ideal, area.removeFromTop (20.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
ScribaKnob::ScribaKnob (const juce::String& name, bool bipolar)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (scribaFont (11.0f, true, 0.15f));
    label.setColour (juce::Label::textColourId, C::label);
    addAndMakeVisible (label);
}

void ScribaKnob::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (16));
    slider.setBounds (b);
}

//==============================================================================
ScribaSweepAudioProcessorEditor::ScribaSweepAudioProcessorEditor (ScribaSweepAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    for (auto* card : { &playCard, &captureCard, &namCard, &dynCard })
        addAndMakeVisible (*card);
    playCard.onClick    = [this] { setMode (0); };
    captureCard.onClick = [this] { setMode (1); };
    namCard.onClick     = [this] { setMode (2); };
    dynCard.onClick     = [this] { setMode (3); };
    captureCard.onHover = [this] (bool in) { showLegend (1, in); };
    namCard.onHover     = [this] (bool in) { showLegend (2, in); };
    dynCard.onHover     = [this] (bool in) { showLegend (3, in); };

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (levelSlider);

    sweepBox.addItemList ({ "3 s", "6 s", "10 s" }, 1);
    tailBox.addItemList ({ "0,3 s  EQ / pre", "1 s", "3 s  riverberi", "6 s  riverberi lunghi" }, 1);
    addAndMakeVisible (sweepBox);
    addAndMakeVisible (tailBox);
    addAndMakeVisible (dynToggle);
    addAndMakeVisible (rawToggle);

    dynLevelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    dynLevelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dynSpeedBox.addItemList ({ "Veloce", "Standard", "Lenta  rilasci lunghi" }, 1);
    addChildComponent (dynLevelSlider);
    addChildComponent (dynSpeedBox);
    addChildComponent (dynApplyToggle);
    dynApplyToggle.setTooltip (TR ("Dopo la cattura DYN, usa curva e tempi misurati nella cattura SWEEP caricata in PLAY"));

    startButton.onClick = [this] { onStartClicked(); };
    addAndMakeVisible (startButton);

    for (auto* b : { &loadButton, &saveButton, &exportButton })
        addAndMakeVisible (*b);
    loadButton.onClick   = [this] { if (modeIndex == 2) loadNamInputClicked(); else loadClicked(); };
    saveButton.onClick   = [this] { if (modeIndex == 3) saveDynClicked(); else saveClicked(); };
    exportButton.onClick = [this] { exportClicked(); };

    addAndMakeVisible (display);
    addChildComponent (dynDisplay);

    downloadButton.setTooltip (TR ("Scarica il grafico (PNG o CSV)"));
    downloadButton.onClick = [this] { if (modeIndex == 3) downloadDynClicked(); else downloadClicked(); };
    addAndMakeVisible (downloadButton);

    for (auto* k : { &inputKnob, &harmKnob, &dynKnob, &mixKnob, &outputKnob })
        addAndMakeVisible (*k);

    levelAtt   = std::make_unique<SliderAtt> (proc.apvts, "testLevel", levelSlider);
    inputAtt   = std::make_unique<SliderAtt> (proc.apvts, "input", inputKnob.slider);
    harmAtt    = std::make_unique<SliderAtt> (proc.apvts, "harmonics", harmKnob.slider);
    dynAtt     = std::make_unique<SliderAtt> (proc.apvts, "dynamics", dynKnob.slider);
    mixAtt     = std::make_unique<SliderAtt> (proc.apvts, "mix", mixKnob.slider);
    outputAtt  = std::make_unique<SliderAtt> (proc.apvts, "output", outputKnob.slider);
    sweepAtt   = std::make_unique<ComboAtt>  (proc.apvts, "sweepLength", sweepBox);
    tailAtt    = std::make_unique<ComboAtt>  (proc.apvts, "tail", tailBox);
    dynTestAtt = std::make_unique<ButtonAtt> (proc.apvts, "dynTest", dynToggle);
    rawAtt     = std::make_unique<ButtonAtt> (proc.apvts, "saveRaw", rawToggle);
    dynLevelAtt = std::make_unique<SliderAtt> (proc.apvts, "dynLevel", dynLevelSlider);
    dynSpeedAtt = std::make_unique<ComboAtt>  (proc.apvts, "dynSpeed", dynSpeedBox);
    dynApplyAtt = std::make_unique<ButtonAtt> (proc.apvts, "dynApply", dynApplyToggle);

    levelSlider.onValueChange = [this] { repaint(); };
    dynLevelSlider.onValueChange = [this] { repaint(); };
    dynSpeedBox.onChange = [this] { repaint(); };

    addChildComponent (legend);   // sopra a tutto, visibile solo al passaggio del mouse

    langSwitch.onChange = [this] { refreshTexts(); };
    addAndMakeVisible (langSwitch);
    refreshTexts();

    setSize (760, 740);
    timerCallback();
    startTimerHz (30);
}

ScribaSweepAudioProcessorEditor::~ScribaSweepAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ScribaSweepAudioProcessorEditor::paintPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (C::panel);
    g.fillRoundedRectangle (r.toFloat(), 20.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 20.0f, 1.0f);
}

void ScribaSweepAudioProcessorEditor::paintSectionLabel (juce::Graphics& g, const juce::String& text, int x, int y, juce::Colour c)
{
    g.setColour (c);
    g.setFont (scribaFont (12.0f, true, 0.2f));
    g.drawText (text, x, y, 400, 16, juce::Justification::centredLeft, false);
}

void ScribaSweepAudioProcessorEditor::paintHeader (juce::Graphics& g)
{
    if (logo.isValid())
        g.drawImage (logo, juce::Rectangle<float> (22.0f, 16.0f, 72.0f, 72.0f), juce::RectanglePlacement::centred);

    g.setFont (scribaFont (28.0f, true, 0.22f));
    g.setColour (C::text);
    g.drawText ("SCRIBA", 112, 22, 180, 40, juce::Justification::centredLeft, false);
    g.setColour (C::teal);
    g.drawText ("CAPTURE", 290, 22, 220, 40, juce::Justification::centredLeft, false);

    g.setColour (C::dim);
    g.setFont (scribaFont (11.0f, false, 0.3f));
    g.drawText (TR ("HARDWARE \xc2\xb7 IMPULSO \xc2\xb7 ARMONICHE \xc2\xb7 DINAMICA"), 114, 64, 420, 16,
                juce::Justification::centredLeft, false);

    // pillola di stato
    using State = ScribaSweepAudioProcessor::CaptureState;
    const auto st = proc.getCaptureState();
    juce::String text;
    juce::Colour dot = C::teal;

    if (st == State::Running)                                  { text = TR ("IN CATTURA"); dot = C::amber; }
    else if (st == State::Recorded || st == State::Analysing)  { text = TR ("ANALISI"); dot = C::amber; }
    else if (shownCapture == nullptr)                          { text = TR ("NESSUNA CATTURA"); dot = C::dim; }
    else                                                       { text = shownCapture->ir.getNumChannels() > 1 ? "STEREO" : "MONO"; }

    const float w = 70.0f + 11.0f * (float) text.length();
    auto pill = juce::Rectangle<float> ((float) getWidth() - 20.0f - w, 30.0f, w, 42.0f);
    g.setColour (C::field);
    g.fillRoundedRectangle (pill, 21.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (pill.reduced (0.5f), 21.0f, 1.0f);
    g.setColour (dot);
    g.fillEllipse (pill.getX() + 24.0f, pill.getCentreY() - 7.0f, 14.0f, 14.0f);
    g.setColour (C::text);
    g.setFont (scribaFont (14.0f, true, 0.15f));
    g.drawText (text, pill.withTrimmedLeft (50.0f).withTrimmedRight (16.0f), juce::Justification::centredLeft, false);
}

void ScribaSweepAudioProcessorEditor::paintLeftPanelFooter (juce::Graphics& g)
{
    using State = ScribaSweepAudioProcessor::CaptureState;
    const auto st = proc.getCaptureState();

    float fillAmount = 0.0f;
    juce::Colour fillColour = C::teal;
    juce::String left, right;

    if (st == State::Running)
    {
        fillAmount = proc.getCaptureProgress();
        left = TR ("Cattura in corso");
        right = juce::String (juce::roundToInt (fillAmount * 100.0f)) + "%";
    }
    else if (st == State::Recorded || st == State::Analysing)
    {
        fillAmount = 1.0f;
        fillColour = C::teal.withAlpha (0.45f);
        left = TR ("Analisi in corso");
        right = "...";
    }
    else
    {
        const float db = juce::Decibels::gainToDecibels (meterLevel, -60.0f);
        fillAmount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        fillColour = db >= -0.5f ? C::danger : (db > -6.0f ? C::amber : C::teal);
        left = TR ("Livello ritorno");
        right = db <= -59.9f ? juce::String ("--") : juce::String (db, 1) + " dB";
    }

    auto bar = barArea.toFloat();
    g.setColour (C::track);
    g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    if (fillAmount > 0.0f)
    {
        g.setColour (fillColour);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (bar.getHeight(), bar.getWidth() * fillAmount)), bar.getHeight() * 0.5f);
    }

    auto row = juce::Rectangle<float> (bar.getX(), bar.getBottom() + 14.0f, bar.getWidth(), 20.0f);
    g.setFont (scribaFont (15.0f));
    g.setColour (C::label);
    g.drawText (left, row, juce::Justification::centredLeft);
    g.setFont (scribaFont (15.0f, true));
    g.setColour (C::text);
    g.drawText (right, row, juce::Justification::centredRight);
}

void ScribaSweepAudioProcessorEditor::paintDetails (juce::Graphics& g)
{
    const int x0 = 470, x1 = playPanel.getRight() - 24;
    const int top = playPanel.getY() + 22;

    const bool dynView = modeIndex == 3;
    paintSectionLabel (g, dynView ? "DYN" : TR ("CATTURA"), x0, top, C::label);
    if (dynView && shownDyn != nullptr)
    {
        g.setColour (C::teal);
        g.setFont (scribaFont (12.0f, true, 0.1f));
        g.drawText (shownDyn->name.toUpperCase(), x0 + 86, top, x1 - x0 - 86, 16, juce::Justification::centredLeft, true);
    }
    else if (! dynView && shownCapture != nullptr)
    {
        g.setColour (C::teal);
        g.setFont (scribaFont (12.0f, true, 0.1f));
        g.drawText (shownCapture->name.toUpperCase(), x0 + 86, top, x1 - x0 - 86, 16, juce::Justification::centredLeft, true);
    }

    // linea tratteggiata come in Scriba Align
    {
        const float lx = (float) x0 - 14.0f;
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (C::edge.brighter (0.3f));
        g.drawDashedLine (juce::Line<float> (lx, (float) top, lx, (float) playPanel.getBottom() - 14.0f), dashes, 2, 1.0f);
    }

    struct Row { juce::String name, value; juce::Colour valueColour; float bar; juce::Colour barColour; };
    std::vector<Row> rows;
    const auto& c = shownCapture;

    auto harmBar = [] (float db) { return db <= -119.0f ? 0.0f : juce::jlimit (0.04f, 1.0f, (db + 80.0f) / 70.0f); };
    auto harmText = [] (float db) { return db <= -119.0f ? TR ("assente") : juce::String (db, 1) + " dB"; };

    if (dynView)
    {
        const auto& d = shownDyn;
        if (d == nullptr)
        {
            for (auto* n : { "Soglia", "Ratio", "Knee", "Attacco", "Rilascio" })
                rows.push_back ({ TR (n), "--", C::dim, 0.0f, C::teal });
        }
        else if (! d->compresses)
        {
            rows.push_back ({ TR ("Soglia"), TR ("non rilevata"), C::amber, 0.0f, C::teal });
            for (auto* n : { "Ratio", "Knee", "Attacco", "Rilascio" })
                rows.push_back ({ TR (n), "--", C::dim, 0.0f, C::teal });
        }
        else
        {
            const bool timed = d->displayBurst >= 0;
            rows.push_back ({ TR ("Soglia"), formatDb (d->thresholdDb) + " dBFS", C::text, 0.0f, C::teal });
            rows.push_back ({ "Ratio", formatDb (d->ratio, d->ratio < 10.0f ? 1 : 0) + " : 1", C::text,
                              juce::jlimit (0.04f, 1.0f, std::log (d->ratio) / std::log (20.0f)), C::teal });
            rows.push_back ({ "Knee", formatDb (d->kneeDb) + " dB", C::text, 0.0f, C::teal });
            rows.push_back ({ TR ("Attacco"), timed ? formatMs (d->referenceAttackMs()) : juce::String ("--"), timed ? C::text : C::dim, 0.0f, C::teal });
            rows.push_back ({ TR ("Rilascio"), timed ? formatMs (d->referenceReleaseMs()) : juce::String ("--"), timed ? C::text : C::dim, 0.0f, C::teal });
        }
    }
    else if (c == nullptr)
    {
        for (auto* n : { "Latenza", "2a armonica", "3a armonica", "Dinamica", "Canali" })
            rows.push_back ({ TR (n), "--", C::dim, 0.0f, C::teal });
    }
    else
    {
        rows.push_back ({ TR ("Latenza"), juce::String (juce::jmax (0.0f, c->latencyMs), 2) + " ms", C::text, 0.0f, C::teal });
        rows.push_back ({ TR ("2a armonica"), harmText (c->h2Db), c->h2Db <= -119.0f ? C::amber : C::text, harmBar (c->h2Db), C::ice });
        rows.push_back ({ TR ("3a armonica"), harmText (c->h3Db), c->h3Db <= -119.0f ? C::amber : C::text, harmBar (c->h3Db), C::teal });

        if (c->hasDynamics)
            rows.push_back ({ TR ("Dinamica"), "att " + juce::String (c->attackMs, 1) + TR (" / ril ") + juce::String (c->releaseMs, 0) + " ms",
                              C::text, 0.0f, C::teal });
        else
            rows.push_back ({ TR ("Dinamica"), TR ("non rilevata"), C::amber, 0.0f, C::teal });

        rows.push_back ({ TR ("Canali"), c->ir.getNumChannels() > 1 ? "Stereo" : "Mono", C::text, 0.0f, C::teal });
    }

    int y = top + 26;
    for (auto& r : rows)
    {
        g.setFont (scribaFont (14.0f));
        g.setColour (C::textSoft);
        g.drawText (r.name, x0, y, 110, 20, juce::Justification::centredLeft);

        if (r.bar > 0.0f)
        {
            g.setColour (r.barColour);
            g.fillRoundedRectangle ((float) x0 + 100.0f, (float) y + 6.0f, 70.0f * r.bar, 8.0f, 4.0f);
        }

        g.setFont (scribaFont (14.0f, true));
        g.setColour (r.valueColour);
        g.drawText (r.value, x0 + 100, y, x1 - x0 - 100, 20, juce::Justification::centredRight);
        y += 24;
    }
}

void ScribaSweepAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (C::bgTop, 0.0f, 0.0f, C::bgBottom, (float) getWidth(), (float) getHeight(), false));
    g.fillAll();

    paintHeader (g);

    paintPanel (g, leftPanel);
    paintSectionLabel (g, modeIndex == 3 ? TR ("CURVA DI COMPRESSIONE") : TR ("RISPOSTA IN FREQUENZA"),
                       leftPanel.getX() + 24, leftPanel.getY() + 20, C::label);
    paintLeftPanelFooter (g);

    const int rx = 374;
    paintSectionLabel (g, TR ("MODO"), rx, 112, C::label);

    if (modeIndex == 2)
    {
        paintNamInfo (g);
    }
    else if (modeIndex == 3)
    {
        paintDynControls (g);
    }
    else
    {
        paintSectionLabel (g, TR ("LIVELLO TEST"), rx, 228, C::label);
        g.setColour (C::text);
        g.setFont (scribaFont (18.0f, true));
        g.drawText (juce::String (levelSlider.getValue(), 1) + " dBFS", rx, 222, getWidth() - 20 - rx, 26, juce::Justification::centredRight);
        paintSectionLabel (g, "SWEEP", rx, 284, C::label);
        paintSectionLabel (g, TR ("CODA IR"), sweepBox.getRight() + 12, 284, C::label);
    }

    paintPanel (g, playPanel);
    paintSectionLabel (g, TR ("RIPRODUZIONE"), playPanel.getX() + 24, playPanel.getY() + 22, C::label);
    paintDetails (g);

    // piede
    g.setFont (scribaFont (13.0f));
    g.setColour (C::textSoft.withAlpha (0.85f));
    g.drawText (lastStatus, 22, getHeight() - 44, getWidth() - 140, 18, juce::Justification::centredLeft, true);
    g.setColour (C::label.withAlpha (0.8f));
    g.setFont (scribaFont (11.0f));
    g.drawText (utf8 ("\xc2\xa9 2026 Meteora Plugins - Scriba Capture - v1.2.0"), 22, getHeight() - 24, 400, 16,
                juce::Justification::centredLeft);
}

void ScribaSweepAudioProcessorEditor::refreshTexts()
{
    dynToggle.setButtonText (TR ("TEST DINAMICA"));
    rawToggle.setButtonText (TR ("REG. GREZZA"));
    dynApplyToggle.setButtonText (TR ("APPLICA A PLAY"));
    dynApplyToggle.setTooltip (TR ("Dopo la cattura DYN, usa curva e tempi misurati nella cattura SWEEP caricata in PLAY"));
    downloadButton.setTooltip (TR ("Scarica il grafico (PNG o CSV)"));
    saveButton.setButtonText (TR ("SALVA"));
    harmKnob.label.setText (TR ("ARMONICHE"), juce::dontSendNotification);
    dynKnob.label.setText (TR ("DINAMICA"), juce::dontSendNotification);

    tailBox.changeItemText (1, TR ("0,3 s  EQ / pre"));
    tailBox.changeItemText (3, TR ("3 s  riverberi"));
    tailBox.changeItemText (4, TR ("6 s  riverberi lunghi"));
    tailBox.setSelectedId (tailBox.getSelectedId(), juce::dontSendNotification);
    dynSpeedBox.changeItemText (1, TR ("Veloce"));
    dynSpeedBox.changeItemText (3, TR ("Lenta  rilasci lunghi"));
    dynSpeedBox.setSelectedId (dynSpeedBox.getSelectedId(), juce::dontSendNotification);

    legend.setVisible (false);
    lastVersion = -1;      // ridisegna i dettagli della cattura
    lastDynVersion = -1;
    repaint();
}

void ScribaSweepAudioProcessorEditor::resized()
{
    langSwitch.setBounds (getWidth() - 20 - 84, getHeight() - 40, 84, 26);
    leftPanel = { 20, 104, 330, 400 };
    display.setBounds (leftPanel.getX() + 14, leftPanel.getY() + 46, leftPanel.getWidth() - 28, 270);
    barArea = { leftPanel.getX() + 24, leftPanel.getBottom() - 64, leftPanel.getWidth() - 48, 8 };
    dynDisplay.setBounds (leftPanel.getX() + 14, leftPanel.getY() + 46, leftPanel.getWidth() - 28, 270);
    downloadButton.setBounds (leftPanel.getRight() - 18 - 40, leftPanel.getY() + 12, 40, 32);

    const int rx = 374, rw = getWidth() - 20 - rx, half = (rw - 12) / 2;

    const int quarter = (rw - 24) / 4;
    playCard.setBounds (rx, 134, quarter, 78);
    captureCard.setBounds (rx + quarter + 8, 134, quarter, 78);
    namCard.setBounds (rx + 2 * (quarter + 8), 134, quarter, 78);
    dynCard.setBounds (rx + 3 * (quarter + 8), 134, rw - 3 * (quarter + 8), 78);
    namInfoArea = { rx, 222, rw, 160 };

    levelSlider.setBounds (rx - 6, 250, rw + 12, 26);
    dynLevelSlider.setBounds (rx - 6, 250, rw + 12, 26);

    sweepBox.setBounds (rx, 304, half, 34);
    tailBox.setBounds (rx + half + 12, 304, half, 34);
    dynSpeedBox.setBounds (rx, 304, half, 34);

    dynToggle.setBounds (rx, 348, half, 34);
    rawToggle.setBounds (rx + half + 12, 348, half, 34);
    dynApplyToggle.setBounds (rx, 348, half, 34);

    startButton.setBounds (rx, 394, rw, 56);

    const int bw = (rw - 16) / 3;
    loadButton.setBounds (rx, 462, bw, 42);
    saveButton.setBounds (rx + bw + 8, 462, bw, 42);
    exportButton.setBounds (rx + 2 * (bw + 8), 462, rw - 2 * (bw + 8), 42);

    playPanel = { 20, 520, getWidth() - 40, 172 };
    auto knobs = juce::Rectangle<int> (playPanel.getX() + 14, playPanel.getY() + 46, 420, 116);
    const int kw = knobs.getWidth() / 5;
    for (auto* k : { &inputKnob, &harmKnob, &dynKnob, &mixKnob, &outputKnob })
        k->setBounds (knobs.removeFromLeft (kw));
}

//==============================================================================
void ScribaSweepAudioProcessorEditor::setMode (int index)
{
    if (auto* param = proc.apvts.getParameter ("mode"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
}

void ScribaSweepAudioProcessorEditor::onStartClicked()
{
    if (proc.getCaptureState() == ScribaSweepAudioProcessor::CaptureState::Running)
    {
        proc.cancelCapture();
        return;
    }

    juce::String error;

    if (modeIndex == 2)
    {
        if (! proc.hasNamInput())          { chooseNamInputFile(); return; }
        if (proc.isNamOutputReady())       { saveNamOutputClicked(); return; }
        if (! proc.startNamCapture (error)) proc.setStatusText (error);
        return;
    }

    if (modeIndex == 3)
    {
        if (! proc.startDynCapture (error))
            proc.setStatusText (error);
        return;
    }

    if (! proc.startCapture (error))
        proc.setStatusText (error);
}

void ScribaSweepAudioProcessorEditor::loadNamInputClicked()
{
    juce::PopupMenu menu;
    menu.addItem (1, TR ("Usa l'input NAM integrato"), true, proc.isUsingBuiltInNam());
    menu.addItem (2, TR ("Carica un altro file di input..."));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadButton),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                proc.useBuiltInNamInput();
                                repaint();
                            }
                            else if (result == 2)
                            {
                                chooseNamInputFile();
                            }
                        });
}

void ScribaSweepAudioProcessorEditor::chooseNamInputFile()
{
    chooser = std::make_unique<juce::FileChooser> (TR ("Carica il file di input NAM (es. input.wav)"), juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error;
                              if (! proc.loadNamInput (file, error))
                                  proc.setStatusText (error);
                              repaint();
                          });
}

void ScribaSweepAudioProcessorEditor::saveNamOutputClicked()
{
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("output.wav");
    chooser = std::make_unique<juce::FileChooser> (TR ("Salva l'output NAM"), start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error;
                              if (proc.saveNamOutput (file, error))
                                  proc.setStatusText (TR ("Output NAM salvato: ") + file.withFileExtension ("wav").getFileName()
                                                      + TR (". Caricalo su TONE3000 o nel trainer NAM."));
                              else
                                  proc.setStatusText (error);
                          });
}

void ScribaSweepAudioProcessorEditor::showLegend (int mode, bool show)
{
    if (! show)
    {
        legend.setVisible (false);
        return;
    }

    using Item = LegendPanel::Item;

    if (mode == 1)
        legend.setContent (TR ("COSA CATTURA LO SWEEP"),
                           { { true,  TR ("Risposta in frequenza: colore ed EQ") },
                             { true,  TR ("Code lunghe: riverberi e delay fino a 6 s") },
                             { true,  TR ("2\xc2\xaa e 3\xc2\xaa armonica (saturazione leggera)") },
                             { true,  TR ("Compressione: curva, attacco e rilascio") },
                             { true,  TR ("Latenza, compensata in automatico") },
                             { false, TR ("Saturazione spinta: solo approssimata") } },
                           TR ("EQ, preamp puliti, riverberi, compressori"));
    else if (mode == 3)
        legend.setContent (TR ("COSA CATTURA DYN"),
                           { { true,  TR ("Curva statica: soglia, ratio e knee") },
                             { true,  TR ("Attacco e rilascio su tre livelli") },
                             { true,  TR ("Gain reduction e guadagno sotto soglia") },
                             { true,  TR ("CSV e PNG per costruire il plugin") },
                             { false, TR ("Colore ed EQ: usa SWEEP") },
                             { false, TR ("Saturazione: usa NAM") } },
                           TR ("Compressori, limiter, leveler"));
    else
        legend.setContent (TR ("COSA CATTURA NAM"),
                           { { true,  TR ("Saturazione e distorsione complete") },
                             { true,  TR ("Come cambia il carattere con il livello") },
                             { true,  TR ("Armoniche alte e intermodulazione") },
                             { true,  TR ("Colore e risposta in frequenza") },
                             { false, TR ("Code lunghe (riverberi, delay): no") },
                             { false, TR ("Compressione con rilascio lento: no") } },
                           TR ("Preamp spinti, valvole, pedali, ampli"));

    const int rx = 374, rw = getWidth() - 20 - rx;
    const int m = LegendPanel::margin;
    legend.setBounds (rx - m, captureCard.getBottom() + 8 - m, rw + 2 * m, legend.getIdealHeight());
    legend.setVisible (true);
    legend.toFront (false);
}

void ScribaSweepAudioProcessorEditor::downloadClicked()
{
    if (shownCapture == nullptr)
    {
        proc.setStatusText (TR ("Nessuna risposta da scaricare: fai prima una cattura SWEEP."));
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (TR ("SCARICA RISPOSTA IN FREQUENZA"));
    menu.addItem (1, TR ("Immagine PNG"));
    menu.addItem (2, TR ("Dati CSV (frequenza, dB)"));
    menu.addItem (3, "PNG + CSV");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&downloadButton),
                        [this] (int choice)
                        {
                            if (choice == 0)
                                return;

                            const bool wantPng = choice == 1 || choice == 3;
                            const bool wantCsv = choice == 2 || choice == 3;
                            const auto ext = wantPng ? juce::String ("png") : juce::String ("csv");
                            const auto baseName = juce::File::createLegalFileName (shownCapture->name + TR (" - risposta"));
                            const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                   .getChildFile (baseName + "." + ext);

                            chooser = std::make_unique<juce::FileChooser> (TR ("Scarica la risposta in frequenza"), start, "*." + ext);
                            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                                  [this, wantPng, wantCsv] (const juce::FileChooser& fc)
                                                  {
                                                      const auto file = fc.getResult();
                                                      if (file == juce::File())
                                                          return;

                                                      juce::StringArray saved;
                                                      bool ok = true;

                                                      if (wantPng)
                                                      {
                                                          const auto f = file.withFileExtension ("png");
                                                          if (writeResponsePng (f)) saved.add (f.getFileName()); else ok = false;
                                                      }
                                                      if (wantCsv)
                                                      {
                                                          const auto f = file.withFileExtension ("csv");
                                                          if (writeResponseCsv (f)) saved.add (f.getFileName()); else ok = false;
                                                      }

                                                      proc.setStatusText (ok ? TR ("Scaricato: ") + saved.joinIntoString (" + ")
                                                                             : juce::String (TR ("Impossibile salvare il file.")));
                                                  });
                        });
}

juce::Image ScribaSweepAudioProcessorEditor::renderResponseReport() const
{
    // immagine 1600 x 900 (disegnata a 800 x 450 e scalata x2 per la nitidezza)
    juce::Image img (juce::Image::ARGB, 1600, 900, true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));

        const float W = 800.0f, H = 450.0f;
        g.setGradientFill (juce::ColourGradient (C::bgTop, 0.0f, 0.0f, C::bgBottom, W, H, false));
        g.fillRect (0.0f, 0.0f, W, H);

        const auto* c = shownCapture.get();

        // intestazione
        if (logo.isValid())
            g.drawImage (logo, juce::Rectangle<float> (22.0f, 16.0f, 48.0f, 48.0f), juce::RectanglePlacement::centred);

        g.setFont (scribaFont (22.0f, true, 0.2f));
        g.setColour (C::text);
        g.drawText ("SCRIBA", juce::Rectangle<float> (84.0f, 18.0f, 140.0f, 28.0f), juce::Justification::centredLeft, false);
        g.setColour (C::teal);
        g.drawText ("CAPTURE", juce::Rectangle<float> (222.0f, 18.0f, 180.0f, 28.0f), juce::Justification::centredLeft, false);
        g.setColour (C::label);
        g.setFont (scribaFont (10.0f, true, 0.25f));
        g.drawText (TR ("RISPOSTA IN FREQUENZA"), juce::Rectangle<float> (85.0f, 46.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);

        if (c != nullptr)
        {
            g.setColour (C::teal);
            g.setFont (scribaFont (14.0f, true, 0.05f));
            g.drawText (c->name, juce::Rectangle<float> (380.0f, 20.0f, 398.0f, 20.0f), juce::Justification::centredRight, true);
            g.setColour (C::label);
            g.setFont (scribaFont (11.0f));
            g.drawText (juce::String (c->sampleRate / 1000.0, 1) + TR (" kHz  |  livello test ") + juce::String (c->testLevelDb, 1) + " dBFS",
                        juce::Rectangle<float> (380.0f, 42.0f, 398.0f, 16.0f), juce::Justification::centredRight, true);
        }

        // pannello del grafico
        auto panel = juce::Rectangle<float> (20.0f, 78.0f, W - 40.0f, 290.0f);
        g.setColour (C::panel);
        g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (C::edge);
        g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);
        ResponseDisplay::paintResponse (g, panel.reduced (16.0f, 14.0f).withTrimmedLeft (6.0f), c, 1.1f);

        // riga di dettagli
        if (c != nullptr)
        {
            auto harm = [] (float db) { return db <= -119.0f ? TR ("assente") : juce::String (db, 1) + " dB"; };
            const juce::String labels[] = { TR ("LATENZA"), TR ("2\xc2\xaa ARMONICA"), TR ("3\xc2\xaa ARMONICA"), TR ("DINAMICA"), TR ("CANALI") };
            const juce::String values[] = {
                juce::String (juce::jmax (0.0f, c->latencyMs), 2) + " ms",
                harm (c->h2Db),
                harm (c->h3Db),
                c->hasDynamics ? "att " + juce::String (c->attackMs, 1) + TR (" / ril ") + juce::String (c->releaseMs, 0) + " ms"
                               : juce::String (TR ("non rilevata")),
                c->ir.getNumChannels() > 1 ? juce::String ("Stereo") : juce::String ("Mono")
            };
            const float widths[] = { 120.0f, 130.0f, 130.0f, 250.0f, 100.0f };

            float x = 30.0f;
            for (int i = 0; i < 5; ++i)
            {
                g.setColour (C::label);
                g.setFont (scribaFont (9.5f, true, 0.2f));
                g.drawText (labels[i], juce::Rectangle<float> (x, 380.0f, widths[i], 12.0f), juce::Justification::centredLeft, false);
                g.setColour (i == 3 && ! c->hasDynamics ? C::amber : C::text);
                g.setFont (scribaFont (14.0f, true));
                g.drawText (values[i], juce::Rectangle<float> (x, 394.0f, widths[i], 20.0f), juce::Justification::centredLeft, true);
                x += widths[i];
            }

            g.setColour (C::label);
            g.setFont (scribaFont (10.0f));
            g.drawText (TR ("0 dB = guadagno a 1 kHz (") + juce::String (c->gain1kDb, 1) + " dB)",
                        juce::Rectangle<float> (30.0f, 426.0f, 360.0f, 14.0f), juce::Justification::centredLeft, false);
        }

        g.setColour (C::label);
        g.setFont (scribaFont (10.0f));
        g.drawText (utf8 ("\xc2\xa9 2026 Meteora Plugins - Scriba Capture"), juce::Rectangle<float> (400.0f, 426.0f, 370.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    return img;
}

bool ScribaSweepAudioProcessorEditor::writeResponsePng (const juce::File& file) const
{
    const auto img = renderResponseReport();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (img, *stream);
}

bool ScribaSweepAudioProcessorEditor::writeResponseCsv (const juce::File& file) const
{
    const auto* c = shownCapture.get();
    if (c == nullptr || c->responseDb.empty())
        return false;

    // separatore ";" e virgola decimale: si apre direttamente in Excel in italiano
    auto num = [] (double v, int decimals) { return juce::String (v, decimals).replaceCharacter ('.', ','); };

    juce::String csv;
    csv << TR ("Frequenza (Hz);Livello relativo a 1 kHz (dB);Livello assoluto (dB)\r\n");

    const int n = (int) c->responseDb.size();
    for (int i = 0; i < n; ++i)
    {
        const float db = c->responseDb[(size_t) i];
        if (db < -110.0f)
            break;

        const double f = 20.0 * std::pow (1000.0, (double) i / (double) (n - 1));
        csv << num (f, 1) << ";" << num (db - c->gain1kDb, 2) << ";" << num (db, 2) << "\r\n";
    }

    return file.replaceWithText (csv, false, false, "\r\n");
}

void ScribaSweepAudioProcessorEditor::paintDynControls (juce::Graphics& g)
{
    const int rx = 374;
    paintSectionLabel (g, TR ("LIVELLO MAX"), rx, 228, C::label);
    g.setColour (C::text);
    g.setFont (scribaFont (18.0f, true));
    g.drawText (formatDb ((float) dynLevelSlider.getValue()) + " dBFS", rx, 222, getWidth() - 20 - rx, 26, juce::Justification::centredRight);

    paintSectionLabel (g, TR ("DURATA"), rx, 284, C::label);

    // durata totale del test con le impostazioni attuali
    auto s = proc.getCurrentDynSettings();
    if (s.sampleRate <= 0.0)
        s.sampleRate = 48000.0;
    const auto L = DynEngine::makeLayout (s);
    const int secs = juce::roundToInt ((double) L.total / s.sampleRate);

    const int half = (getWidth() - 20 - rx - 12) / 2;
    auto box = juce::Rectangle<float> ((float) (rx + half + 12), 304.0f, (float) half, 34.0f);
    paintSectionLabel (g, TR ("TEMPO TOTALE"), (int) box.getX(), 284, C::label);
    g.setColour (C::field);
    g.fillRoundedRectangle (box, 9.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (box.reduced (0.5f), 9.0f, 1.0f);
    g.setColour (C::text);
    g.setFont (scribaFont (14.0f, true));
    g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2) + "  min",
                box.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
}

void ScribaSweepAudioProcessorEditor::saveDynClicked()
{
    if (proc.getDynResult() == nullptr)
    {
        proc.setStatusText (TR ("Nessuna cattura DYN da salvare: premi prima AVVIA CATTURA DYN."));
        return;
    }

    const auto base = juce::File::createLegalFileName (proc.getDynResult()->name);
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (base + ".csv");
    chooser = std::make_unique<juce::FileChooser> (TR ("Salva la cattura DYN (CSV + PNG)"), start, "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error, saved;
                              if (! proc.saveDynFiles (file, error, saved))
                              {
                                  proc.setStatusText (error);
                                  return;
                              }

                              const auto png = file.withFileExtension ("png");
                              if (writeDynPng (png))
                                  saved << " + " << png.getFileName();
                              proc.setStatusText (saved);
                          });
}

void ScribaSweepAudioProcessorEditor::downloadDynClicked()
{
    if (shownDyn == nullptr)
    {
        proc.setStatusText (TR ("Nessun grafico da scaricare: fai prima una cattura DYN."));
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (TR ("SCARICA CURVA DI COMPRESSIONE"));
    menu.addItem (1, TR ("Immagine PNG"));
    menu.addItem (2, TR ("Dati CSV (curva, attacco, rilascio)"));
    menu.addItem (3, "PNG + CSV");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&downloadButton),
                        [this] (int choice)
                        {
                            if (choice == 0 || shownDyn == nullptr)
                                return;

                            const bool wantPng = choice == 1 || choice == 3;
                            const bool wantCsv = choice == 2 || choice == 3;
                            const auto ext = wantPng ? juce::String ("png") : juce::String ("csv");
                            const auto baseName = juce::File::createLegalFileName (shownDyn->name + TR (" - compressione"));
                            const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                   .getChildFile (baseName + "." + ext);

                            chooser = std::make_unique<juce::FileChooser> (TR ("Scarica la curva di compressione"), start, "*." + ext);
                            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                                  [this, wantPng, wantCsv] (const juce::FileChooser& fc)
                                                  {
                                                      const auto file = fc.getResult();
                                                      if (file == juce::File() || shownDyn == nullptr)
                                                          return;

                                                      juce::StringArray saved;
                                                      bool ok = true;

                                                      if (wantPng)
                                                      {
                                                          const auto f = file.withFileExtension ("png");
                                                          if (writeDynPng (f)) saved.add (f.getFileName()); else ok = false;
                                                      }
                                                      if (wantCsv)
                                                      {
                                                          const auto f = file.withFileExtension ("csv");
                                                          if (f.replaceWithText (DynEngine::toCsv (*shownDyn), false, false, "\r\n"))
                                                              saved.add (f.getFileName());
                                                          else
                                                              ok = false;
                                                      }

                                                      proc.setStatusText (ok ? TR ("Scaricato: ") + saved.joinIntoString (" + ")
                                                                             : juce::String (TR ("Impossibile salvare il file.")));
                                                  });
                        });
}

juce::Image ScribaSweepAudioProcessorEditor::renderDynReport() const
{
    // immagine 1600 x 900 (disegnata a 800 x 450 e scalata x2)
    juce::Image img (juce::Image::ARGB, 1600, 900, true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (2.0f));

        const float W = 800.0f, H = 450.0f;
        g.setGradientFill (juce::ColourGradient (C::bgTop, 0.0f, 0.0f, C::bgBottom, W, H, false));
        g.fillRect (0.0f, 0.0f, W, H);

        const auto* d = shownDyn.get();

        if (logo.isValid())
            g.drawImage (logo, juce::Rectangle<float> (22.0f, 16.0f, 48.0f, 48.0f), juce::RectanglePlacement::centred);

        g.setFont (scribaFont (22.0f, true, 0.2f));
        g.setColour (C::text);
        g.drawText ("SCRIBA", juce::Rectangle<float> (84.0f, 18.0f, 140.0f, 28.0f), juce::Justification::centredLeft, false);
        g.setColour (C::teal);
        g.drawText ("CAPTURE", juce::Rectangle<float> (222.0f, 18.0f, 180.0f, 28.0f), juce::Justification::centredLeft, false);
        g.setColour (C::label);
        g.setFont (scribaFont (10.0f, true, 0.25f));
        g.drawText (TR ("CURVA DI COMPRESSIONE"), juce::Rectangle<float> (85.0f, 46.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);

        if (d != nullptr)
        {
            g.setColour (C::teal);
            g.setFont (scribaFont (14.0f, true, 0.05f));
            g.drawText (d->name, juce::Rectangle<float> (380.0f, 20.0f, 398.0f, 20.0f), juce::Justification::centredRight, true);
            g.setColour (C::label);
            g.setFont (scribaFont (11.0f));
            g.drawText (juce::String (d->sampleRate / 1000.0, 1) + TR (" kHz  |  livello max ") + formatDb (d->settings.maxLevelDb)
                            + TR (" dBFS  |  tono 1 kHz"),
                        juce::Rectangle<float> (380.0f, 42.0f, 398.0f, 16.0f), juce::Justification::centredRight, true);
        }

        auto panel = juce::Rectangle<float> (20.0f, 78.0f, W - 40.0f, 290.0f);
        g.setColour (C::panel);
        g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (C::edge);
        g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);

        auto inner = panel.reduced (16.0f, 14.0f);
        auto left = inner.removeFromLeft (390.0f);
        inner.removeFromLeft (20.0f);
        DynDisplay::paintTransfer (g, left, d, 1.05f);
        auto top = inner.removeFromTop (inner.getHeight() * 0.5f - 4.0f);
        inner.removeFromTop (8.0f);
        DynDisplay::paintEnvelope (g, top, d, true, 1.05f);
        DynDisplay::paintEnvelope (g, inner, d, false, 1.05f);

        if (d != nullptr)
        {
            const bool timed = d->displayBurst >= 0;
            const juce::String labels[] = { TR ("SOGLIA"), "RATIO", "KNEE", TR ("SOTTO SOGLIA"), TR ("ATTACCO"), TR ("RILASCIO") };
            const juce::String values[] = {
                d->compresses ? formatDb (d->thresholdDb) + " dBFS" : juce::String (TR ("non rilevata")),
                d->compresses ? formatDb (d->ratio, 1) + " : 1" : juce::String ("--"),
                d->compresses ? formatDb (d->kneeDb) + " dB" : juce::String ("--"),
                formatDb (d->makeupDb) + " dB",
                timed ? formatMs (d->referenceAttackMs()) : juce::String ("--"),
                timed ? formatMs (d->referenceReleaseMs()) : juce::String ("--")
            };
            const float widths[] = { 130.0f, 100.0f, 100.0f, 130.0f, 130.0f, 130.0f };

            float x = 30.0f;
            for (int i = 0; i < 6; ++i)
            {
                g.setColour (C::label);
                g.setFont (scribaFont (9.5f, true, 0.2f));
                g.drawText (labels[i], juce::Rectangle<float> (x, 380.0f, widths[i], 12.0f), juce::Justification::centredLeft, false);
                g.setColour (i == 0 && ! d->compresses ? C::amber : C::text);
                g.setFont (scribaFont (14.0f, true));
                g.drawText (values[i], juce::Rectangle<float> (x, 394.0f, widths[i], 20.0f), juce::Justification::centredLeft, true);
                x += widths[i];
            }

            g.setColour (C::label);
            g.setFont (scribaFont (10.0f));
            g.drawText (TR ("Punti = misura, linea = modello; tempi = costante 63% del burst a ")
                            + (timed ? formatDb (d->bursts[(size_t) d->displayBurst].loudDb) : juce::String ("--")) + " dBFS",
                        juce::Rectangle<float> (30.0f, 426.0f, 440.0f, 14.0f), juce::Justification::centredLeft, false);
        }

        g.setColour (C::label);
        g.setFont (scribaFont (10.0f));
        g.drawText (utf8 ("\xc2\xa9 2026 Meteora Plugins - Scriba Capture"), juce::Rectangle<float> (480.0f, 426.0f, 290.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    return img;
}

bool ScribaSweepAudioProcessorEditor::writeDynPng (const juce::File& file) const
{
    const auto img = renderDynReport();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (img, *stream);
}

void ScribaSweepAudioProcessorEditor::paintNamInfo (juce::Graphics& g)
{
    auto a = namInfoArea;
    paintSectionLabel (g, TR ("INPUT NAM"), a.getX(), a.getY() + 6, C::label);

    auto box = juce::Rectangle<float> ((float) a.getX(), (float) a.getY() + 30.0f, (float) a.getWidth(), 50.0f);
    g.setColour (C::field);
    g.fillRoundedRectangle (box, 11.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (box.reduced (0.5f), 11.0f, 1.0f);

    drawScribaIcon (g, ScribaIcon::Nam, box.withWidth (50.0f).withSizeKeepingCentre (24.0f, 24.0f), C::teal, 2.2f);

    const auto desc = proc.getNamInputDescription();
    g.setFont (scribaFont (14.0f, desc.isNotEmpty()));
    g.setColour (desc.isNotEmpty() ? C::text : C::label);
    g.drawText (desc.isNotEmpty() ? desc : juce::String (TR ("Nessun file: premi CARICA INPUT NAM")),
                box.withTrimmedLeft (54.0f).withTrimmedRight (12.0f), juce::Justification::centredLeft, true);

    g.setFont (scribaFont (13.0f));
    g.setColour (C::label);
    g.drawFittedText (TR ("L'input di addestramento NAM e' gia' integrato: imposta la sessione a 48 kHz e avvia. ") +
                      TR ("L'output viene salvato mono 24 bit, lungo quanto l'input: caricalo su TONE3000 o nel trainer ") +
                      TR ("NAM insieme all'input."),
                      a.withTrimmedTop (92).withTrimmedBottom (4), juce::Justification::topLeft, 4);
}

void ScribaSweepAudioProcessorEditor::loadClicked()
{
    chooser = std::make_unique<juce::FileChooser> (TR ("Carica una cattura"), juce::File(), "*.scriba;*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error;
                              if (! proc.loadCaptureFile (file, error))
                                  proc.setStatusText (error);
                          });
}

void ScribaSweepAudioProcessorEditor::saveClicked()
{
    if (proc.getCapture() == nullptr)
    {
        proc.setStatusText (TR ("Nessuna cattura da salvare."));
        return;
    }

    chooser = std::make_unique<juce::FileChooser> (TR ("Salva la cattura"), juce::File(), "*.scriba");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error, saved;
                              if (proc.saveCaptureFile (file, error, saved))
                                  proc.setStatusText (saved);
                              else
                                  proc.setStatusText (error);
                          });
}

void ScribaSweepAudioProcessorEditor::exportClicked()
{
    if (proc.getCapture() == nullptr)
    {
        proc.setStatusText (TR ("Nessuna cattura da esportare."));
        return;
    }

    chooser = std::make_unique<juce::FileChooser> (TR ("Esporta la risposta all'impulso"), juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              juce::String error;
                              if (proc.exportImpulseWav (file, error))
                                  proc.setStatusText (TR ("IR esportata: ") + file.withFileExtension ("wav").getFileName());
                              else
                                  proc.setStatusText (error);
                          });
}

void ScribaSweepAudioProcessorEditor::timerCallback()
{
    using State = ScribaSweepAudioProcessor::CaptureState;

    const int newMode = proc.getModeIndex();
    if (newMode != modeIndex)
    {
        modeIndex = newMode;
        repaint();
    }

    playCard.setToggleState (modeIndex == 0, juce::dontSendNotification);
    captureCard.setToggleState (modeIndex == 1, juce::dontSendNotification);
    namCard.setToggleState (modeIndex == 2, juce::dontSendNotification);
    dynCard.setToggleState (modeIndex == 3, juce::dontSendNotification);

    const auto st = proc.getCaptureState();
    const bool idle = st == State::Idle;
    const bool namMode = modeIndex == 2;
    const bool dynMode = modeIndex == 3;

    juce::String startText = TR ("AVVIA CATTURA");
    if (st == State::Running)                 startText = TR ("ANNULLA");
    else if (namMode && ! proc.hasNamInput()) startText = TR ("CARICA INPUT NAM");
    else if (namMode && proc.isNamOutputReady()) startText = TR ("SALVA OUTPUT NAM");
    else if (namMode)                         startText = TR ("AVVIA CATTURA NAM");
    else if (dynMode)                         startText = TR ("AVVIA CATTURA DYN");

    if (startButton.getButtonText() != startText)
        startButton.setButtonText (startText);

    startButton.setEnabled ((modeIndex > 0 && idle) || st == State::Running);

    for (juce::Component* comp : { (juce::Component*) &levelSlider, (juce::Component*) &sweepBox,
                                   (juce::Component*) &tailBox, (juce::Component*) &dynToggle })
    {
        comp->setVisible (! namMode && ! dynMode);
        comp->setEnabled (idle);
    }

    rawToggle.setVisible (! namMode);

    for (juce::Component* comp : { (juce::Component*) &dynLevelSlider, (juce::Component*) &dynSpeedBox,
                                   (juce::Component*) &dynApplyToggle })
    {
        comp->setVisible (dynMode);
        comp->setEnabled (idle);
    }

    if (display.isVisible() == dynMode)
    {
        display.setVisible (! dynMode);
        dynDisplay.setVisible (dynMode);
    }

    const juce::String loadText = namMode ? "INPUT" : TR ("CARICA");
    if (loadButton.getButtonText() != loadText)
        loadButton.setButtonText (loadText);
    loadButton.setEnabled (idle);

    meterLevel = juce::jmax (proc.getAndResetInputPeak(), meterLevel * 0.85f);

    const int v = proc.getCaptureVersion();
    if (v != lastVersion)
    {
        lastVersion = v;
        shownCapture = proc.getCapture();
        display.setCapture (shownCapture);
    }

    const int dv = proc.getDynVersion();
    if (dv != lastDynVersion)
    {
        lastDynVersion = dv;
        shownDyn = proc.getDynResult();
        dynDisplay.setResult (shownDyn);
    }

    saveButton.setEnabled (dynMode ? shownDyn != nullptr : shownCapture != nullptr);
    downloadButton.setEnabled (dynMode ? shownDyn != nullptr : shownCapture != nullptr);
    exportButton.setEnabled (shownCapture != nullptr);

    const auto status = proc.getStatusText();
    if (status != lastStatus)
        lastStatus = status;

    // aggiorna solo le zone dinamiche
    repaint (barArea.expanded (4, 40));
    repaint (getWidth() - 300, 24, 300, 56);
    repaint (playPanel);
    if (modeIndex == 2)
        repaint (namInfoArea);
    repaint (0, getHeight() - 46, getWidth() - 110, 46);
}
