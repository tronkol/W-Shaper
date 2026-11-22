#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

//==============================================================================
// SKIN
namespace ui
{
const std::vector<Palette>& skins()
{
    static const std::vector<Palette> list = [] {
        std::vector<Palette> v;
        // FUMO (default): grigio fumo con accenti beige
        v.push_back ({ "SMOKE",
                       Colour (0xff2b2c30), Colour (0xff36383d), Colour (0xffc8c6c1), Colour (0xfff4f1ea),
                       Colour (0xff1b1c1f), Colour (0xfff4f1ea), Colour (0xffd8c8a6), Colour (0xffd8c8a6),
                       Colour (0xff1b1c1f), Colour (0xff1b1c1f).withAlpha (0.6f), Colour (0xffe6d3b0), Colour (0xffe07a5f),
                       Colour (0xff63666d), Colour (0xffefece6), Colour (0xffd8c8a6), Colour (0xffefdcb8),
                       { Colour (0xffefe4cc), Colour (0xffa9a49a), Colour (0xffd8b98a) } });
        // ABISSO: blu caldi e morbidi, nello spirito di soothe3
        v.push_back ({ "ABYSS",
                       Colour (0xff1d304b), Colour (0xff253c5c), Colour (0xffb5c8e2), Colour (0xfff2f7ff),
                       Colour (0xff13223a), Colour (0xfff2f7ff), Colour (0xff8ed2ff), Colour (0xff8ed2ff),
                       Colour (0xff13223a), Colour (0xff13223a).withAlpha (0.6f), Colour (0xffffb68a), Colour (0xffff6b7d),
                       Colour (0xff5a7ca8), Colour (0xffe4eeff), Colour (0xff8ed2ff), Colour (0xffffd6a6),
                       { Colour (0xff8ed2ff), Colour (0xffa9ecc4), Colour (0xffffb68a) } });
        // LUNA: nero, bianco, beige (la stessa di ULTRAMEGASUPERMAXICHORUS)
        v.push_back ({ "MOON",
                       Colour (0xff0b0b0c), Colour (0xff18181a), Colour (0xffe9e6df), Colour (0xffffffff),
                       Colour (0xff161618), Colour (0xffffffff), Colour (0xffd8c8a6), Colour (0xffe9e6df),
                       Colour (0xff0b0b0c), Colour (0xffffffff).withAlpha (0.07f), Colour (0xffd8c8a6), Colour (0xffe07a5f),
                       Colour (0xff55534f), Colour (0xffffffff), Colour (0xffd8c8a6), Colour (0xffefe4cc),
                       { Colour (0xffffffff), Colour (0xffb9b3a6), Colour (0xffd8c8a6) } });
        // CARTA: chiara
        v.push_back ({ "PAPER",
                       Colour (0xffece6da), Colour (0xffded6c6), Colour (0xff3a352e), Colour (0xff141210),
                       Colour (0xfff7f3ea), Colour (0xff141210), Colour (0xff141210), Colour (0xff141210),
                       Colour (0xffece6da), Colour (0xffffffff).withAlpha (0.55f), Colour (0xffc8553d), Colour (0xffd1342f),
                       Colour (0xffb9b0a0), Colour (0xff141210), Colour (0xff2f6f9f), Colour (0xffc8553d),
                       { Colour (0xff2f6f9f), Colour (0xff3d8a5a), Colour (0xffc8553d) } });
        return v;
    }();
    return list;
}

Font font (float size, bool bold)
{
    return Font (FontOptions (size, bold ? Font::bold : Font::plain));
}
}

const ui::Palette& palOf (const Component& c)
{
    if (auto* fl = dynamic_cast<FlatLook*> (&c.getLookAndFeel()))
        return fl->pal();
    return ui::skins().front();
}

//==============================================================================
// GEOMETRIA: tutto costruito sullo stesso "squircle" (quadrato stondato)
namespace geo
{
constexpr float kN = 5.0f;

static Point<float> point (Point<float> c, float r, float a)
{
    const float s = std::sin (a), co = std::cos (a);
    const float k = std::pow (std::pow (std::abs (s), kN) + std::pow (std::abs (co), kN), -1.0f / kN);
    return { c.x + r * k * s, c.y - r * k * co };
}

static Path arc (Point<float> c, float r, float a0, float a1)
{
    Path p;
    const int steps = jmax (4, (int) std::ceil (std::abs (a1 - a0) / 0.025f));
    for (int i = 0; i <= steps; ++i)
    {
        const auto pt = point (c, r, a0 + (a1 - a0) * (float) i / (float) steps);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    return p;
}

static Path body (Point<float> c, float r)
{
    auto p = arc (c, r, 0.0f, MathConstants<float>::twoPi);
    p.closeSubPath();
    return p;
}
}

//==============================================================================
FlatLook::FlatLook() { setSkin (0); }

void FlatLook::setSkin (int index)
{
    skin = jlimit (0, (int) ui::skins().size() - 1, index);
    const auto& p = pal();
    setColour (PopupMenu::backgroundColourId,            p.band);
    setColour (PopupMenu::textColourId,                  p.text);
    setColour (PopupMenu::highlightedBackgroundColourId, p.fill);
    setColour (PopupMenu::highlightedTextColourId,       p.onFill);
    setColour (ComboBox::textColourId,                   p.strong);
    setColour (ComboBox::backgroundColourId,             p.field);
    setColour (ComboBox::outlineColourId,                Colours::transparentBlack);
    setColour (ComboBox::arrowColourId,                  p.text);
    setColour (TooltipWindow::backgroundColourId,        p.band);
    setColour (TooltipWindow::textColourId,              p.text);
    setColour (TooltipWindow::outlineColourId,           p.text.withAlpha (0.3f));
    setColour (Label::textColourId,                      p.text);
}

void FlatLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                 float pos, float a0, float a1, Slider& s)
{
    const auto& p  = pal();
    const bool big = (bool) s.getProperties().getWithDefault ("big", false);
    const auto b   = Rectangle<int> (x, y, w, h).toFloat();
    const auto c   = b.getCentre();
    const float r     = jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    const float thick = big ? 4.0f : 2.5f;
    const float ang   = a0 + pos * (a1 - a0);
    const float ar    = r - thick * 0.5f;
    const PathStrokeType stroke (thick, PathStrokeType::mitered, PathStrokeType::butt);

    g.setColour (p.text.withAlpha (0.22f));
    g.strokePath (geo::arc (c, ar, a0, a1), stroke);

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from   = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    if (std::abs (ang - from) > 0.01f)
    {
        g.setColour (p.arc);
        g.strokePath (geo::arc (c, ar, jmin (from, ang), jmax (from, ang)), stroke);
    }

    const float br = r - thick - (big ? 7.0f : 4.0f);
    g.setColour (p.body);
    g.fillPath (geo::body (c, br));

    const auto p0 = geo::point (c, br * (big ? 0.45f : 0.3f), ang);
    const auto p1 = geo::point (c, br * 0.82f, ang);
    g.setColour (p.mark);
    g.drawLine (Line<float> (p0, p1), big ? 3.0f : 2.0f);
}

