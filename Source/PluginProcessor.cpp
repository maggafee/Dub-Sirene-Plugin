#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace dub;

namespace
{
juce::NormalisableRange<float> unit() { return { 0.0f, 1.0f, 0.0f }; }

void addKnob (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, float def)
{
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, unit(), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v * 10.0f, 1); })));
}
void addBool (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DubProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // Bandecho
    addKnob (l, "t_input", "Echo Input", 0.55f);
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "t_time", 1 }, "Delay Time", unit(), TapeEcho::tapeV (400.0f),
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (TapeEcho::tapeMs (v))) + " ms"; })));
    addKnob (l, "t_fb", "Feedback", 0.45f);
    addKnob (l, "t_bass", "Bass", 0.5f);
    addKnob (l, "t_mid", "Mid", 0.5f);
    addKnob (l, "t_treble", "Treble", 0.5f);
    addKnob (l, "t_level", "Echo Level", 0.5f);
    addBool (l, "t_head1", "Head 1", true);
    addBool (l, "t_head2", "Head 2", false);
    addBool (l, "t_head3", "Head 3", true);
    addBool (l, "t_power", "Echo Power", true);
    addBool (l, "t_sync", "Sync to Host Tempo", false);

    // Effekt-Leiste
    addKnob (l, "x_in", "Input", 0.5f);
    addKnob (l, "x_out", "Output", 0.8f);
    addBool (l, "x_stop", "Tape Stop", false);
    addBool (l, "x_throw", "Dub Throw", false);
    addBool (l, "x_runaway", "Runaway", false);
    addBool (l, "protect", "Limiter", true);

   #if DUB_INSTRUMENT
    // Sirene
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "wave", 1 }, "Waveform",
        juce::StringArray { "Sine", "Saw", "Triangle", "Square" }, 2));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "speed", 1 }, "Speed", unit(), 0.45f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (SirenVoice::rateHz (v), 2) + " Hz"; })));
    addBool (l, "press", "Siren (Speed Button)", false);
    addBool (l, "hold", "Siren On / Hold", false);
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "pitch", 1 }, "Pitch", -12, 12, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v) + " st"; })));
    addKnob (l, "depth", "Mod Depth", 0.55f);
    addKnob (l, "filter", "Tone / Filter", 0.55f);
    addKnob (l, "volume", "Volume", 0.7f);
    addBool (l, "keyfollow", "Keyboard Plays Pitch", false);
    addKnob (l, "in_input", "Reverb Input", 0.714f);
    addKnob (l, "in_dwell", "Reverb Dwell", 0.6f);
    addKnob (l, "in_tone", "Reverb Tone", 0.5f);
    addKnob (l, "in_mix", "Reverb Mix", 0.35f);
   #else
    addBool (l, "dry", "Dry Signal", true);
   #endif
    return l;
}

DubProcessor::DubProcessor()
    : AudioProcessor (BusesProperties()
                     #if ! DUB_INSTRUMENT
                       .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DubSirene", createLayout())
{
    startTimerHz (20);
}

DubProcessor::~DubProcessor() { stopTimer(); }

bool DubProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
   #if ! DUB_INSTRUMENT
    if (layouts.getMainInputChannelSet() != out) return false;
   #endif
    return true;
}

void DubProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    voice.prepare (sr);
    spring.prepare (sr, maxBlock);
    echo.prepare (sr);
    protect.prepare (sr);
    if (! isInstrument) protect.setTransparent();
    extBuf.setSize (4, maxBlock);
    monoBuf.setSize (1, maxBlock);
    outGain.reset (sr, 0.03);
    outGain.setCurrentAndTargetValue (std::pow (p ("x_out") * 1.25f, 2.0f));
   #if DUB_INSTRUMENT
    spring.setDwell (p ("in_dwell"));   // erste Impulsantwort sofort
    voice.rate.jump ((float) SirenVoice::rateHz (p ("speed")));
   #endif
    lastSpeedTarget = -1;
}

