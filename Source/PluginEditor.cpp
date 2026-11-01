#include "PluginEditor.h"

using juce::Graphics;
using juce::Rectangle;
using juce::Colour;

namespace ui
{
juce::Image img (const char* name)
{
    juce::String res (name);
    res = res.replaceCharacter ('.', '_').replaceCharacter ('-', '_');
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (res.toRawUTF8(), size))
        return juce::ImageCache::getFromMemory (data, size);
    jassertfalse;
    return {};
}
} // namespace ui

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
float rad (float deg) { return deg * kPi / 180.0f; }
float lerp (float a, float b, float t) { return a + (b - a) * t; }

Rectangle<float> box (float cx, float cy, float r, float ry = -1) { if (ry < 0) ry = r; return { cx - r, cy - ry, 2 * r, 2 * ry }; }

// VU-Skalen (wie im Browser)
float sirenDeg (float db)
{
    static const float D[] = { -20, -10, -5, 0, 3 }, A[] = { -30, -14, -2.3f, 11.7f, 30.6f };
    if (db <= D[0]) return lerp (-33, A[0], juce::jlimit (0.0f, 1.0f, (db + 40) / 20));
    for (int i = 1; i < 5; ++i) if (db <= D[i]) return lerp (A[i - 1], A[i], (db - D[i - 1]) / (D[i] - D[i - 1]));
    return juce::jmin (36.0f, A[4] + (db - 3) * 2);
}
float tapeDeg (float db)
{
    static const float D[] = { -20, -10, -7, -5, -3, -1, 0, 1, 3 }, A[] = { -24.9f, -18.8f, -14.9f, -10.8f, -5.5f, 0, 3.5f, 6.8f, 13.6f };
    if (db <= D[0]) return lerp (-28, A[0], juce::jlimit (0.0f, 1.0f, (db + 40) / 20));
    for (int i = 1; i < 9; ++i) if (db <= D[i]) return lerp (A[i - 1], A[i], (db - D[i - 1]) / (D[i] - D[i - 1]));
    return juce::jmin (20.0f, A[8] + (db - 3) * 2.5f);
}

void radialGlow (Graphics& g, juce::Point<float> c, float r, Colour inner, Colour mid, float alpha)
{
    if (alpha <= 0.001f) return;
    juce::ColourGradient cg (inner.withMultipliedAlpha (alpha), c, Colour (0x00000000), c.translated (r, 0), true);
    cg.addColour (0.4, mid.withMultipliedAlpha (alpha));
    g.setGradientFill (cg);
    g.fillEllipse (box (c.x, c.y, r));
}

void drawNeedle (Graphics& g, juce::Point<float> pivot, float len, float deg, Colour col, float width, Rectangle<float> clip)
{
    Graphics::ScopedSaveState s (g);
    juce::Path cp; cp.addRoundedRectangle (clip, 7);
    g.reduceClipRegion (cp);
    const float a = rad (deg);
    const juce::Point<float> tip (pivot.x + std::sin (a) * len, pivot.y - std::cos (a) * len);
    g.setColour (Colour::fromRGBA (40, 20, 0, 70));
    g.drawLine ({ pivot.translated (4, 0), tip.translated (4, 0) }, width + 1);
    g.setColour (col);
    g.drawLine ({ pivot, tip }, width);
}

const juce::Point<float> waveDots[4] = { { 278, 547 }, { 375, 640 }, { 278, 728 }, { 181, 640 } }; // sine, saw, triangle, square
const float waveAngles[4] = { 0, 90, 180, 270 };
} // namespace

