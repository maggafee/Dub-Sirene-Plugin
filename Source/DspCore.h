#pragma once
// Klangerzeugung der Dub Sirene – 1:1-Port der Web-Audio-Kette aus dem WordPress-Plugin.
// Alles läuft pro Sample, ohne Speicheranforderungen im Audio-Thread.

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace dub
{
constexpr double kPi = 3.14159265358979323846;

inline float clampf (float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

/** Wie AudioParam.setTargetAtTime: läuft exponentiell mit Zeitkonstante tau auf das Ziel zu. */
struct Glide
{
    float value = 0, target = 0, coeff = 1;
    double sr = 44100;
    void prepare (double sampleRate, float v) { sr = sampleRate; value = target = v; }
    void set (float t, float tauSeconds)
    {
        target = t;
        coeff = tauSeconds <= 0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (tauSeconds * sr)));
    }
    void jump (float v) { value = target = v; }
    inline float next() { value += (target - value) * coeff; return value; }
};

/** Biquad nach den Formeln der Web-Audio-Spezifikation (damit es genauso klingt wie im Browser). */
struct Biquad
{
    enum Type { lowpass, highpass, lowshelf, highshelf, peaking };
    Type type = lowpass;
    double sr = 44100;
    float freq = 1000, q = 1, gainDb = 0;
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void prepare (double sampleRate, Type t, float f, float qq = 1.0f, float g = 0.0f)
    {
        sr = sampleRate; type = t; freq = f; q = qq; gainDb = g; reset(); update();
    }
    void reset() { z1 = z2 = 0; }
    void setFreq (float f) { if (f != freq) { freq = f; update(); } }
    void setGain (float g) { if (g != gainDb) { gainDb = g; update(); } }