void DubProcessor::timerCallback()
{
   #if DUB_INSTRUMENT
    // Federhall-Länge: Impulsantwort außerhalb des Audio-Threads neu berechnen (wie im Browser, verzögert)
    static int settle = 0;
    const float d = p ("in_dwell");
    if (std::abs (d - dwellPending.load()) > 0.004f) { dwellPending = d; settle = 4; }
    else if (settle > 0 && --settle == 0) spring.setDwell (d);
   #endif

    // Tempo-Sync: DELAY TIME = Achtelnote im Host-Tempo
    const double bpm = hostBpm.load();
    if (p ("t_sync") > 0.5f && bpm > 20)
    {
        double ms = 30000.0 / bpm;
        while (ms > 600) ms /= 2;
        while (ms < 50) ms *= 2;
        const float v = TapeEcho::tapeV ((float) ms);
        if (auto* prm = apvts.getParameter ("t_time"); prm != nullptr && std::abs (prm->getValue() - v) > 0.002f)
            prm->setValueNotifyingHost (v);
        tapBpm = bpm;
    }
}

void DubProcessor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (taps.empty() || now - taps.back() > 2000) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    beatZeroMs = now;
    if (taps.size() < 2) return;
    double sum = 0;
    for (size_t i = 1; i < taps.size(); ++i) sum += taps[i] - taps[i - 1];
    const double beat = sum / (double) (taps.size() - 1);
    tapBpm = (double) juce::roundToInt (juce::jlimit (40.0, 200.0, 60000.0 / beat));
    double ms = 30000.0 / tapBpm;
    while (ms > 600) ms /= 2;
    while (ms < 50) ms *= 2;
    if (auto* prm = apvts.getParameter ("t_time"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (TapeEcho::tapeV ((float) ms));
        prm->endChangeGesture();
    }
}

void DubProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) hostBpm = *b;

    // ---- Parameter übernehmen ----
    echo.setInput (p ("t_input"));
    echo.setTime (p ("t_time"));
    echo.setFeedback (p ("t_fb"));
    echo.setBass (p ("t_bass")); echo.setMid (p ("t_mid")); echo.setTreble (p ("t_treble"));
    echo.setLevel (p ("t_level"));
    echo.setHead (0, p ("t_head1") > 0.5f); echo.setHead (1, p ("t_head2") > 0.5f); echo.setHead (2, p ("t_head3") > 0.5f);
    echo.setPower (p ("t_power") > 0.5f);
    echo.setBrake (p ("x_stop") > 0.5f);
    echo.setThrow (p ("x_throw") > 0.5f);
    echo.setRunaway (p ("x_runaway") > 0.5f);
    echo.setExt (p ("x_in"));
    outGain.setTargetValue (std::pow (p ("x_out") * 1.25f, 2.0f));

   #if DUB_INSTRUMENT
    voice.depth.set (p ("depth") * 1250.0f, 0.03f);
    voice.filterHz.set (220.0f * std::pow (2.0f, p ("filter") * 6.2f), 0.03f);
    voice.volume.set (std::pow (p ("volume"), 2.0f) * 1.1f, 0.03f);
    spring.setInput (p ("in_input"));
    spring.setTone (p ("in_tone"));
    spring.setMix (p ("in_mix"));
    const auto wave = (Wave) juce::jlimit (0, 3, (int) p ("wave"));
   #endif

    // ---- MIDI: Note = SPEED drücken, Sustain-Pedal = HOLD ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) { ++heldNotes; lastNote = m.getNoteNumber(); }
        else if (m.isNoteOff()) heldNotes = juce::jmax (0, heldNotes - 1);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldNotes = 0;
        else if (m.isSustainPedalOn()) sustain = true;
        else if (m.isSustainPedalOff()) sustain = false;
    }
    midiNotesDown = heldNotes;

   #if DUB_INSTRUMENT
    const bool pressed = p ("press") > 0.5f || heldNotes > 0;
    const bool on = pressed || p ("hold") > 0.5f || sustain;
    voice.gate.set (on ? 1.0f : 0.0f, on ? 0.006f : 0.045f);
    sounding = on;

    // Tonhöhe (optional vom Keyboard)
    int semi = (int) p ("pitch");
    if (p ("keyfollow") > 0.5f) semi = juce::jlimit (-24, 24, semi + (lastNote - 60));
    voice.hz.set ((float) (SirenVoice::baseHz * std::pow (2.0, semi / 12.0)), 0.03f);

    // SPEED: gedrückt halten = Sirene läuft hoch
    const float base = (float) SirenVoice::rateHz (p ("speed"));
    const float speedTarget = pressed ? juce::jmin (40.0f, base * 3.2f) : base;
    if (pressed != wasPressed || std::abs (speedTarget - lastSpeedTarget) > 1e-4f)
    {
        voice.rate.set (speedTarget, pressed ? 0.9f : 0.18f);
        wasPressed = pressed; lastSpeedTarget = speedTarget;
    }
   #endif

    // ---- Audio in Häppchen ≤ maxBlock ----
    double sqL = 0, sqR = 0;
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        float* L = buffer.getWritePointer (0, start);
        float* R = chans > 1 ? buffer.getWritePointer (1, start) : nullptr;

       #if DUB_INSTRUMENT
        float* mono = monoBuf.getWritePointer (0);
        for (int i = 0; i < n; ++i) mono[i] = voice.next (wave, i);
        // Sirene (mono) + Federhall (stereo)
        auto* eL = extBuf.getWritePointer (0);
        auto* eR = extBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i) { eL[i] = mono[i]; eR[i] = mono[i]; }
        spring.process (mono, eL, eR, n);
        echo.mainToTape = true;
        echo.process (eL, eR, nullptr, nullptr, n, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            eL[i] *= g; eR[i] *= g;
        }
       #else
        // Effekt: Spur → (INPUT) → Bandecho; trocken optional
        auto* eL = extBuf.getWritePointer (0);
        auto* eR = extBuf.getWritePointer (1);
        auto* xL = extBuf.getWritePointer (2);
        auto* xR = extBuf.getWritePointer (3);
        juce::FloatVectorOperations::copy (eL, L, n);
        juce::FloatVectorOperations::copy (eR, R != nullptr ? R : L, n);
        juce::FloatVectorOperations::copy (xL, eL, n);
        juce::FloatVectorOperations::copy (xR, eR, n);
        echo.mainToTape = false;   // die Spur geht nur über INPUT ins Echo
        echo.process (eL, eR, xL, xR, n, p ("dry") > 0.5f ? 1.0f : 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            eL[i] *= g; eR[i] *= g;
        }
       #endif

        if (p ("protect") > 0.5f) protect.process (eL, eR, n);

        for (int i = 0; i < n; ++i) { sqL += eL[i] * eL[i]; sqR += eR[i] * eR[i]; }
        if (R != nullptr)
        {
            juce::FloatVectorOperations::copy (L, eL, n);
            juce::FloatVectorOperations::copy (R, eR, n);
        }
        else
        {
            for (int i = 0; i < n; ++i) L[i] = 0.5f * (eL[i] + eR[i]);
        }
    }
    for (int ch = 2; ch < chans; ++ch) buffer.clear (ch, 0, total);

    const float k = 0.25f;
    vuL = vuL.load() + ((float) std::sqrt (sqL / juce::jmax (1, total)) - vuL.load()) * k;
    vuR = vuR.load() + ((float) std::sqrt (sqR / juce::jmax (1, total)) - vuR.load()) * k;
    vuTape = echo.level;
}

void DubProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    // Momentane Taster nicht speichern
    for (auto id : { "press", "x_stop", "x_throw", "x_runaway" })
        if (state.getChildWithProperty ("id", id).isValid())
            state.getChildWithProperty ("id", id).setProperty ("value", 0.0f, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void DubProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DubProcessor(); }