//==============================================================================
DubEditor::DubEditor (DubProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setOpaque (true);
    tape = std::make_unique<ui::Panel> (ui::img ("tape_bg.jpg"));
    rack = std::make_unique<ui::Panel> (ui::img ("rack_bg.jpg"));
    addAndMakeVisible (*tape);
    addAndMakeVisible (*rack);
    buildTape();
    buildRack();

    if (DubProcessor::isInstrument)
    {
        siren = std::make_unique<ui::Panel> (ui::img ("siren_bg.jpg"));
        addAndMakeVisible (*siren);
        buildSiren();
        designW = 1536 * 2 + 40; designH = 1274;
    }
    else
    {
        designW = 1536; designH = 1274;
    }

    const float def = DubProcessor::isInstrument ? 0.42f : 0.5f;
    constrainer.setFixedAspectRatio (designW / designH);
    constrainer.setSizeLimits ((int) (designW * 0.25f), (int) (designH * 0.25f), (int) (designW * 1.0f), (int) (designH * 1.0f));
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize ((int) (designW * def), (int) (designH * def));
    setWantsKeyboardFocus (true);
    lastMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

DubEditor::~DubEditor() { stopTimer(); }

void DubEditor::paint (Graphics& g) { g.fillAll (Colour (0xff0d0a08)); }

void DubEditor::resized()
{
    const float s = (float) getWidth() / designW;
    tape->setTransform (juce::AffineTransform::scale (s));
    tape->setTopLeftPosition (0, 0);
    rack->setTransform (juce::AffineTransform::scale (s).translated (0, 1024 * s));
    if (siren)
    {
        // Sirene rechts, auf Spaltenbreite skaliert und vertikal mittig
        const float k = 1536.0f / 1448.0f;
        const float h = 902 * k;
        siren->setTransform (juce::AffineTransform::scale (s * k).translated ((1536 + 40) * s, (designH - h) * 0.5f * s));
    }
}

//==============================================================================
void DubEditor::setBool (const char* id, bool on)
{
    if (auto* r = prm (id))
    {
        if ((r->getValue() > 0.5f) == on) return;
        r->beginChangeGesture();
        r->setValueNotifyingHost (on ? 1.0f : 0.0f);
        r->endChangeGesture();
    }
}

void DubEditor::setMomentary (const char* id, bool on) { setBool (id, on); }

void DubEditor::readout (ui::Panel& pn, float yFrac)
{
    auto& l = pn.add ({ pn.getWidth() * 0.5f - 260, pn.getHeight() * yFrac, 520, 54 });
    l.state = [&pn] { return juce::Time::getMillisecondCounterHiRes() < pn.readUntil ? (float) pn.readText.hashCode() : 0.0f; };
    l.painter = [&pn, &l] (Graphics& g)
    {
        if (juce::Time::getMillisecondCounterHiRes() >= pn.readUntil) return;
        auto r = l.getBounds().toFloat();
        juce::Font f (juce::FontOptions (30.0f, juce::Font::bold));
        const float w = juce::GlyphArrangement::getStringWidth (f, pn.readText) + 44;
        auto pill = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), w), r.getHeight());
        g.setColour (Colour (0xdd0d0a08)); g.fillRoundedRectangle (pill, 12);
        g.setColour (Colour (0x88e9b218)); g.drawRoundedRectangle (pill.reduced (1), 12, 2);
        g.setColour (Colour (0xfff2d9a0)); g.setFont (f);
        g.drawText (pn.readText, pill, juce::Justification::centred);
    };
}

void DubEditor::knob (ui::Panel& pn, const char* id, juce::Image cap, float cx, float cy, float r, bool enabled)
{
    auto* param = prm (id);
    auto& l = pn.add (box (cx, cy, r));
    l.setInterceptsMouseClicks (enabled, false);
    l.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    l.setTooltip (param->getName (64));
    l.state = [param] { return param->getValue(); };
    l.painter = [param, cap, cx, cy, r, enabled] (Graphics& g)
    {
        const float a = rad (-135.0f + 270.0f * param->getValue());
        g.setOpacity (enabled ? 1.0f : 0.55f);
        g.drawImageTransformed (cap, juce::AffineTransform::scale (2 * r / cap.getWidth(), 2 * r / cap.getHeight())
                                        .translated (cx - r, cy - r).rotated (a, cx, cy));
    };
    if (! enabled) return;
    auto* pnp = &pn;
    auto start = std::make_shared<float> (0.0f);
    l.onDown = [param, start] (const juce::MouseEvent&) { *start = param->getValue(); param->beginChangeGesture(); };
    l.onDrag = [param, start, pnp] (const juce::MouseEvent& e)
    {
        const float sens = e.mods.isShiftDown() ? 1500.0f : 300.0f;
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, *start - e.getDistanceFromDragStartY() / sens));
        pnp->showReadout (param->getName (32).toUpperCase() + "  " + param->getCurrentValueAsText());
    };
    l.onUp = [param] (const juce::MouseEvent&) { param->endChangeGesture(); };
    l.onDouble = [param, pnp] (const juce::MouseEvent&)
    {
        param->beginChangeGesture(); param->setValueNotifyingHost (param->getDefaultValue()); param->endChangeGesture();
        pnp->showReadout (param->getName (32).toUpperCase() + "  " + param->getCurrentValueAsText());
    };
    l.onWheel = [param, pnp] (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, param->getValue() + (w.deltaY > 0 ? 0.02f : -0.02f)));
        param->endChangeGesture();
        pnp->showReadout (param->getName (32).toUpperCase() + "  " + param->getCurrentValueAsText());
    };
}

