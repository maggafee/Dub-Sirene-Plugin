// Prüfprogramm: rendert Klang (WAV) und Oberfläche (PNG) ohne DAW.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : "/tmp");
    const double sr = 48000; const int block = 256;

    std::unique_ptr<DubProcessor> p (new DubProcessor());
    p->setPlayConfigDetails (DubProcessor::isInstrument ? 0 : 2, 2, sr, block);
    p->prepareToPlay (sr, block);

    // Federhall-IR laden lassen
    for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    const int seconds = 9;
    juce::AudioBuffer<float> all (2, (int) sr * seconds);
    juce::AudioBuffer<float> buf (2, block);
    juce::Random rnd (1);
    float peak = 0; bool nan = false;
    for (int pos = 0; pos + block <= all.getNumSamples(); pos += block)
    {
        juce::MidiBuffer midi;
        const double t = pos / sr;
        if (DubProcessor::isInstrument)
        {
            buf.clear();
            if (pos == (int) (0.2 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (pos == (int) (2.2 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            if (pos == (int) (5.0 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (pos == (int) (5.6 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            // Runaway von 6.0 bis 7.2 s, danach Tape Stop
            p->apvts.getParameter ("x_runaway")->setValueNotifyingHost (t > 6.0 && t < 7.2 ? 1.0f : 0.0f);
            p->apvts.getParameter ("x_stop")->setValueNotifyingHost (t > 7.2 ? 1.0f : 0.0f);
        }
        else
        {
            // Trommelschlag-artige Impulse in die Effekt-Version
            for (int i = 0; i < block; ++i)
            {
                const double tt = (pos + i) / sr;
                const double ph = std::fmod (tt, 1.5);
                const float v = ph < 0.08 ? (float) (std::sin (2 * 3.14159 * 180 * ph) * std::exp (-ph * 40) * 0.8) : 0.0f;
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
        }
        p->processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            all.copyFrom (ch, pos, buf, ch, 0, block);
            for (int i = 0; i < block; ++i) { const float v = buf.getSample (ch, i); if (! std::isfinite (v)) nan = true; peak = juce::jmax (peak, std::abs (v)); }
        }
    }
    std::cout << "peak " << peak << " nan " << nan << " rms " << all.getRMSLevel (0, 0, all.getNumSamples()) << std::endl;

    juce::WavAudioFormat wav;
    auto f = outDir.getChildFile (DubProcessor::isInstrument ? "siren.wav" : "echo.wav");
    f.deleteFile();
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 16, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples()); }

    // Oberfläche mit gedrückter SPEED-Taste
    if (DubProcessor::isInstrument) { p->apvts.getParameter ("press")->setValueNotifyingHost (1.0f); p->apvts.getParameter ("x_throw")->setValueNotifyingHost (1.0f); }
    p->apvts.getParameter ("t_head2")->setValueNotifyingHost (1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (ed->getWidth() * 2, ed->getHeight() * 2);
    for (int i = 0; i < 15; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    auto snap = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto pf = outDir.getChildFile (DubProcessor::isInstrument ? "siren_ui.png" : "echo_ui.png");
    pf.deleteFile();
    if (auto os = pf.createOutputStream()) png.writeImageToStream (snap, *os);
    std::cout << "ui " << ed->getWidth() << "x" << ed->getHeight() << std::endl;
    ed.reset();
    p.reset();
    return nan ? 1 : 0;
}
