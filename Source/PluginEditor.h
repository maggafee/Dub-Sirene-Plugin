#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace ui
{
/** Bild aus den eingebetteten Grafiken */
juce::Image img (const char* name);

/** Fläche mit frei gezeichnetem Inhalt in Panel-Koordinaten (Pixel der Originalgrafik). */
class Live : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (juce::Graphics&)> painter;
    std::function<float()> state;      // ändert sich der Wert, wird neu gezeichnet
    float last = -1.0e9f;
    void tick()
    {
        if (! state) return;
        const float s = state();
        if (std::abs (s - last) > 1.0e-4f) { last = s; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        if (! painter) return;
        g.addTransform (juce::AffineTransform::translation ((float) -getX(), (float) -getY()));
        painter (g);
    }
    std::function<void (const juce::MouseEvent&)> onDown, onUp, onDrag, onDouble;
    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;
    void mouseDown (const juce::MouseEvent& e) override { if (onDown) onDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (onUp) onUp (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (onDouble) onDouble (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (onWheel) onWheel (e, w); }
};

/** Ein Teil des Geräts (Bandecho, Leiste, Sirene) in Originalgröße; wird von außen skaliert. */
class Panel : public juce::Component
{
public:
    Panel (juce::Image background) : bg (std::move (background)) { setSize (bg.getWidth(), bg.getHeight()); setOpaque (true); }
    void paint (juce::Graphics& g) override { g.drawImageAt (bg, 0, 0); }

    Live& add (juce::Rectangle<float> r)
    {
        auto* l = items.add (new Live());
        l->setBounds (r.getSmallestIntegerContainer());
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
        return *l;
    }
    void tick() { for (auto* l : items) l->tick(); }

    juce::Image bg;
    juce::OwnedArray<Live> items;
    juce::OwnedArray<juce::ParameterAttachment> attachments;

    // kurze Wertanzeige oben im Panel
    juce::String readText;
    double readUntil = 0;
    void showReadout (const juce::String& t) { readText = t; readUntil = juce::Time::getMillisecondCounterHiRes() + 1100; }
};
} // namespace ui

class DubEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DubEditor (DubProcessor&);
    ~DubEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void buildTape();
    void buildRack();
    void buildSiren();

    // Bausteine
    void knob (ui::Panel&, const char* id, juce::Image cap, float cx, float cy, float r, bool enabled = true);
    void cropKnob (ui::Panel&, const char* id, float cx, float cy, float r);
    void readout (ui::Panel&, float yFrac);
    juce::RangedAudioParameter* prm (const char* id) { return proc.apvts.getParameter (id); }
    float val (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); }
    void setBool (const char* id, bool on);
    void setMomentary (const char* id, bool on);

    DubProcessor& proc;
    std::unique_ptr<ui::Panel> tape, rack, siren;
    juce::TooltipWindow tooltips { this, 600 };
    juce::ComponentBoundsConstrainer constrainer;
    float designW = 0, designH = 0;

    // Anzeige-Zustand
    double lastMs = 0;
    float reelSpeed = 0, reelAngle[2] = { 0, 0 };
    float tvu = -28, vu[2] = { -33, -33 }, glow = 0;
    float waveAngle = 0;
    double tapHitMs = 0, midiSeenMs = 0;
    bool speedDown = false;
    juce::Point<float> speedDragStart;
    float speedAtDown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubEditor)
};