Label* FlatLook::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (ui::font (11.0f));
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    return l;
}

void FlatLook::drawButtonBackground (Graphics& g, Button& button, const Colour&, bool highlighted, bool)
{
    const auto& p = pal();
    auto b = button.getLocalBounds().toFloat();
    const auto kind = button.getProperties().getWithDefault ("kind", "").toString();
    const bool on = button.getToggleState();

    if (kind == "tab")
        return; // la barretta la disegna il contenuto, cosi' scorre

    b = b.reduced (0.5f);
    // switch in un unico riquadro: gli angoli interni (dove i pulsanti si toccano) restano vivi
    const auto side = button.getProperties().getWithDefault ("joined", "").toString();
    const bool roundL = side != "right" && side != "mid";
    const bool roundR = side != "left" && side != "mid";
    Path shape;
    shape.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), 4.0f, 4.0f,
                               roundL, roundR, roundL, roundR);
    if (on)
    {
        g.setColour (kind == "solo" ? p.accent : p.fill);
        g.fillPath (shape);
    }
    else
    {
        if (highlighted)
        {
            g.setColour (p.text.withAlpha (0.08f));
            g.fillPath (shape);
        }
        g.setColour (p.text.withAlpha (0.35f));
        g.strokePath (shape, PathStrokeType (1.0f));
    }
}

void FlatLook::drawButtonText (Graphics& g, TextButton& button, bool highlighted, bool)
{
    const auto& p = pal();
    const auto kind = button.getProperties().getWithDefault ("kind", "").toString();
    const bool on = button.getToggleState();
    g.setFont (ui::font (kind == "tab" ? 11.5f : 10.5f, true));

    if (kind == "tab")
        g.setColour (on ? p.strong : p.text.withAlpha (highlighted ? 0.85f : 0.55f));
    else
        g.setColour (on ? p.onFill : p.text);

    g.drawText (button.getButtonText(), button.getLocalBounds(), Justification::centred);
}

void FlatLook::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox&)
{
    const auto& p = pal();
    auto b = Rectangle<int> (0, 0, width, height).toFloat();
    g.setColour (p.field);
    g.fillRoundedRectangle (b, 4.0f);

    Path arrowPath;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrowPath.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrowPath.lineTo (ax, ay + 2.0f);
    arrowPath.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (p.text);
    g.strokePath (arrowPath, PathStrokeType (1.4f));
}

void FlatLook::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                 Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::TwoValueHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const auto& p = pal();
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (p.text.withAlpha (0.22f));
    g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
    g.setColour (p.arc);
    g.fillRoundedRectangle (minPos, cy - 1.5f, jmax (0.0f, maxPos - minPos), 3.0f, 1.5f);
    for (float px : { minPos, maxPos })
    {
        g.setColour (p.body);
        g.fillPath (geo::body ({ px, cy }, 7.0f));
        g.setColour (p.arc);
        g.strokePath (geo::body ({ px, cy }, 7.0f), PathStrokeType (2.0f));
    }
}

Font FlatLook::getComboBoxFont (ComboBox&) { return ui::font (12.0f); }
Font FlatLook::getPopupMenuFont()          { return ui::font (13.0f); }

void FlatLook::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// MARCHIO
namespace brand
{
// due voci che si sovrappongono: la tua (contorno) e il riferimento (pieno, accento)
static void drawMark (Graphics& g, Rectangle<float> box, const ui::Palette& p)
{
    const float u = box.getWidth() / 26.0f;
    const auto at = [&] (float x, float y) { return Point<float> (box.getX() + x * u, box.getY() + y * u); };
    g.setColour (p.accent);
    g.fillPath (geo::body (at (16.5f, 15.5f), 7.5f * u));
    g.setColour (p.strong.withAlpha (0.8f));
    g.strokePath (geo::body (at (9.5f, 10.5f), 7.5f * u), PathStrokeType (1.6f));
}

// W-SHAPER stirato fino a riempire il riquadro, con contorno dello stesso colore (piu' bold)
static void drawWordmark (Graphics& g, Rectangle<float> box, const ui::Palette& p)
{
    GlyphArrangement ga;
    ga.addLineOfText (ui::font (100.0f, true), "W-SHAPER", 0.0f, 0.0f);
    Path path;
    ga.createPath (path);
    const auto b = path.getBounds();
    if (b.isEmpty()) return;
    const float st = 1.1f;
    path.applyTransform (AffineTransform::translation (-b.getX(), -b.getY())
                             .scaled ((box.getWidth() - st) / b.getWidth(), (box.getHeight() - st) / b.getHeight())
                             .translated (box.getX() + st * 0.5f, box.getY() + st * 0.5f));
    g.setColour (p.strong);
    g.fillPath (path);
    g.strokePath (path, PathStrokeType (st, PathStrokeType::curved, PathStrokeType::rounded));
}
}