void DubEditor::cropKnob (ui::Panel& pn, const char* id, float cx, float cy, float r)
{
    auto cap = pn.bg.getClippedImage (box (cx, cy, r).getSmallestIntegerContainer());
    // runder Ausschnitt
    juce::Image round (juce::Image::ARGB, cap.getWidth(), cap.getHeight(), true);
    {
        Graphics g (round);
        juce::Path c; c.addEllipse (0, 0, (float) cap.getWidth(), (float) cap.getHeight());
        g.reduceClipRegion (c);
        g.drawImageAt (cap, 0, 0);
    }
    knob (pn, id, round, cx, cy, r);
}

//==============================================================================
void DubEditor::buildTape()
{
    auto& T = *tape;

    // Spulen
    struct ReelDef { float x, y, r, ry; const char* file; };
    const ReelDef reels[2] = { { 325.5f, 272.5f, 247.5f, 241.5f, "reel1.png" }, { 1207, 272.5f, 245, 241.5f, "reel2.png" } };
    for (int i = 0; i < 2; ++i)
    {
        const auto d = reels[i];
        auto im = ui::img (d.file);
        auto& l = T.add (box (d.x, d.y, d.r, d.ry).expanded (2));
        l.state = [this, i] { return reelAngle[i]; };
        l.painter = [this, i, d, im] (Graphics& g)
        {
            const float sy = d.ry / d.r;
            g.drawImageTransformed (im, juce::AffineTransform::scale (2 * d.r / im.getWidth())
                                            .translated (d.x - d.r, d.y - d.r)
                                            .rotated (rad (reelAngle[i]), d.x, d.y)
                                            .scaled (1.0f, sy, d.x, d.y));
        };
    }

    // Drei Tonköpfe
    const Rectangle<float> heads[3] = { { 605, 534, 115, 74 }, { 722, 534, 96, 74 }, { 820, 534, 115, 74 } };
    for (int i = 0; i < 3; ++i)
    {
        const char* id = i == 0 ? "t_head1" : (i == 1 ? "t_head2" : "t_head3");
        auto* param = prm (id);
        auto rr = heads[i];
        auto& l = T.add (rr);
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("Tonkopf " + juce::String (i + 1) + " an/aus");
        l.state = [param] { return param->getValue(); };
        l.painter = [param, rr] (Graphics& g)
        {
            const bool on = param->getValue() > 0.5f;
            if (! on) { g.setColour (Colour::fromRGBA (8, 6, 4, 128)); g.fillRoundedRectangle (rr, 4); }
            const auto c = rr.getCentre();
            const float r = rr.getWidth() * 0.055f;
            if (on)
            {
                radialGlow (g, c, r * 4.5f, Colour (0xccffa028), Colour (0x55ff7814), 1.0f);
                juce::ColourGradient cg (Colour (0xfffff3c0), c.translated (-r * 0.3f, -r * 0.4f), Colour (0xffe06a00), c.translated (r, r), true);
                cg.addColour (0.45, Colour (0xffffb02a));
                g.setGradientFill (cg);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (Colour (0xff5a2a0c), c.translated (-r * 0.3f, -r * 0.4f), Colour (0xff1a0a04), c.translated (r, r), true));
            }
            g.fillEllipse (box (c.x, c.y, r));
            g.setColour (Colour (0xcc1e140a)); g.drawEllipse (box (c.x, c.y, r), 1.5f);
        };
        l.onDown = [this, id, param] (const juce::MouseEvent&) { setBool (id, param->getValue() < 0.5f); };
    }

    // VU-Meter (Echo-Signal)
    {
        const Rectangle<float> clip (629, 374, 284, 114);
        auto& l = T.add (clip);
        l.state = [this] { return tvu + (val ("t_power") > 0.5f ? 0.0f : 1000.0f); };
        l.painter = [this, clip] (Graphics& g)
        {
            const bool on = val ("t_power") > 0.5f;
            drawNeedle (g, { 781, 684 }, 280, tvu, on ? Colour (0xff1b1714) : Colour (0xff3a332c), 2.6f, clip);
        };
    }

    // Drehknöpfe
    knob (T, "t_input", ui::img ("knob_b.png"), 164.5f, 781, 57);
    knob (T, "t_time", ui::img ("knob_a.png"), 380.5f, 795.5f, 57);
    knob (T, "t_fb", ui::img ("knob_b.png"), 591.5f, 789.5f, 57);
    knob (T, "t_bass", ui::img ("knob_c.png"), 772, 787.5f, 55);
    knob (T, "t_mid", ui::img ("knob_c.png"), 942.5f, 787.5f, 55);
    knob (T, "t_treble", ui::img ("knob_c.png"), 1109, 788.5f, 55);
    knob (T, "t_level", ui::img ("knob_b.png"), 1273.5f, 777.5f, 50);

    // POWER-Lampe
    {
        auto on = ui::img ("lamp_on.png"), off = ui::img ("lamp_off.png");
        auto& l = T.add (box (1404.5f, 744.5f, 31).expanded (20));
        l.state = [this] { return val ("t_power"); };
        l.painter = [this, on, off] (Graphics& g)
        {
            const bool p = val ("t_power") > 0.5f;
            g.drawImage (p ? on : off, box (1404.5f, 744.5f, 31));
            if (p) radialGlow (g, { 1404.5f, 744.5f }, 31 * 2.2f, Colour (0x8cff8c28), Colour (0x26ff6414), 1.0f);
        };
    }
    // POWER-Schalter (Ausschnitt der Grafik, an = gespiegelt)
    {
        auto lever = T.bg.getClippedImage (box (1404.5f, 843.5f, 34).getSmallestIntegerContainer());
        auto& l = T.add ({ 1364.5f, 783.5f, 80, 120 });
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("POWER");
        l.state = [this] { return val ("t_power"); };
        l.painter = [this, lever] (Graphics& g)
        {
            const bool p = val ("t_power") > 0.5f;
            Graphics::ScopedSaveState s (g);
            juce::Path c; c.addEllipse (box (1404.5f, 843.5f, 34)); g.reduceClipRegion (c);
            auto t = juce::AffineTransform::translation (1404.5f - 34, 843.5f - 34);
            if (p) t = t.scaled (1.0f, -1.0f, 1404.5f, 843.5f);
            g.drawImageTransformed (lever, t);
        };
        l.onDown = [this] (const juce::MouseEvent&) { setBool ("t_power", val ("t_power") < 0.5f); tape->showReadout (val ("t_power") > 0.5f ? "ECHO ON" : "ECHO OFF"); };
    }

    readout (T, 0.026f);
}

