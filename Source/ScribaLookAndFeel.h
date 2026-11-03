#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Palette ripresa dalla linea Scriba (Meteora Plugins). Tutti i colori stanno qui.
namespace ScribaColours
{
    const juce::Colour bgTop      { 0xff121824 };
    const juce::Colour bgBottom   { 0xff3d5b80 };
    const juce::Colour panel      { 0xe6152030 };
    const juce::Colour field      { 0xff172131 };
    const juce::Colour edge       { 0xff2a384c };
    const juce::Colour track      { 0xff26344a };
    const juce::Colour grid       { 0xff223046 };
    const juce::Colour teal       { 0xff34e2c6 };
    const juce::Colour tealLight  { 0xffdcfbf4 };
    const juce::Colour tealDark   { 0xff0c3438 };
    const juce::Colour cardOn     { 0xff14353c };
    const juce::Colour text       { 0xfff1f4f8 };
    const juce::Colour textSoft   { 0xffd5dce5 };
    const juce::Colour label      { 0xff8b98aa };
    const juce::Colour dim        { 0xff66758a };
    const juce::Colour amber      { 0xfff2c46d };
    const juce::Colour ice        { 0xff9fd8ea };
    const juce::Colour danger     { 0xffff6b6b };
}

inline juce::Font scribaFont (float height, bool bold = false, float tracking = 0.0f)
{
    juce::Font f (juce::FontOptions ("Arial", height, bold ? juce::Font::bold : juce::Font::plain));
    return tracking != 0.0f ? f.withExtraKerningFactor (tracking) : f;
}

// testo UTF-8 scritto con sequenze di escape (evita problemi di codifica dei sorgenti)
inline juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

//==============================================================================
enum class ScribaIcon { None, Play, Capture, Load, Save, Export, Nam, Dyn };