//==============================================================================
//==============================================================================
//==============================================================================
NsView::NsView (WShaperProcessor& p) : proc (p)
{
    sS.fill (-120.0f); sT.fill (-120.0f);
    startTimerHz (60);
}

float NsView::xOf (float hz) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    return r.getX() + r.getWidth() * std::log (jlimit (20.0f, 20000.0f, hz) / 20.0f) / std::log (1000.0f);
}

float NsView::hzOf (float x) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

int NsView::hitEdge (float x) const
{
    const float a = xOf (proc.apvts.getRawParameterValue ("start")->load());
    const float b = xOf (proc.apvts.getRawParameterValue ("end")->load());
    const float da = std::abs (x - a), dbb = std::abs (x - b);
    if (jmin (da, dbb) > 9.0f) return -1;
    return da <= dbb ? 0 : 1;
}

void NsView::timerCallback()
{
   #ifndef SMOKER_SNAPSHOT
    if (! isShowing()) return;
   #endif
    auto& v = proc.vis();
    for (int i = 0; i < P; ++i)
    {
        sS[(size_t) i] += (v.nsS[(size_t) i].load() - sS[(size_t) i]) * 0.5f;
        sT[(size_t) i] += (v.nsT[(size_t) i].load() - sT[(size_t) i]) * 0.5f;
        sG[(size_t) i] += (v.nsG[(size_t) i].load() - sG[(size_t) i]) * 0.5f;
    }
    repaint();
}

void NsView::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (p.body);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    Graphics::ScopedSaveState ss (g);
    {
        Path cp;
        cp.addRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.reduceClipRegion (cp);
    }

    auto val = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const bool on = true;
    const float fs = val ("start"), fe = val ("end"), slope = val ("slope");

    // banda: piu' chiara dove N-SHAPER lavora, sfuma con la SLOPE
    for (int x = 0; x < (int) r.getWidth(); x += 2)
    {
        const float px = r.getX() + (float) x;
        const float w = wsk::nsBandWeight (hzOf (px), fs, fe, slope);
        if (w > 0.01f)
        {
            g.setColour (p.accent.withAlpha (0.10f * w));
            g.fillRect (px, r.getY(), 2.0f, r.getHeight());
        }
    }
    g.setFont (ui::font (9.0f));
    for (int f : { 100, 1000, 10000 })
    {
        const float x = xOf ((float) f);
        g.setColour (p.text.withAlpha (0.07f));
        g.drawVerticalLine (roundToInt (x), r.getY(), r.getBottom());
        g.setColour (p.text.withAlpha (0.4f));
        g.drawText (f == 100 ? "100" : (f == 1000 ? "1k" : "10k"), Rectangle<float> (x + 3.0f, r.getBottom() - 13.0f, 30.0f, 12.0f), Justification::centredLeft);
    }

    auto xAt  = [this] (int i) { return xOf (20.0f * std::pow (1000.0f, ((float) i + 0.5f) / (float) P)); };
    auto ySp  = [&r] (float db) { return r.getY() + 8.0f + (r.getHeight() - 8.0f) * jlimit (0.0f, 1.0f, -db / 100.0f); };
    const float y0 = r.getY() + r.getHeight() * 0.5f;
    auto yG   = [&r, y0] (float db) { return y0 - jlimit (-24.0f, 24.0f, db) / 24.0f * r.getHeight() * 0.42f; };

    if (on)
    {
        // spettro misurato
        Path ps;
        ps.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < P; ++i) ps.lineTo (xAt (i), ySp (sS[(size_t) i]));
        ps.lineTo (r.getRight(), r.getBottom());
        ps.closeSubPath();
        g.setColour (p.specIn.withAlpha (0.5f));
        g.fillPath (ps);

        // rumore bersaglio, solo dentro la banda
        Path pt;
        bool open = false;
        for (int i = 0; i < P; ++i)
        {
            const float x = xAt (i);
            if (wsk::nsBandWeight (hzOf (x), fs, fe, slope) > 0.3f)
            {
                const Point<float> q (x, ySp (sT[(size_t) i]));
                if (! open) { pt.startNewSubPath (q); open = true; } else pt.lineTo (q);
            }
            else open = false;
        }
        g.setColour (p.curve);
        const float dashes[] = { 4.0f, 3.0f };
        Path dashed;
        PathStrokeType (1.6f).createDashedStroke (dashed, pt, dashes, 2);
        g.fillPath (dashed);

        // correzione applicata (+ alza, - taglia)
        g.setColour (p.text.withAlpha (0.18f));
        g.drawHorizontalLine (roundToInt (y0), r.getX(), r.getRight());
        Path pg, fill;
        fill.startNewSubPath (r.getX(), y0);
        for (int i = 0; i < P; ++i)
        {
            const Point<float> q (xAt (i), yG (sG[(size_t) i]));
            if (i == 0) pg.startNewSubPath (q); else pg.lineTo (q);
            fill.lineTo (q);
        }
        fill.lineTo (r.getRight(), y0);
        fill.closeSubPath();
        g.setColour (p.gr.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (p.gr);
        g.strokePath (pg, PathStrokeType (1.6f));
    }

    // bordi trascinabili
    for (int e = 0; e < 2; ++e)
    {
        const float x = xOf (e == 0 ? fs : fe);
        const bool hot = e == drag || e == hover;
        g.setColour (p.accent.withAlpha (hot ? 1.0f : 0.7f));
        g.fillRect (x - 0.5f, r.getY(), 1.0f, r.getHeight());
        g.fillPath (geo::body ({ x, r.getBottom() - 26.0f }, hot ? 6.0f : 5.0f)); // in basso: non copre le scritte
    }

    g.setFont (ui::font (10.0f, true));
    g.setColour (p.curve);
    const bool fileOn = proc.vis().refActive.load() != 0;
    const String tgtName = fileOn ? String ("REFERENCE TIMBRE") : String ("NO REFERENCE: LOAD AN A CAPPELLA");
    g.drawText (tgtName, Rectangle<float> (r.getX() + 8.0f, r.getY() + 4.0f, 220.0f, 14.0f), Justification::centredLeft);
    g.setColour (p.gr);
    g.drawText ("CORRECTION", Rectangle<float> (r.getX() + 8.0f, r.getY() + 18.0f, 140.0f, 14.0f), Justification::centredLeft);
    if (! on)
    {
        g.setColour (p.text.withAlpha (0.55f));
        g.setFont (ui::font (11.0f));
        g.drawText ("N-SHAPER is off: turn it on to use this algorithm instead of SMOKER", r.reduced (10.0f, 0.0f), Justification::centred);
    }
}