void DubEditor::buildRack()
{
    auto& R = *rack;
    knob (R, "x_in", ui::img ("knob_in.png"), 179.9f, 118.5f, 54, ! DubProcessor::isInstrument);
    knob (R, "x_out", ui::img ("knob_out.png"), 1355.7f, 119.1f, 59.3f);

    struct KeyDef { float x; const char* id; const char* file; const char* label; };
    const KeyDef keys[3] = { { 369.7f, "x_stop", "key_stop", "TAPE STOP" }, { 529, "x_throw", "key_throw", "DUB THROW" }, { 688.3f, "x_runaway", "key_run", "RUNAWAY" } };
    for (auto k : keys)
    {
        auto off = ui::img ((juce::String (k.file) + ".png").toRawUTF8());
        auto on = ui::img ((juce::String (k.file) + "_on.png").toRawUTF8());
        const Rectangle<float> rr (k.x - 66, 77, 132, 90);
        auto& l = R.add (rr.expanded (8));
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip (juce::String (k.label) + " (gedrückt halten)");
        auto* param = prm (k.id);
        l.state = [param] { return param->getValue(); };
        l.painter = [param, rr, on, off] (Graphics& g)
        {
            const bool down = param->getValue() > 0.5f;
            if (! down) { g.drawImage (off, rr); return; }
            auto r2 = rr.withSizeKeepingCentre (rr.getWidth() * 0.985f, rr.getHeight() * 0.985f).translated (0, rr.getHeight() * 0.03f);
            radialGlow (g, r2.getCentre(), rr.getWidth() * 0.62f, Colour (0x448cff5a), Colour (0x1a8cff5a), 1.0f);
            g.drawImage (on, r2);
        };
        const char* id = k.id;
        l.onDown = [this, id] (const juce::MouseEvent&) { setMomentary (id, true); };
        l.onUp = [this, id] (const juce::MouseEvent&) { setMomentary (id, false); };
    }

    // TAP TEMPO
    {
        auto off = ui::img ("lampbtn_off.png"), on = ui::img ("tap_on.png");
        auto& l = R.add (box (847.7f, 122, 44));
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("TAP TEMPO");
        l.state = [this]
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double beat = 60000.0 / juce::jmax (40.0, proc.tapBpm);
            const double ph = std::fmod (std::fmod (now - proc.beatZeroMs, beat) + beat, beat);
            return (ph < 110 || now - tapHitMs < 90) ? 1.0f : 0.0f;
        };
        l.painter = [&l, off, on] (Graphics& g) { g.drawImage (l.last > 0.5f ? on : off, box (847.7f, 122, 44)); };
        l.onDown = [this] (const juce::MouseEvent&)
        {
            tapHitMs = juce::Time::getMillisecondCounterHiRes();
            proc.tap();
            rack->showReadout ("TAP " + juce::String (juce::roundToInt (proc.tapBpm)) + " BPM");
            if (tape) tape->showReadout ("TAP " + juce::String (juce::roundToInt (proc.tapBpm)) + " BPM");
        };
    }
    // RECORD (im Plugin nimmt der DAW auf) – Lampe bleibt dunkel
    {
        auto off = ui::img ("rec_off.png");
        auto& l = R.add (box (1007, 122, 44));
        l.painter = [off] (Graphics& g) { g.drawImage (off, box (1007, 122, 44)); };
    }
    // MIDI IN: Buchse + LED zeigt eingehende Noten
    {
        auto jack = ui::img ("jack.png");
        auto& j = R.add (box (1166.3f, 122, 44));
        j.painter = [jack] (Graphics& g) { g.drawImage (jack, box (1166.3f, 122, 44)); };
        auto& l = R.add (box (1226, 80, 7).expanded (12));
        l.state = [this] { return (proc.midiNotesDown.load() > 0 || juce::Time::getMillisecondCounterHiRes() - midiSeenMs < 120) ? 1.0f : 0.0f; };
        l.painter = [&l] (Graphics& g)
        {
            const juce::Point<float> c (1226, 80);
            g.setGradientFill (juce::ColourGradient (Colour (0xff3b2a1e), c.translated (-2, -2), Colour (0xff120c08), c.translated (7, 7), true));
            g.fillEllipse (box (c.x, c.y, 7));
            g.setColour (Colour::fromRGBA (160, 130, 90, 140)); g.drawEllipse (box (c.x, c.y, 7), 2);
            if (l.last > 0.5f) { radialGlow (g, c, 18, Colour (0xe6ffdc3c), Colour (0x4dffbe1e), 1.0f); g.setColour (Colour (0xffffe27a)); g.fillEllipse (box (c.x, c.y, 5)); }
        };
    }
}

