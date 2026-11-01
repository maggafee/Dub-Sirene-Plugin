#pragma once
#include <JuceHeader.h>
#include "DspCore.h"

#ifndef DUB_INSTRUMENT
 #define DUB_INSTRUMENT 1
#endif

/** Gemeinsamer Prozessor: DUB_INSTRUMENT=1 → „Dub Sirene“ (Instrument mit Federhall und Bandecho),
    DUB_INSTRUMENT=0 → „Dubasco Tape Echo“ (Effekt für jede Spur). */
class DubProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DubProcessor();
    ~DubProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return DUB_INSTRUMENT != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Anzeige (vom Audio-Thread geschrieben, von der Oberfläche gelesen)
    std::atomic<float> vuL { 0 }, vuR { 0 }, vuTape { 0 };
    std::atomic<bool> sounding { false };
    std::atomic<double> hostBpm { 0 };
    std::atomic<int> midiNotesDown { 0 };

    /** TAP TEMPO (Oberfläche) */
    void tap();
    double tapBpm = 75;
    double beatZeroMs = 0;

    static constexpr bool isInstrument = DUB_INSTRUMENT != 0;

private:
    void timerCallback() override;
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    dub::SirenVoice voice;
    dub::SpringUnit spring;
    dub::TapeEcho echo;
    dub::Protector protect;
    juce::AudioBuffer<float> extBuf, monoBuf;
    int maxBlock = 512;
    double sr = 44100;

    std::atomic<float> dwellPending { -1 };
    int heldNotes = 0;
    int lastNote = 60;
    bool sustain = false;
    bool wasPressed = false;
    float lastSpeedTarget = -1;
    std::vector<double> taps;

    juce::LinearSmoothedValue<float> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DubProcessor)
};