void NsView::mouseMove (const MouseEvent& e)
{
    const int h = hitEdge (e.position.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void NsView::mouseDown (const MouseEvent& e)
{
    drag = hitEdge (e.position.x);
    if (drag >= 0)
        if (auto* prm = proc.apvts.getParameter (drag == 0 ? "start" : "end")) prm->beginChangeGesture();
}

void NsView::mouseDrag (const MouseEvent& e)
{
    if (drag < 0) return;
    if (auto* prm = proc.apvts.getParameter (drag == 0 ? "start" : "end"))
    {
        float f = hzOf (e.position.x);
        const float other = proc.apvts.getRawParameterValue (drag == 0 ? "end" : "start")->load();
        f = drag == 0 ? jmin (f, other / 1.06f) : jmax (f, other * 1.06f);
        prm->setValueNotifyingHost (prm->convertTo0to1 (f));
    }
}

void NsView::mouseUp (const MouseEvent&)
{
    if (drag >= 0)
        if (auto* prm = proc.apvts.getParameter (drag == 0 ? "start" : "end")) prm->endChangeGesture();
    drag = -1;
}

//==============================================================================
NsMeter::NsMeter (WShaperProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void NsMeter::timerCallback()
{
    auto& v = proc.vis();
    const float l = v.nsLevel.load();
    level = l > level ? l : level + (l - level) * 0.25f;
    ref = v.nsRef.load();
    amt += (v.nsAmtNow.load() - amt) * 0.4f;
    repaint();
}

void NsMeter::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    auto b = getLocalBounds().toFloat();
    g.setColour (p.body);
    g.fillRoundedRectangle (b, 5.0f);
    auto r = b.reduced (10.0f, 8.0f);

    const bool on = true;
    auto xOf = [&r] (float db) { return r.getX() + r.getWidth() * jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f); };

    g.setFont (ui::font (9.5f, true));
    g.setColour (p.text.withAlpha (0.7f));
    g.drawText ("BAND LEVEL", r.removeFromTop (13.0f), Justification::centredLeft);

    auto bar = r.removeFromTop (10.0f);
    g.setColour (p.text.withAlpha (0.15f));
    g.fillRoundedRectangle (bar, 2.0f);
    // i 60 dB su cui DYNAMIC misura la presenza: dal picco recente in giu'
    if (on)
    {
        g.setColour (p.accent.withAlpha (0.2f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (xOf (ref - 60.0f), bar.getY(), xOf (ref), bar.getBottom()));
        g.setColour (p.arc);
        g.fillRoundedRectangle (bar.withRight (xOf (level)), 2.0f);
        g.setColour (p.accent);
        g.fillRect (xOf (ref) - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
    }

    r.removeFromTop (6.0f);
    g.setFont (ui::font (9.5f));
    g.setColour (p.text.withAlpha (0.55f));
    g.drawText ("-90", r.withHeight (12.0f), Justification::centredLeft);
    g.drawText ("0 dB", r.withHeight (12.0f), Justification::centredRight);
    r.removeFromTop (16.0f);
    g.setFont (ui::font (10.5f, true));
    g.setColour (p.strong);
    g.drawText (on ? "AMOUNT NOW  " + String (roundToInt (amt * 100.0f)) + " %" : String ("N-SHAPER OFF"), r.withHeight (14.0f), Justification::centredLeft);
}

//==============================================================================
Meters::Meters (WShaperProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Meters::timerCallback()
{
    auto& v = proc.vis();
    const float i = Decibels::gainToDecibels (v.inPeak.load(), -90.0f);
    const float o = Decibels::gainToDecibels (v.outPeak.load(), -90.0f);
    in  = i > in  ? i : in  - 1.5f;
    out = o > out ? o : out - 1.5f;
    if (i >= inPk)  { inPk = i;  inHold = 30; } else if (--inHold < 0)  inPk -= 1.0f;
    if (o >= outPk) { outPk = o; outHold = 30; } else if (--outHold < 0) outPk -= 1.0f;
    gr += (v.grNow.load() - gr) * 0.35f;
    repaint();
}

void Meters::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    auto b = getLocalBounds().toFloat();
    const float colW = b.getWidth() / 3.0f;
    const char* names[] = { "IN", "GR", "OUT" };

    for (int k = 0; k < 3; ++k)
    {
        auto c = b.withX (b.getX() + colW * (float) k).withWidth (colW);
        auto lab = c.removeFromBottom (13.0f);
        auto val = c.removeFromBottom (13.0f);
        auto bar = c.withSizeKeepingCentre (10.0f, c.getHeight() - 4.0f);

        g.setColour (p.body);
        g.fillRoundedRectangle (bar, 2.0f);

        float frac = 0.0f, pk = -1.0f;
        String txt;
        if (k == 1)
        {
            frac = jlimit (0.0f, 1.0f, gr / 24.0f);
            g.setColour (p.gr);
            g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 2.0f);
            txt = String (gr, 1);
        }
        else
        {
            const float lv = k == 0 ? in : out, pkv = k == 0 ? inPk : outPk;
            frac = jlimit (0.0f, 1.0f, (lv + 60.0f) / 60.0f);
            pk = jlimit (0.0f, 1.0f, (pkv + 60.0f) / 60.0f);
            g.setColour (lv > -0.3f ? p.hot : p.arc);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * frac), 2.0f);
            if (pk > 0.0f)
            {
                g.setColour (p.strong.withAlpha (0.8f));
                g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * pk, bar.getWidth(), 1.5f);
            }
            txt = pkv <= -89.0f ? String ("-inf") : String (pkv, 1);
        }
        g.setFont (ui::font (9.5f));
        g.setColour (p.strong);
        g.drawText (txt, val, Justification::centred);
        g.setFont (ui::font (9.5f, true));
        g.setColour (p.text.withAlpha (0.7f));
        g.drawText (names[k], lab, Justification::centred);
    }
}