void DubEditor::buildSiren()
{
    auto& S = *siren;

    // Federhall-Glühen
    {
        auto& l = S.add (box (420, 182, 330, 62));
        l.state = [this] { return std::round (glow * 50.0f) / 50.0f; };
        l.painter = [this] (Graphics& g)
        {
            if (glow < 0.01f) return;
            juce::ColourGradient cg (Colour::fromFloatRGBA (1.0f, 0.59f, 0.2f, 0.55f * glow * 0.9f), { 420, 182 }, Colour (0x00000000), { 420 + 330 * 0.72f, 182 }, true);
            cg.addColour (0.45 / 0.72, Colour::fromFloatRGBA (1.0f, 0.43f, 0.08f, 0.18f * glow * 0.9f));
            g.setGradientFill (cg);
            g.addTransform (juce::AffineTransform::scale (1.0f, 62.0f / 330.0f, 420, 182));
            g.fillEllipse (box (420, 182, 330));
        };
    }

    // VU-Meter links / rechts
    {
        const Rectangle<float> cl (75, 301, 242, 115), cr (1135, 301, 241, 115);
        auto& l = S.add (cl);
        l.state = [this] { return vu[0]; };
        l.painter = [this, cl] (Graphics& g) { drawNeedle (g, { 196.5f, 529 }, 193, vu[0], Colour (0xffc4181b), 3.2f, cl); };
        auto& r = S.add (cr);
        r.state = [this] { return vu[1]; };
        r.painter = [this, cr] (Graphics& g) { drawNeedle (g, { 1258.8f, 522 }, 186, vu[1], Colour (0xffc4181b), 3.2f, cr); };
    }

    // Drehknöpfe (Ausschnitte der Grafik)
    cropKnob (S, "in_input", 852, 172, 38);
    cropKnob (S, "in_dwell", 1006, 172, 38);
    cropKnob (S, "in_tone", 1162, 172, 38);
    cropKnob (S, "in_mix", 1317, 172, 38);
    cropKnob (S, "pitch", 1032, 553, 38);
    cropKnob (S, "depth", 1212, 553, 38);
    cropKnob (S, "filter", 1032, 757, 38);
    cropKnob (S, "volume", 1212, 757, 38);

    // Wellenform-Wahlschalter
    {
        juce::Image cap = S.bg.getClippedImage (box (278, 640, 59).getSmallestIntegerContainer());
        juce::Image round (juce::Image::ARGB, cap.getWidth(), cap.getHeight(), true);
        { Graphics g (round); juce::Path c; c.addEllipse (0, 0, (float) cap.getWidth(), (float) cap.getHeight()); g.reduceClipRegion (c); g.drawImageAt (cap, 0, 0); }
        waveAngle = waveAngles[(int) val ("wave")] + 25.0f;
        auto& l = S.add ({ 130, 500, 300, 270 });
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("WAVEFORM");
        l.state = [this] { return waveAngle * 10.0f + val ("wave"); };
        l.painter = [this, round] (Graphics& g)
        {
            g.drawImageTransformed (round, juce::AffineTransform::scale (118.0f / round.getWidth()).translated (278 - 59, 640 - 59).rotated (rad (waveAngle), 278, 640));
            const auto d = waveDots[juce::jlimit (0, 3, (int) val ("wave"))];
            radialGlow (g, d, 19, Colour (0xffffe680), Colour (0x8cffbe1e), 1.0f);
            g.setColour (Colour (0xfffffbe0)); g.fillEllipse (box (d.x, d.y, 4));
        };
        l.onDown = [this, &l] (const juce::MouseEvent& e)
        {
            const auto pt = e.position + l.getPosition().toFloat();
            int pick = -1;
            if (pt.getDistanceFrom ({ 278, 640 }) < 59)
                pick = ((int) val ("wave") + 1) % 4;
            else
            {
                float best = 1e9f;
                for (int i = 0; i < 4; ++i) { const float dd = pt.getDistanceFrom (waveDots[i]); if (dd < best) { best = dd; pick = i; } }
            }
            if (auto* w = prm ("wave"))
            {
                w->beginChangeGesture(); w->setValueNotifyingHost (w->convertTo0to1 ((float) pick)); w->endChangeGesture();
                siren->showReadout ("WAVEFORM " + w->getCurrentValueAsText().toUpperCase());
            }
        };
    }

    // Tempo-Punkt auf dem Bogen
    {
        auto& l = S.add (box (722, 642, 205));
        l.state = [this] { return val ("speed") + (proc.sounding.load() ? 10.0f : 0.0f); };
        l.painter = [this] (Graphics& g)
        {
            const float a = rad (-78.0f + 156.0f * val ("speed"));
            const juce::Point<float> c (722 + std::sin (a) * 180, 642 - std::cos (a) * 180);
            radialGlow (g, c, 22, proc.sounding.load() ? Colour (0xfffff0aa) : Colour (0xe6ffdc78), Colour (0x66ffcc55), 1.0f);
            g.setColour (Colour (0xfffff6d8)); g.fillEllipse (box (c.x, c.y, 7));
            g.setColour (Colour::fromRGBA (60, 30, 0, 150)); g.drawEllipse (box (c.x, c.y, 7), 2);
        };
    }

    // SPEED-Taste: gedrückt halten = Sirene, dabei hoch/runter ziehen = Grundtempo
    {
        auto cap = S.bg.getClippedImage (box (722, 622, 88).getSmallestIntegerContainer());
        auto& l = S.add (box (722, 622, 88).expanded (40));
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("SPEED – gedrückt halten (Leertaste / MIDI-Note), dabei ziehen = Tempo");
        l.state = [this] { return (val ("press") > 0.5f || proc.midiNotesDown.load() > 0 ? 2.0f : 0.0f) + (proc.sounding.load() ? 1.0f : 0.0f); };
        l.painter = [this, cap] (Graphics& g)
        {
            const bool down = val ("press") > 0.5f || proc.midiNotesDown.load() > 0;
            const bool snd = proc.sounding.load();
            if (down)
            {
                Graphics::ScopedSaveState s (g);
                juce::Path c; c.addEllipse (box (722, 622, 88)); g.reduceClipRegion (c);
                g.fillAll (Colour (0xff0d0a08));
                g.drawImageTransformed (cap, juce::AffineTransform::translation (722 - 88, 622 - 88).scaled (0.965f, 0.965f, 722, 622));
            }
            const float a = down ? 1.0f : (snd ? 0.38f : 0.0f);
            if (a > 0)
            {
                // rotes Leuchten
                radialGlow (g, { 722, 622 }, 88 + 60, Colour (0x8cff190a), Colour (0x47ff1400), a * 0.8f);
                juce::ColourGradient cg (Colour::fromFloatRGBA (1.0f, 0.27f, 0.16f, 0.95f * a), { 722, 622 }, Colour (0x00c80000), { 722 + 88 * 0.72f, 622 }, true);
                cg.addColour (0.34 / 0.72, Colour::fromFloatRGBA (1.0f, 0.12f, 0.06f, 0.8f * a));
                cg.addColour (0.56 / 0.72, Colour::fromFloatRGBA (0.9f, 0.04f, 0.0f, 0.45f * a));
                g.setGradientFill (cg);
                g.fillEllipse (box (722, 622, 88));
            }
        };
        l.onDown = [this] (const juce::MouseEvent& e)
        {
            speedDragStart = e.position; speedAtDown = val ("speed");
            setMomentary ("press", true);
            if (auto* s = prm ("speed")) s->beginChangeGesture();
        };
        l.onDrag = [this] (const juce::MouseEvent& e)
        {
            if (auto* s = prm ("speed"))
            {
                const float v = juce::jlimit (0.0f, 1.0f, speedAtDown - (e.position.y - speedDragStart.y) / 300.0f);
                if (std::abs (e.position.y - speedDragStart.y) > 3)
                {
                    s->setValueNotifyingHost (v);
                    siren->showReadout ("SPEED " + s->getCurrentValueAsText());
                }
            }
        };
        l.onUp = [this] (const juce::MouseEvent&)
        {
            setMomentary ("press", false);
            if (auto* s = prm ("speed")) s->endChangeGesture();
        };
    }

    // ON / HOLD-Schalter
    {
        auto lever = S.bg.getClippedImage (box (1350, 700, 38).getSmallestIntegerContainer());
        auto& l = S.add ({ 1308, 598, 84, 184 });
        l.setInterceptsMouseClicks (true, false);
        l.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        l.setTooltip ("SIREN ON / HOLD");
        l.state = [this] { return val ("hold"); };
        l.painter = [this, lever] (Graphics& g)
        {
            const bool on = val ("hold") > 0.5f;
            Graphics::ScopedSaveState s (g);
            juce::Path c; c.addEllipse (box (1350, 700, 38)); g.reduceClipRegion (c);
            auto t = juce::AffineTransform::translation (1350 - 38, 700 - 38);
            if (! on) t = t.scaled (1.0f, -1.0f, 1350, 700);
            g.drawImageTransformed (lever, t);
        };
        l.onDown = [this] (const juce::MouseEvent&) { setBool ("hold", val ("hold") < 0.5f); siren->showReadout (val ("hold") > 0.5f ? "SIREN HOLD" : "SIREN OFF"); };
    }

    // Sirenen-LED
    {
        auto& l = S.add (box (1350, 533, 17).expanded (20));
        l.state = [this] { return proc.sounding.load() ? 1.0f + std::round (glow * 20) / 20 : 0.0f; };
        l.painter = [this] (Graphics& g)
        {
            const juce::Point<float> c (1350, 533);
            if (! proc.sounding.load())
            {
                g.setGradientFill (juce::ColourGradient (Colour (0xd128201a), c.translated (-2, -3), Colour (0xe60c0907), c.translated (12, 12), true));
                g.fillEllipse (box (c.x, c.y, 17));
                return;
            }
            radialGlow (g, c, 17 * 2.8f, Colour (0xbfff5a32), Colour (0x40ff2814), juce::jmin (1.0f, 0.65f + glow * 0.5f));
        };
    }

    readout (S, 0.524f);
}