inline void drawScribaIcon (juce::Graphics& g, ScribaIcon icon, juce::Rectangle<float> a, juce::Colour c, float stroke = 2.4f)
{
    g.setColour (c);
    const juce::PathStrokeType st (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto cx = a.getCentreX(), cy = a.getCentreY();
    const float s = juce::jmin (a.getWidth(), a.getHeight());

    switch (icon)
    {
        case ScribaIcon::Play:
        {
            juce::Path p;
            p.addTriangle (cx - s * 0.28f, cy - s * 0.36f, cx - s * 0.28f, cy + s * 0.36f, cx + s * 0.38f, cy);
            g.strokePath (p, st);
            break;
        }
        case ScribaIcon::Capture:
            g.drawEllipse (cx - s * 0.45f, cy - s * 0.45f, s * 0.9f, s * 0.9f, stroke);
            g.drawEllipse (cx - s * 0.22f, cy - s * 0.22f, s * 0.44f, s * 0.44f, stroke);
            break;

        case ScribaIcon::Load:
        case ScribaIcon::Save:
        {
            juce::Path tray;
            tray.startNewSubPath (cx - s * 0.42f, cy + s * 0.05f);
            tray.lineTo (cx - s * 0.42f, cy + s * 0.40f);
            tray.lineTo (cx + s * 0.42f, cy + s * 0.40f);
            tray.lineTo (cx + s * 0.42f, cy + s * 0.05f);
            g.strokePath (tray, st);

            juce::Path arrow;
            const bool up = icon == ScribaIcon::Load;
            const float tip = up ? cy - s * 0.42f : cy + s * 0.18f;
            const float tail = up ? cy + s * 0.18f : cy - s * 0.42f;
            arrow.startNewSubPath (cx, tail);
            arrow.lineTo (cx, tip);
            const float d = up ? s * 0.18f : -s * 0.18f;
            arrow.startNewSubPath (cx - s * 0.18f, tip + d);
            arrow.lineTo (cx, tip);
            arrow.lineTo (cx + s * 0.18f, tip + d);
            g.strokePath (arrow, st);
            break;
        }
        case ScribaIcon::Export:
        {
            juce::Path w;
            const float x0 = cx - s * 0.45f;
            w.startNewSubPath (x0, cy);
            const float amps[] = { 0.10f, 0.32f, 0.18f, 0.40f, 0.14f, 0.26f, 0.08f };
            for (int i = 0; i < 7; ++i)
            {
                const float x = x0 + s * 0.9f * (float) (i + 1) / 8.0f;
                w.lineTo (x, cy + ((i % 2 == 0) ? -1.0f : 1.0f) * s * amps[i]);
            }
            w.lineTo (cx + s * 0.45f, cy);
            g.strokePath (w, st);
            break;
        }
        case ScribaIcon::Nam:
        {
            // piccola rete neurale: due ingressi, un'uscita
            const juce::Point<float> n1 (cx - s * 0.36f, cy - s * 0.30f), n2 (cx - s * 0.36f, cy + s * 0.30f);
            const juce::Point<float> out (cx + s * 0.36f, cy), mid (cx, cy - s * 0.02f);
            juce::Path links;
            links.startNewSubPath (n1); links.lineTo (mid);
            links.startNewSubPath (n2); links.lineTo (mid);
            links.startNewSubPath (n1); links.lineTo (out);
            links.startNewSubPath (n2); links.lineTo (out);
            links.startNewSubPath (mid); links.lineTo (out);
            g.strokePath (links, juce::PathStrokeType (stroke * 0.6f));
            const float nr = s * 0.13f;
            for (auto p : { n1, n2, out, mid })
                g.fillEllipse (p.x - nr, p.y - nr, nr * 2.0f, nr * 2.0f);
            break;
        }
        case ScribaIcon::Dyn:
        {
            // curva di compressione: sale a 45 gradi, poi si piega sopra la soglia
            const float x0 = cx - s * 0.42f, y0 = cy + s * 0.42f;
            const float kx = cx - s * 0.02f, ky = cy - s * 0.02f;
            juce::Path axis;
            axis.startNewSubPath (x0, cy - s * 0.42f);
            axis.lineTo (x0, y0);
            axis.lineTo (cx + s * 0.42f, y0);
            g.strokePath (axis, juce::PathStrokeType (stroke * 0.55f));

            juce::Path curve;
            curve.startNewSubPath (x0 + s * 0.06f, y0 - s * 0.06f);
            curve.lineTo (kx - s * 0.08f, ky + s * 0.08f);
            curve.quadraticTo (kx + s * 0.02f, ky - s * 0.02f, kx + s * 0.14f, ky - s * 0.06f);
            curve.lineTo (cx + s * 0.42f, cy - s * 0.18f);
            g.strokePath (curve, st);
            break;
        }
        case ScribaIcon::None:
        default: break;
    }
}

//==============================================================================
class ScribaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ScribaLookAndFeel()
    {
        namespace C = ScribaColours;

        setColour (juce::ResizableWindow::backgroundColourId, C::bgTop);
        setColour (juce::Label::textColourId, C::text);

        setColour (juce::Slider::textBoxTextColourId, C::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, C::teal.withAlpha (0.3f));

        setColour (juce::ComboBox::textColourId, C::text);
        setColour (juce::ComboBox::backgroundColourId, C::field);
        setColour (juce::ComboBox::outlineColourId, C::edge);
        setColour (juce::ComboBox::arrowColourId, C::teal);

        setColour (juce::PopupMenu::backgroundColourId, C::field);
        setColour (juce::PopupMenu::textColourId, C::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, C::cardOn);
        setColour (juce::PopupMenu::highlightedTextColourId, C::teal);

        setColour (juce::TextEditor::backgroundColourId, C::field);
        setColour (juce::TextEditor::textColourId, C::text);
        setColour (juce::TextEditor::highlightColourId, C::teal.withAlpha (0.3f));
        setColour (juce::TextEditor::outlineColourId, C::edge);
        setColour (juce::TextEditor::focusedOutlineColourId, C::teal);

        setColour (juce::TooltipWindow::backgroundColourId, C::field);
        setColour (juce::TooltipWindow::textColourId, C::text);
        setColour (juce::TooltipWindow::outlineColourId, C::teal.withAlpha (0.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return scribaFont (14.0f, true); }
    juce::Font getPopupMenuFont() override                        { return scribaFont (14.0f); }

    juce::Font getLabelFont (juce::Label& l) override
    {
        if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)
            return scribaFont (13.0f, true);
        return l.getFont();
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        namespace C = ScribaColours;
        const float size = (float) juce::jmin (w, h);
        const float thick = size * 0.13f;
        const float r = size * 0.5f - thick * 0.5f - 3.0f;
        const juce::Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::PathStrokeType st (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (C::track);
        g.strokePath (track, st);

        const bool bipolar = (bool) s.getProperties()["bipolar"];
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const auto accent = s.isEnabled() ? C::teal : C::dim;

        if (std::abs (angle - from) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent);
            g.strokePath (value, st);
        }

        // cerchio interno
        const float ir = r - thick * 1.25f;
        g.setColour (C::edge);
        g.drawEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f, 1.0f);

        // punto zero (solo bipolari)
        if (bipolar)
        {
            const auto z = c.getPointOnCircumference (r, from);
            const float zr = thick * 0.42f;
            g.setColour (C::field);
            g.fillEllipse (z.x - zr, z.y - zr, zr * 2.0f, zr * 2.0f);
            g.setColour (C::text);
            g.drawEllipse (z.x - zr, z.y - zr, zr * 2.0f, zr * 2.0f, 1.6f);
        }

        // pallino del valore
        const auto p = c.getPointOnCircumference (r, angle);
        const float pr = thick * 0.72f;
        g.setColour (C::field);
        g.fillEllipse (p.x - pr - 2.0f, p.y - pr - 2.0f, (pr + 2.0f) * 2.0f, (pr + 2.0f) * 2.0f);
        g.setColour (accent);
        g.fillEllipse (p.x - pr, p.y - pr, pr * 2.0f, pr * 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
            return;
        }

        namespace C = ScribaColours;
        const float cy = (float) y + (float) h * 0.5f;
        const float left = (float) x + 10.0f, right = (float) (x + w) - 10.0f;

        g.setColour (C::track);
        g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);

        g.setColour (s.isEnabled() ? C::teal : C::dim);
        g.fillRoundedRectangle (left, cy - 3.0f, juce::jmax (0.0f, sliderPos - left), 6.0f, 3.0f);

        g.setColour (s.isEnabled() ? C::text : C::label);
        g.fillEllipse (sliderPos - 10.0f, cy - 10.0f, 20.0f, 20.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        namespace C = ScribaColours;
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (C::field);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (box.hasKeyboardFocus (true) ? C::teal : C::edge);
        g.drawRoundedRectangle (r, 9.0f, 1.0f);

        const float ax = (float) width - 24.0f, ay = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (ax - 6.0f, ay - 3.0f);
        chevron.lineTo (ax, ay + 3.0f);
        chevron.lineTo (ax + 6.0f, ay - 3.0f);
        g.setColour (box.isEnabled() ? C::teal : C::dim);
        g.strokePath (chevron, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 40, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool isOver, bool) override
    {
        namespace C = ScribaColours;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.45f;

        g.setColour (C::field.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour ((on ? C::teal.withAlpha (0.55f) : (isOver ? C::label : C::edge)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, 9.0f, 1.0f);

        auto sw = juce::Rectangle<float> (r.getRight() - 46.0f, r.getCentreY() - 9.0f, 34.0f, 18.0f);
        g.setColour ((on ? C::teal : C::track).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (sw, 9.0f);
        const float kx = on ? sw.getRight() - 16.0f : sw.getX() + 2.0f;
        g.setColour ((on ? C::tealDark : C::label).withMultipliedAlpha (alpha));
        g.fillEllipse (kx, sw.getY() + 2.0f, 14.0f, 14.0f);

        g.setColour ((on ? C::text : C::label).withMultipliedAlpha (alpha));
        g.setFont (scribaFont (12.0f, true, 0.08f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (14.0f).withTrimmedRight (52.0f), juce::Justification::centredLeft);
    }
};