//==============================================================================
//==============================================================================
RefView::RefView (WShaperProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    trail.fill (-1.0f);
    startTimerHz (30);
}

void RefView::timerCallback()
{
    auto& v = proc.vis();
    trail[(size_t) head] = v.refActive.load() != 0 ? v.refPos.load() : -1.0f;
    head = (head + 1) % kTrail;
    quality += (v.matchQ.load() - quality) * 0.3f;
    repaint();
}

void RefView::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    const auto b = getLocalBounds().toFloat();
    g.setColour (p.body);
    g.fillRoundedRectangle (b, 5.0f);
    const auto* r = proc.reference();
    if (r == nullptr || r->audio.overview.empty())
    {
        g.setColour (p.text.withAlpha (0.55f));
        g.setFont (ui::font (11.0f));
        g.drawFittedText ("No reference loaded.\nLoad an a cappella of the voice you want to sound like.",
                          b.reduced (14.0f, 0.0f).toNearestInt(), Justification::centred, 3);
        return;
    }
    auto area = b.reduced (8.0f, 22.0f);
    const auto& ov = r->audio.overview;
    const int n = (int) ov.size();
    Path w;
    w.startNewSubPath (area.getX(), area.getCentreY());
    for (int i = 0; i < n; ++i)
        w.lineTo (area.getX() + area.getWidth() * (float) i / (float) (n - 1), area.getCentreY() - ov[(size_t) i] * area.getHeight() * 0.5f);
    for (int i = n - 1; i >= 0; --i)
        w.lineTo (area.getX() + area.getWidth() * (float) i / (float) (n - 1), area.getCentreY() + ov[(size_t) i] * area.getHeight() * 0.5f);
    w.closeSubPath();
    g.setColour (p.text.withAlpha (0.45f));
    g.fillPath (w);

    // gli istanti dell'a cappella che sta usando adesso (con una scia che sfuma)
    for (int i = 0; i < kTrail; ++i)
    {
        const int idx = (head + i) % kTrail;
        const float ps = trail[(size_t) idx];
        if (ps < 0.0f) continue;
        const float age = (float) (i + 1) / (float) kTrail;
        const float x = area.getX() + area.getWidth() * ps;
        g.setColour (p.accent.withAlpha (0.15f + 0.85f * age * age));
        if (i == kTrail - 1) g.fillRect (x - 1.0f, b.getY() + 18.0f, 2.0f, b.getHeight() - 36.0f);
        else g.fillPath (geo::body ({ x, b.getBottom() - 11.0f }, 2.5f));
    }

    g.setFont (ui::font (9.5f, true));
    g.setColour (p.accent);
    g.drawText ("MOMENT IN USE", b.reduced (8.0f, 4.0f).removeFromTop (12.0f), Justification::centredLeft);
    g.setColour (p.text.withAlpha (0.7f));
    g.drawText (String (r->model.F) + " moments analysed  -  match " + String (roundToInt (quality * 100.0f)) + " %",
                b.reduced (8.0f, 4.0f).removeFromTop (12.0f), Justification::centredRight);
}

//==============================================================================
StatusRow::StatusRow (WShaperProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}

void StatusRow::timerCallback() { repaint(); }

void StatusRow::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    auto b = getLocalBounds().toFloat();
    auto& v = proc.vis();
    const bool on = v.refActive.load() != 0;
    const float learn = v.learn.load();
    g.setFont (ui::font (9.5f, true));
    const Point<float> c (b.getX() + 5.0f, b.getCentreY());
    g.setColour (on ? p.accent : p.text.withAlpha (0.25f));
    g.fillPath (geo::body (c, 4.0f));
    g.setColour (p.text.withAlpha (0.75f));
    String t = on ? (learn < 0.99f ? "LEARNING YOUR VOICE " + String (roundToInt (learn * 100.0f)) + " %" : String ("TRANSFERRING TIMBRE"))
                  : String ("NO REFERENCE");
    g.drawText (t, Rectangle<float> (b.getX() + 14.0f, b.getY(), 220.0f, b.getHeight()), Justification::centredLeft);
    g.setColour (p.text.withAlpha (0.5f));
    g.setFont (ui::font (9.5f));
    g.drawText ("LATENCY " + String (proc.latencyMs()) + " ms", b, Justification::centredRight);
}