//==============================================================================
void DubEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastMs) / 1000.0);
    lastMs = now;

    // Spulen: Drehzahl folgt der Bandgeschwindigkeit
    const bool power = val ("t_power") > 0.5f, braking = val ("x_stop") > 0.5f;
    const float target = (power && ! braking) ? 1.0f : 0.0f;
    const float tc = target > 0 ? 0.35f : (braking ? 0.45f : 0.8f);
    reelSpeed += (target - reelSpeed) * (1.0f - std::exp (-dt / tc));
    if (std::abs (target - reelSpeed) < 0.002f) reelSpeed = target;
    const float headTime = dub::TapeEcho::tapeMs (val ("t_time")) / 1000.0f;
    const float rps = juce::jlimit (0.25f, 2.2f, 0.32f / headTime) * reelSpeed;
    for (int i = 0; i < 2; ++i) reelAngle[i] = std::fmod (reelAngle[i] + rps * 360.0f * dt * (i ? 1.0f : 0.9f), 360.0f);

    // VU-Meter (≈ 300 ms Trägheit)
    const float k = 1.0f - std::pow (1.0f - 0.16f, dt * 60.0f);
    const float tl = proc.vuTape.load();
    tvu += (tapeDeg (tl > 0 ? 20.0f * std::log10 (tl) + 9.0f : -60.0f) - tvu) * k;
    const float lv[2] = { proc.vuL.load(), proc.vuR.load() };
    float lvlMax = 0;
    for (int i = 0; i < 2; ++i)
    {
        const float db = lv[i] > 0 ? 20.0f * std::log10 (lv[i]) + 7.0f : -60.0f;
        vu[i] += (sirenDeg (db) - vu[i]) * k;
        lvlMax = juce::jmax (lvlMax, juce::jlimit (0.0f, 1.0f, (db + 30) / 33));
    }
    glow += (lvlMax - glow) * (1.0f - std::pow (1.0f - 0.12f, dt * 60.0f));

    // Wellenform-Zeiger dreht immer den kurzen Weg
    if (siren)
    {
        float t = waveAngles[juce::jlimit (0, 3, (int) val ("wave"))] + 25.0f;
        const float d = std::fmod (std::fmod (t - waveAngle, 360.0f) + 540.0f, 360.0f) - 180.0f;
        waveAngle += d * (1.0f - std::exp (-dt / 0.06f));
        if (std::abs (d) < 0.05f) waveAngle += d;
    }
    if (proc.midiNotesDown.load() > 0) midiSeenMs = now;

    tape->tick(); rack->tick();
    if (siren) siren->tick();
}