    void update()
    {
        const double nyq = sr * 0.5;
        const double f = juce::jlimit (10.0, nyq * 0.98, (double) freq);
        const double w0 = 2.0 * kPi * f / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0, B1, B2, A0, A1, A2;
        switch (type)
        {
            case lowpass:
            case highpass:
            {
                // Web Audio: Q dieser Filter ist in dB angegeben
                const double alpha = sw / (2.0 * std::pow (10.0, q / 20.0));
                if (type == lowpass) { B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; }
                else                 { B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; }
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha;
                break;
            }
            case peaking:
            {
                const double alpha = sw / (2.0 * juce::jmax (0.0001, (double) q));
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
                break;
            }
            case lowshelf:
            case highshelf:
            default:
            {
                const double alpha = sw / 2.0 * std::sqrt (2.0); // S = 1
                const double k = 2.0 * std::sqrt (A) * alpha;
                if (type == lowshelf)
                {
                    B0 = A * ((A + 1) - (A - 1) * cw + k);
                    B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                    B2 = A * ((A + 1) - (A - 1) * cw - k);
                    A0 = (A + 1) + (A - 1) * cw + k;
                    A1 = -2 * ((A - 1) + (A + 1) * cw);
                    A2 = (A + 1) + (A - 1) * cw - k;
                }
                else
                {
                    B0 = A * ((A + 1) + (A - 1) * cw + k);
                    B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                    B2 = A * ((A + 1) + (A - 1) * cw - k);
                    A0 = (A + 1) - (A - 1) * cw + k;
                    A1 = 2 * ((A - 1) - (A + 1) * cw);
                    A2 = (A + 1) - (A - 1) * cw - k;
                }
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }

    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

/** WaveShaper mit Kurve tanh(k·x)/tanh(k), Eingang wie im Browser auf ±1 begrenzt. */
inline float shape (float x, float k)
{
    x = clampf (x, -1.0f, 1.0f);
    return std::tanh (k * x) / std::tanh (k);
}

inline double polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

enum class Wave { sine = 0, sawtooth = 1, triangle = 2, square = 3 };

/** LFO mit den Kurvenformen der Web-Audio-Oszillatoren (Phase 0 = Wellenanfang). */
struct Lfo
{
    double phase = 0;
    float smooth = 0;
    inline float next (Wave w, double hz, double sr)
    {
        const double p = phase;
        phase += hz / sr;
        if (phase >= 1.0) phase -= std::floor (phase);
        float v;
        switch (w)
        {
            case Wave::sine:     v = (float) std::sin (2.0 * kPi * p); break;
            case Wave::sawtooth: v = (float) (2.0 * (p - std::floor (p + 0.5))); break;
            case Wave::triangle: v = (float) (p < 0.25 ? 4 * p : (p < 0.75 ? 2 - 4 * p : 4 * p - 4)); break;
            case Wave::square:
            default:             v = p < 0.5 ? 1.0f : -1.0f; break;
        }
        // ganz leicht glätten (≈ 1 ms) – der Browser-Oszillator ist bandbegrenzt
        smooth += (v - smooth) * (float) (1.0 - std::exp (-1.0 / (0.0008 * sr)));
        return smooth;
    }
};

/** Mono-Verzögerungsleitung mit kubischer Interpolation. */
struct DelayLine
{
    std::vector<float> buf;
    int w = 0, mask = 0;
    void prepare (double sr, double maxSeconds)
    {
        int n = 1;
        while (n < (int) (sr * maxSeconds) + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    /** d = Verzögerung in Samples (≥ 1). */
    inline float read (double d) const
    {
        d = juce::jlimit (1.0, (double) mask - 4, d);
        const double rp = (double) w - d;
        int i = (int) std::floor (rp);
        const float f = (float) (rp - i);
        auto at = [this] (int k) { return buf[(size_t) (k & mask)]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c0 = y1, c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
};

/** Federhall-Impulsantwort – identisch zur Browser-Version (gleicher Zufallsgenerator). */
inline juce::AudioBuffer<float> springIR (double sr, double seconds, int seed)
{
    const int len = juce::jmax (1, (int) std::floor (sr * seconds));
    juce::AudioBuffer<float> b (2, len);
    const double k = 6.9 / seconds;
    double rnd = seed ? seed : 1;
    auto rand = [&rnd]() { rnd = std::fmod (rnd * 16807.0, 2147483647.0); return rnd / 2147483647.0 * 2.0 - 1.0; };
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = b.getWritePointer (ch);
        double lp = 0, lp2 = 0;
        const int echo = (int) std::floor (sr * (0.029 + ch * 0.004));
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const double env = std::exp (-k * t);
            const double flutter = 1 + 0.45 * std::sin (2 * kPi * (26 + ch * 3) * t + ch);
            double x = rand() * env * flutter;
            if (i >= echo) x += d[i - echo] * 0.38;
            const double damp = juce::jlimit (0.08, 0.6, 0.55 - t / seconds * 0.4);
            lp += (x - lp) * damp;
            lp2 += (lp - lp2) * 0.9;
            d[i] = (float) (lp2 - lp * 0.12);
        }
        const int fade = juce::jmin (len, (int) (sr * 0.004));
        for (int j = 0; j < fade; ++j) d[j] *= (float) j / (float) (sr * 0.004);
    }
    // Normalisierung wie der ConvolverNode des Browsers
    double power = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < len; ++i) power += (double) b.getSample (ch, i) * b.getSample (ch, i);
    power = std::sqrt (power / (2.0 * len));
    if (! std::isfinite (power) || power < 0.000125) power = 0.000125;
    const double scale = (1.0 / power) * std::pow (10.0, -58.0 / 20.0) * (44100.0 / sr);
    b.applyGain ((float) scale);
    return b;
}

//==============================================================================
/** Die Sirene: Rechteck + Sub-Dreieck, per LFO in der Tonhöhe moduliert, Hochpass, Tiefpass. */
struct SirenVoice
{
    static constexpr double baseHz = 520.0;
    double sr = 44100;
    double phase = 0, subPhase = 0;
    Lfo lfo;
    Glide gate, rate, hz, depth, filterHz, volume;
    Biquad hp, lp;

    void prepare (double s)
    {
        sr = s;
        gate.prepare (sr, 0); rate.prepare (sr, 1); hz.prepare (sr, (float) baseHz);
        depth.prepare (sr, 0); filterHz.prepare (sr, 2000); volume.prepare (sr, 0.5f);
        hp.prepare (sr, Biquad::highpass, 140, 1);
        lp.prepare (sr, Biquad::lowpass, 2000, 3.2f);
        phase = subPhase = 0; lfo = {};
    }

    static double rateHz (float speed) { return 0.22 * std::pow (48.0, (double) speed); }

    /** Ein Sample (mono). wave = LFO-Form. */
    inline float next (Wave wave, int sampleIndex)
    {
        const float g = gate.next();
        const double r = rate.next();
        const float cents = lfo.next (wave, r, sr) * depth.next();
        const double f = hz.next() * std::pow (2.0, cents / 1200.0);
        if ((sampleIndex & 31) == 0) lp.setFreq (filterHz.next()); else filterHz.next();

        // Rechteck (PolyBLEP, bandbegrenzt)
        const double dt = f / sr;
        double sq = phase < 0.5 ? 1.0 : -1.0;
        sq += polyBlep (phase, dt);
        double t2 = phase + 0.5; if (t2 >= 1) t2 -= 1;
        sq -= polyBlep (t2, dt);
        phase += dt; if (phase >= 1) phase -= 1;
        // Sub-Dreieck eine Oktave tiefer
        const double sp = subPhase;
        const double tri = sp < 0.25 ? 4 * sp : (sp < 0.75 ? 2 - 4 * sp : 4 * sp - 4);
        subPhase += dt * 0.5; if (subPhase >= 1) subPhase -= 1;

        float x = (float) (sq + tri * 0.55) * g;
        x = lp.process (hp.process (x));
        return x * 0.32f * volume.next();
    }
};

//==============================================================================
/** Federhall-Einheit als Send: Input-Sättigung → Faltung → Hochpass → Tone → Mix. */
struct SpringUnit
{
    juce::dsp::Convolution conv { juce::dsp::Convolution::NonUniform { 512 } };
    Biquad hpL, hpR, toneL, toneR;
    Glide send, wet;
    float driveK = 1;
    double sr = 44100;
    float lastDwell = -1;
    juce::AudioBuffer<float> work;

    void prepare (double s, int maxBlock)
    {
        sr = s;
        conv.prepare ({ sr, (juce::uint32) maxBlock, 2 });
        hpL.prepare (sr, Biquad::highpass, 180, 1); hpR.prepare (sr, Biquad::highpass, 180, 1);
        toneL.prepare (sr, Biquad::lowpass, 3000, 1); toneR.prepare (sr, Biquad::lowpass, 3000, 1);
        send.prepare (sr, 0.3f); wet.prepare (sr, 0.5f);
        work.setSize (2, maxBlock);
        lastDwell = -1;
    }

    void setDwell (float dwell)
    {
        if (std::abs (dwell - lastDwell) < 0.004f) return;
        lastDwell = dwell;
        conv.loadImpulseResponse (springIR (sr, 0.8 + dwell * 3.6, 11), sr,
                                  juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                  juce::dsp::Convolution::Normalise::no);
    }

    void setInput (float v)
    {
        const float gain = v < 0.01f ? 0.0f : std::pow (10.0f, (-30.0f + 42.0f * v) / 20.0f);
        send.set (juce::jmin (gain, 1.0f), 0.03f);
        driveK = 1.0f + juce::jmax (0.0f, gain - 1.0f) * 2.2f;
    }
    void setTone (float v) { const float f = 1200.0f * std::pow (2.0f, v * 3.3f); toneL.setFreq (f); toneR.setFreq (f); }
    void setMix (float v) { wet.set (v * 1.5f, 0.03f); }

    /** in: Mono-Signal der Sirene; addiert den Hall (stereo) auf outL/outR. */
    void process (const float* in, float* outL, float* outR, int n)
    {
        work.setSize (2, n, false, false, true);
        auto* a = work.getWritePointer (0);
        auto* b = work.getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float x = shape (in[i] * send.next(), driveK); a[i] = x; b[i] = x; }
        juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.next();
            outL[i] += toneL.process (hpL.process (a[i])) * w;
            outR[i] += toneR.process (hpR.process (b[i])) * w;
        }
    }
};

//==============================================================================
/** Bandecho: 70er/80er-Schule (King Tubby, Space Echo). Drei Wiedergabeköpfe bei 1×, 2×, 3× DELAY TIME. */
struct TapeEcho
{
    static constexpr int kHeads = 3;
    double sr = 44100;
    DelayLine tape;
    Glide inGain, throwG, powerIn, powerOut, fb, runG, wetG, stopG, extG;
    std::array<Glide, kHeads> headG, headT;   // headT = Laufzeit in Sekunden (ohne Modulation)
    float preK = 1;
    Biquad recHp, recLp;
    Biquad lb, lm, lt, loopLp, loopHp;
    Biquad ebL, emL, etL, ageL, ebR, emR, etR, ageR;
    double wowPh = 0, flutPh = 0, driftPh = 0;
    float wowAmt = 0, flutAmt = 0, driftAmt = 0;
    float fbSumPrev = 0;
    float level = 0;  // RMS des Echos für das VU-Meter

    // Zustand
    float baseTime = 0.4f;
    bool heads[kHeads] = { true, false, true };
    bool braking = false, running = false, throwing = false, power = true;
    float fbKnob = 0.45f;
    bool mainToTape = true;   // Hauptsignal (Sirene) aufs Band

    void prepare (double s)
    {
        sr = s;
        tape.prepare (sr, 6.2);
        inGain.prepare (sr, 0.8f); throwG.prepare (sr, 1); powerIn.prepare (sr, 1); powerOut.prepare (sr, 1);
        fb.prepare (sr, 0); runG.prepare (sr, 1); wetG.prepare (sr, 0.8f); stopG.prepare (sr, 1); extG.prepare (sr, 1);
        for (int i = 0; i < kHeads; ++i) { headG[(size_t) i].prepare (sr, heads[i] ? 1.0f : 0.0f); headT[(size_t) i].prepare (sr, baseTime * (i + 1)); }
        recHp.prepare (sr, Biquad::highpass, 55, 0.6f); recLp.prepare (sr, Biquad::lowpass, 7200, 0.6f);
        lb.prepare (sr, Biquad::lowshelf, 160); lm.prepare (sr, Biquad::peaking, 900, 0.8f); lt.prepare (sr, Biquad::highshelf, 3200);
        loopLp.prepare (sr, Biquad::lowpass, 3900, 0.5f); loopHp.prepare (sr, Biquad::highpass, 120, 0.5f);
        for (auto* b : { &ebL, &ebR }) b->prepare (sr, Biquad::lowshelf, 120);
        for (auto* b : { &emL, &emR }) b->prepare (sr, Biquad::peaking, 1000, 0.7f);
        for (auto* b : { &etL, &etR }) b->prepare (sr, Biquad::highshelf, 3500);
        for (auto* b : { &ageL, &ageR }) b->prepare (sr, Biquad::lowpass, 6000, 0.4f);
        fbSumPrev = 0; level = 0;
        applyTimes (true); applyHeads (true);
    }

    static float tapeMs (float v) { return 50.0f * std::pow (12.0f, v); }
    static float tapeV (float ms) { return std::log (ms / 50.0f) / std::log (12.0f); }
    static float eqDb (float v) { return (v * 2 - 1) * 12; }

    void setInput (float v) { inGain.set (std::pow (v * 1.6f, 2.0f), 0.03f); preK = 1 + juce::jmax (0.0f, v - 0.6f) * 9; }
    void setTime (float v) { const float t = tapeMs (v) / 1000.0f; if (t != baseTime) { baseTime = t; applyTimes (false); } }
    void setFeedback (float v) { fbKnob = v; applyHeads (false); }
    void setBass (float v)   { const float d = eqDb (v); ebL.setGain (d); ebR.setGain (d); lb.setGain (d * 0.4f); }
    void setMid (float v)    { const float d = eqDb (v); emL.setGain (d); emR.setGain (d); lm.setGain (d * 0.4f); }
    void setTreble (float v) { const float d = eqDb (v); etL.setGain (d); etR.setGain (d); lt.setGain (d * 0.4f); }
    void setLevel (float v)  { wetG.set (v * 1.6f, 0.03f); }
    void setExt (float v)    { extG.set (std::pow (v * 2.0f, 2.0f), 0.03f); }
    void setHead (int i, bool on) { if (heads[i] != on) { heads[i] = on; applyHeads (false); } }
    void setPower (bool on)
    {
        if (power == on) return;
        power = on;
        powerIn.set (on ? 1.0f : 0.0f, 0.03f); powerOut.set (on ? 1.0f : 0.0f, 0.03f);
    }
    void setBrake (bool on)
    {
        if (braking == on) return;
        braking = on;
        stopG.set (on ? 0.0f : 1.0f, on ? 0.7f : 0.05f);
        applyTimes (false);
    }
    void setThrow (bool on) { if (throwing != on) { throwing = on; throwG.set (on ? 2.5f : 1.0f, 0.03f); } }
    void setRunaway (bool on)
    {
        if (running == on) return;
        running = on;
        const float f = juce::jmax (0.05f, fb.value);
        runG.set (on ? juce::jmax (1.25f, 1.18f / f) : 1.0f, 0.03f);
    }

    void applyTimes (bool fast)
    {
        const float slow = braking ? 0.06f : 1.0f;
        for (int i = 0; i < kHeads; ++i)
        {
            const float target = juce::jmin (5.6f, baseTime * (float) (i + 1) / slow);
            if (fast) headT[(size_t) i].jump (target); else headT[(size_t) i].set (target, braking ? 0.45f : 0.14f);
        }
        wowAmt = baseTime * 0.0028f; flutAmt = baseTime * 0.00028f; driftAmt = baseTime * 0.004f;
    }

    void applyHeads (bool fast)
    {
        int n = 0;
        for (int i = 0; i < kHeads; ++i)
        {
            if (fast) headG[(size_t) i].jump (heads[i] ? 1.0f : 0.0f); else headG[(size_t) i].set (heads[i] ? 1.0f : 0.0f, 0.03f);
            if (heads[i]) ++n;
        }
        const float f = fbKnob * 1.02f / std::sqrt ((float) juce::jmax (1, n));
        if (fast) fb.jump (f); else fb.set (f, 0.03f);
    }

    /** Stereo in/out. ext = zusätzliches Signal nur ins Echo (MIC/LINE bzw. Effekt-Eingang), darf nullptr sein.
        dryGain = Anteil des Eingangs, der trocken durchgeht. */
    void process (float* L, float* R, const float* extL, const float* extR, int n, float dryGain)
    {
        static const float panL[kHeads] = { (float) std::cos ((-0.35 + 1) * kPi / 4), (float) std::cos (kPi / 4), (float) std::cos ((0.35 + 1) * kPi / 4) };
        static const float panR[kHeads] = { (float) std::sin ((-0.35 + 1) * kPi / 4), (float) std::sin (kPi / 4), (float) std::sin ((0.35 + 1) * kPi / 4) };
        const double wowInc = 0.55 / sr, flutInc = 6.8 / sr, driftInc = 0.13 / sr;
        double sumSq = 0;
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            float mono = mainToTape ? 0.5f * (inL + inR) : 0.0f;
            if (extL != nullptr) mono += 0.5f * (extL[i] + (extR != nullptr ? extR[i] : extL[i])) * extG.next();

            // Vorstufe: INPUT → Dub Throw → Sättigung → POWER
            float x = shape (mono * inGain.next() * throwG.next(), preK) * powerIn.next();

            // Wow / Flutter / Drift (Sekunden)
            wowPh += wowInc; if (wowPh >= 1) wowPh -= 1;
            flutPh += flutInc; if (flutPh >= 1) flutPh -= 1;
            driftPh += driftInc; if (driftPh >= 1) driftPh -= 1;
            const double tri = driftPh < 0.25 ? 4 * driftPh : (driftPh < 0.75 ? 2 - 4 * driftPh : 4 * driftPh - 4);
            const double mod = std::sin (2 * kPi * wowPh) * wowAmt + std::sin (2 * kPi * flutPh) * flutAmt + tri * driftAmt;

            // Köpfe lesen
            float fbSum = 0, wl = 0, wr = 0;
            for (int h = 0; h < kHeads; ++h)
            {
                const double secs = headT[(size_t) h].next() + mod * (h + 1);
                const float y = tape.read (secs * sr) * headG[(size_t) h].next();
                fbSum += y; wl += y * panL[h]; wr += y * panR[h];
            }

            // Rückkopplung: jede Wiederholung dunkler, wärmer, schmutziger
            float loop = loopHp.process (loopLp.process (lt.process (lm.process (lb.process (fbSum)))));
            loop = shape (loop, 1.8f) * fb.next() * runG.next();

            // Aufnahmekopf
            float w = x + loop;
            w = shape (recLp.process (recHp.process (w)), 1.35f);
            tape.push (w);

            // Ausgang: BASS / MID / TREBLE → Alterung → ECHO LEVEL → Tape Stop → POWER
            const float g = wetG.next() * stopG.next() * powerOut.next();
            wl = ageL.process (etL.process (emL.process (ebL.process (wl)))) * g;
            wr = ageR.process (etR.process (emR.process (ebR.process (wr)))) * g;
            sumSq += 0.5 * (wl * wl + wr * wr);
            L[i] = inL * dryGain + wl;
            R[i] = inR * dryGain + wr;
        }
        const float rms = (float) std::sqrt (sumSq / juce::jmax (1, n));
        level = rms;
    }
};

//==============================================================================
/** Schutz-Limiter wie der DynamicsCompressor im Browser (−8 dB, 14:1, 2 ms / 180 ms, Make-up). */
struct Protector
{
    double sr = 44100;
    float env = 0, attackC = 0, releaseC = 0;
    static constexpr float ratio = 14.0f, kneeDb = 4.0f;
    float thresholdDb = -8.0f;
    float makeup = 1.67f;
    /** Effekt-Version: nur als Notbremse knapp unter 0 dB, ohne Make-up */
    void setTransparent() { thresholdDb = -1.5f; makeup = 1.0f; }
    void prepare (double s)
    {
        sr = s; env = 0;
        attackC = (float) (1.0 - std::exp (-1.0 / (0.002 * sr)));
        releaseC = (float) (1.0 - std::exp (-1.0 / (0.18 * sr)));
    }
    float curveDb (float inDb) const
    {
        const float over = inDb - thresholdDb;
        if (over <= -kneeDb / 2) return inDb;
        if (over >= kneeDb / 2) return thresholdDb + over / ratio;
        const float t = over + kneeDb / 2;
        return inDb + (1.0f / ratio - 1.0f) * t * t / (2.0f * kneeDb);
    }
    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float pk = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            env += (pk - env) * (pk > env ? attackC : releaseC);
            const float inDb = juce::Decibels::gainToDecibels (env, -120.0f);
            float g = dbToGain (curveDb (inDb) - inDb) * makeup;
            float l = L[i] * g, r = R[i] * g;
            // weiche Kappung knapp unter 0 dBFS
            auto soft = [] (float v) { const float a = std::abs (v); return a < 0.9f ? v : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), v); };
            L[i] = soft (l); R[i] = soft (r);
        }
    }
};

} // namespace dub