//==============================================================================
// EDITOR
void WShaperEditor::Content::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    g.fillAll (p.bg);
    g.setColour (p.band);
    g.fillRect (0, 0, baseW, pageY);
    g.fillRect (0, pageY + pageH, baseW, baseH - pageY - pageH);
    brand::drawMark (g, { 16.0f, 14.0f, 26.0f, 26.0f }, p);
    brand::drawWordmark (g, { 50.0f, 18.0f, 112.0f, 18.0f }, p);
    g.setColour (p.strong);
    g.fillRect (tabX + 10.0f, (float) pageY - 2.0f, tabW - 20.0f, 2.0f);
}

void WShaperEditor::Page::paint (Graphics& g)
{
    const auto& p = palOf (*this);
    if (index == 1)
    {
        g.setColour (p.text.withAlpha (0.6f));
        g.setFont (ui::font (10.5f));
        g.drawFittedText ("W-Shaper analyses the a cappella moment by moment. While you sing, it finds the moments where the reference "
                          "sings the same thing (same vowel, same consonant, similar intensity) and gives your voice their timbre. "
                          "Pitch, timing and level stay yours. Best reference: a dry a cappella of 1-3 minutes, full bandwidth, "
                          "with plenty of different vowels and words. It takes a few seconds of your singing to learn your voice.",
                          Rectangle<int> (16, 346, 390, 80), Justification::topLeft, 7);
    }
}

WShaperEditor::WShaperEditor (WShaperProcessor& p)
    : AudioProcessorEditor (&p), proc (p), nsView (p), nsMeter (p), refView (p), meters (p), status (p)
{
    setLookAndFeel (&look);
    look.setSkin ((int) proc.apvts.state.getProperty ("skin", 0));

    addAndMakeVisible (content);
    content.addAndMakeVisible (clip);
    clip.addAndMakeVisible (strip);
    for (auto* pg : { &pTimbre, &pRef }) strip.addAndMakeVisible (*pg);
    content.addAndMakeVisible (meters);
    content.addAndMakeVisible (status);

    // --- TIMBRE
    pTimbre.addAndMakeVisible (nsView);
    refShort.setFont (ui::font (10.5f, true));
    refShort.setJustificationType (Justification::centredLeft);
    refShort.setInterceptsMouseClicks (false, false);
    pTimbre.addAndMakeVisible (refShort);
    for (int i = 0; i < wsk::Engine::kNumRes; ++i)
        resBox.addItem (i == 0 ? String ("LIVE  0 ms") : String (proc.bandsFor (i)) + " bands", i + 1);
    resAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "res", resBox);
    {
        String tip = "Number of bands. More bands = finer timbre detail, but more latency and CPU.\n\n"
                     "LIVE: zero latency, for singing through it on stage or while tracking.\n";
        for (int i = 1; i < wsk::Engine::kNumRes; ++i)
            tip << "\n" << proc.bandsFor (i) << " bands: latency " << proc.latencyMsFor (i) << " ms";
        resBox.setTooltip (tip);
    }
    pTimbre.addAndMakeVisible (resBox);

    range.setNormalisableRange ([] { NormalisableRange<double> r (20.0, 20000.0); r.setSkewForCentre (1000.0); return r; }());
    range.setTooltip ("Band where the timbre is transferred: drag the two ends (or the lines in the display).");
    range.onDragStart = [this]
    {
        rangeDragging = true;
        for (auto* id : { "start", "end" }) if (auto* prm = proc.apvts.getParameter (id)) prm->beginChangeGesture();
    };
    range.onValueChange = [this]
    {
        if (! rangeDragging) return;
        if (auto* a = proc.apvts.getParameter ("start")) a->setValueNotifyingHost (a->convertTo0to1 ((float) range.getMinValue()));
        if (auto* b = proc.apvts.getParameter ("end"))   b->setValueNotifyingHost (b->convertTo0to1 ((float) range.getMaxValue()));
    };
    range.onDragEnd = [this]
    {
        for (auto* id : { "start", "end" }) if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
        rangeDragging = false;
    };
    pTimbre.addAndMakeVisible (range);
    for (auto* l : { &startLbl, &endLbl })
    {
        l->setFont (ui::font (10.0f, true));
        l->setInterceptsMouseClicks (false, false);
        pTimbre.addAndMakeVisible (*l);
    }
    startLbl.setJustificationType (Justification::centredLeft);
    endLbl.setJustificationType (Justification::centredRight);

    setupKnob (slope,    pTimbre, "slope",    "SLOPE",    "Steepness of both band edges.");
    setupKnob (amount,   pTimbre, "amount",   "AMOUNT",   "How much of the reference timbre your voice takes. 100% = full transfer (boosts limited to +18 dB).");
    setupKnob (makeup,   pTimbre, "makeup",   "MAKEUP",   "Gain inside the band only.");
    setupKnob (attack,   pTimbre, "attack",   "ATTACK",   "How fast the correction goes down (cuts).");
    setupKnob (release,  pTimbre, "release",  "RELEASE",  "How fast the correction comes back up.");
    setupKnob (detail,   pTimbre, "detail",   "DETAIL",   "How detailed the timbre is: low = overall colour, high = every resonance of the reference voice.");
    setupKnob (match,    pTimbre, "match",    "MATCH",    "0% = the average timbre of the reference, always the same. 100% = for every moment, the timbre of the reference "
                                                          "singing the same thing (same vowel or consonant): the timbre changes with what you sing.");
    setupKnob (loudness, pTimbre, "loudness", "LOUDNESS", "How much intensity counts when looking for similar moments: high = your loud notes take the timbre of the reference's loud notes.");
    setupKnob (formant,  pTimbre, "formant",  "FORMANT",  "Shifts the reference timbre up or down (semitones). Use it if the reference voice is much higher or lower than yours.");
    setupKnob (smooth,   pTimbre, "smooth",   "SMOOTH",   "How soft the changes of timbre are from one moment to the next.");

    // --- REFERENCE
    pRef.addAndMakeVisible (loadBtn);
    pRef.addAndMakeVisible (clearBtn);
    loadBtn.setTooltip ("Pick the a cappella from the Finder (WAV, AIFF, FLAC, MP3, M4A, CAF).");
    clearBtn.setTooltip ("Remove the reference.");
    loadBtn.onClick = [this] { chooseReference(); };
    clearBtn.onClick = [this] { proc.clearReference(); refreshRefLabel(); };
    refName.setFont (ui::font (11.0f));
    refName.setJustificationType (Justification::centredLeft);
    pRef.addAndMakeVisible (refName);
    pRef.addAndMakeVisible (refView);
    pRef.addAndMakeVisible (nsMeter);
    setupKnob (gate, pRef, "dynamic", "GATE", "How much W-Shaper backs off when your voice is quiet: 0% = same everywhere, 100% = less and less as you get quieter, nothing in silence.");

    // --- piede
    setupKnob (mix,    content, "mix",    "MIX",    "Dry/wet of the whole plugin.");
    setupKnob (output, content, "output", "OUTPUT", "Output volume.");
    setupToggle (delta, content, "delta", "DELTA", "Listen only to the difference W-Shaper makes.", "solo");

    const char* tabNames[] = { "TIMBRE", "REFERENCE" };
    for (int i = 0; i < kPages; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (tabNames[i]);
        t.getProperties().set ("kind", "tab");
        t.onClick = [this, i] { showPage (i, true); };
        content.addAndMakeVisible (t);
    }

    presetBox.setTextWhenNothingSelected ("Preset");
    for (int i = 0; i < (int) wsp::presets().size(); ++i)
        presetBox.addItem (wsp::presets()[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0) proc.applyPreset (id - 1);
    };
    content.addAndMakeVisible (presetBox);

    for (int i = 0; i < (int) ui::skins().size(); ++i)
        skinBox.addItem (ui::skins()[(size_t) i].name, i + 1);
    skinBox.setSelectedId (look.getSkin() + 1, dontSendNotification);
    skinBox.setTooltip ("Changes the interface colours. The sound does not change.");
    skinBox.onChange = [this] { applySkin (skinBox.getSelectedId() - 1); };
    content.addAndMakeVisible (skinBox);

    setResizable (true, true);
    setResizeLimits ((int) (baseW * 0.8f), (int) (baseH * 0.8f), (int) (baseW * 2.0f), (int) (baseH * 2.0f));
    if (auto* con = getConstrainer())
        con->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);

    applySkin (look.getSkin());
    showPage ((int) proc.apvts.state.getProperty ("uiPage", 0), false);
    refreshRefLabel();
    timerCallback();
    startTimerHz (60);
}