//==============================================================================
bool DubEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (DubProcessor::isInstrument)
    {
        if (key == juce::KeyPress::spaceKey) { setMomentary ("press", true); return true; }
        if (c >= '1' && c <= '4')
        {
            const int order[4] = { 0, 1, 2, 3 }; // Sinus, Sägezahn, Dreieck, Rechteck
            if (auto* w = prm ("wave")) { w->beginChangeGesture(); w->setValueNotifyingHost (w->convertTo0to1 ((float) order[c - '1'])); w->endChangeGesture(); }
            return true;
        }
        if (c == 'h') { setBool ("hold", val ("hold") < 0.5f); return true; }
    }
    if (c == 'b') { setMomentary ("x_stop", true); return true; }
    if (c == 'e') { setMomentary ("x_throw", true); return true; }
    if (c == 'f') { setMomentary ("x_runaway", true); return true; }
    if (c == 'p') { setBool ("t_power", val ("t_power") < 0.5f); return true; }
    if (c == 't') { tapHitMs = juce::Time::getMillisecondCounterHiRes(); proc.tap(); return true; }
    return false;
}

bool DubEditor::keyStateChanged (bool)
{
    if (DubProcessor::isInstrument && val ("press") > 0.5f && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey)) setMomentary ("press", false);
    if (val ("x_stop") > 0.5f && ! juce::KeyPress::isKeyCurrentlyDown ('B') && ! juce::KeyPress::isKeyCurrentlyDown ('b')) setMomentary ("x_stop", false);
    if (val ("x_throw") > 0.5f && ! juce::KeyPress::isKeyCurrentlyDown ('E') && ! juce::KeyPress::isKeyCurrentlyDown ('e')) setMomentary ("x_throw", false);
    if (val ("x_runaway") > 0.5f && ! juce::KeyPress::isKeyCurrentlyDown ('F') && ! juce::KeyPress::isKeyCurrentlyDown ('f')) setMomentary ("x_runaway", false);
    return false;
}

juce::AudioProcessorEditor* DubProcessor::createEditor() { return new DubEditor (*this); }