WShaperEditor::~WShaperEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

std::vector<WShaperEditor::Knob*> WShaperEditor::allKnobs()
{
    return { &slope, &amount, &makeup, &attack, &release, &detail, &match, &loudness, &formant, &smooth, &gate, &mix, &output };
}

void WShaperEditor::applySkin (int index)
{
    look.setSkin (index);
    proc.apvts.state.setProperty ("skin", look.getSkin(), nullptr);
    const auto& p = look.pal();
    for (auto* k : allKnobs())
    {
        k->slider.setColour (Slider::textBoxTextColourId, p.strong);
        k->name.setColour (Label::textColourId, p.text.withAlpha (0.75f));
    }
    refName.setColour (Label::textColourId, p.strong);
    refShort.setColour (Label::textColourId, p.text.withAlpha (0.8f));
    startLbl.setColour (Label::textColourId, p.text.withAlpha (0.8f));
    endLbl.setColour (Label::textColourId, p.text.withAlpha (0.8f));
    sendLookAndFeelChange();
    repaint();
}

void WShaperEditor::setParam (const char* id, float value)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

void WShaperEditor::setupKnob (Knob& k, Component& parent, const String& paramId, const String& label, const String& tip, bool big)
{
    auto& s = k.slider;
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, true, 80, 16);
    s.setColour (Slider::textBoxOutlineColourId,    Colours::transparentBlack);
    s.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    s.setTooltip (tip);
    if (big) s.getProperties().set ("big", true);
    k.att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, s);
    if (auto* prm = proc.apvts.getParameter (paramId))
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    parent.addAndMakeVisible (s);
    k.name.setText (label, dontSendNotification);
    k.name.setJustificationType (Justification::centred);
    k.name.setFont (ui::font (10.0f, true));
    k.name.setInterceptsMouseClicks (false, false);
    parent.addAndMakeVisible (k.name);
}

void WShaperEditor::setupToggle (Toggle& t, Component& parent, const String& paramId, const String& text, const String& tip, const String& kind)
{
    t.button.setButtonText (text);
    t.button.setClickingTogglesState (true);
    t.button.setTooltip (tip);
    if (kind.isNotEmpty()) t.button.getProperties().set ("kind", kind);
    t.att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramId, t.button);
    parent.addAndMakeVisible (t.button);
}

void WShaperEditor::placeKnob (Knob& k, Rectangle<int> cell)
{
    k.name.setBounds (cell.removeFromTop (16));
    k.slider.setBounds (cell.reduced (4, 0));
}

void WShaperEditor::showPage (int newPage, bool animate)
{
    page = jlimit (0, kPages - 1, newPage);
    proc.apvts.state.setProperty ("uiPage", page, nullptr);
    for (int i = 0; i < kPages; ++i)
        tabs[(size_t) i].setToggleState (i == page, dontSendNotification);
    animTo = (float) (page * baseW);
    if (animate)
    {
        animFrom = stripShift;
        animStart = Time::getMillisecondCounterHiRes();
        animating = std::abs (animTo - animFrom) > 0.5f;
    }
    if (! animate || ! animating)
    {
        stripShift = animTo;
        animating = false;
    }
    strip.setTopLeftPosition (-roundToInt (stripShift), 0);
    content.tabX = 16.0f + tabW * stripShift / (float) baseW;
    content.repaint();
}

void WShaperEditor::chooseReference()
{
    chooser = std::make_unique<FileChooser> ("Choose the a cappella", File::getSpecialLocation (File::userHomeDirectory),
                                             "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile() && ! proc.loadReference (f))
                                  refName.setText ("Unreadable format", dontSendNotification);
                              refreshRefLabel();
                          });
}

void WShaperEditor::refreshRefLabel()
{
    lastRefVersion = proc.referenceVersion();
    const auto* r = proc.reference();
    if (r == nullptr)
    {
        refName.setText ("no file", dontSendNotification);
        refShort.setText ("NO REFERENCE  -  load an a cappella in REFERENCE", dontSendNotification);
        return;
    }
    const double secs = (double) r->audio.length / jmax (1.0, r->audio.sampleRate);
    refName.setText (String (r->audio.name) + "  " + String (secs, 1) + " s", dontSendNotification);
    refShort.setText ("REFERENCE:  " + String (r->audio.name), dontSendNotification);
}

void WShaperEditor::paint (Graphics& g) { g.fillAll (look.pal().band); }

void WShaperEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (AffineTransform::scale (s));

    presetBox.setBounds (172, 14, 132, 26);
    skinBox.setBounds (312, 14, 94, 26);
    for (int i = 0; i < kPages; ++i)
        tabs[(size_t) i].setBounds (16 + roundToInt ((float) i * tabW), 52, roundToInt (tabW), 32);

    clip.setBounds (0, pageY, baseW, pageH);
    strip.setBounds (-roundToInt (stripShift), 0, baseW * kPages, pageH);
    pTimbre.setBounds (0, 0, baseW, pageH);
    pRef.setBounds (baseW, 0, baseW, pageH);

    // TIMBRE
    refShort.setBounds (16, 10, 282, 26);
    resBox.setBounds (306, 10, 100, 26);
    nsView.setBounds (16, 44, 390, 146);
    range.setBounds (8, 192, 406, 24);
    startLbl.setBounds (16, 214, 160, 14);
    endLbl.setBounds (246, 214, 160, 14);
    Knob* r1[] = { &slope, &amount, &makeup, &attack, &release };
    for (int i = 0; i < 5; ++i) placeKnob (*r1[i], { 16 + i * 78, 234, 78, 84 });
    Knob* r2[] = { &detail, &match, &loudness, &formant, &smooth };
    for (int i = 0; i < 5; ++i) placeKnob (*r2[i], { 16 + i * 78, 324, 78, 84 });

    // REFERENCE
    loadBtn.setBounds (16, 10, 140, 26);
    refName.setBounds (162, 10, 210, 26);
    clearBtn.setBounds (378, 10, 28, 26);
    refView.setBounds (16, 44, 390, 200);
    placeKnob (gate, { 16, 252, 78, 84 });
    nsMeter.setBounds (102, 260, 304, 72);

    // piede
    placeKnob (mix,    { 16, 526, 78, 84 });
    placeKnob (output, { 94, 526, 78, 84 });
    delta.button.setBounds (182, 554, 108, 28);
    meters.setBounds (300, 528, 106, 82);
    status.setBounds (16, 616, 390, 18);
}

void WShaperEditor::timerCallback()
{
    if (animating)
    {
        const double t = jlimit (0.0, 1.0, (Time::getMillisecondCounterHiRes() - animStart) / pageAnimMs);
        const float e = (1.0f - std::exp2 (-7.0f * (float) t)) / (1.0f - std::exp2 (-7.0f));
        stripShift = animFrom + (animTo - animFrom) * e;
        if (t >= 1.0) { stripShift = animTo; animating = false; }
        strip.setTopLeftPosition (-roundToInt (stripShift), 0);
        content.tabX = 16.0f + tabW * stripShift / (float) baseW;
        content.repaint (0, pageY - 3, baseW, 4);
    }
    if (++tick % 4 != 0) return;

    auto val = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    if (! rangeDragging)
        range.setMinAndMaxValues (val ("start"), val ("end"), dontSendNotification);
    auto hzT = [] (float f) { return f < 1000.0f ? String (roundToInt (f)) + " Hz" : String (f / 1000.0f, f < 10000.0f ? 2 : 1) + " kHz"; };
    startLbl.setText ("START  " + hzT (val ("start")), dontSendNotification);
    endLbl.setText ("END  " + hzT (val ("end")), dontSendNotification);
    if (proc.referenceVersion() != lastRefVersion) refreshRefLabel();
}
