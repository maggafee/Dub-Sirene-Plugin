# Dub Sirene – VST3 / AU

Zwei Plugins aus dem WordPress-Plugin „Dub Sirene“ (GeyserHaus e.V.), mit denselben Grafiken und derselben Klangkette:

| Plugin | Typ | Inhalt |
|---|---|---|
| **Dub Sirene** | Instrument | Sirene (Wellenform, SPEED, Pitch, Mod Depth, Filter, Volume, Hold), Federhall, Dubasco-Bandecho, Effekt-Leiste |
| **Dubasco Tape Echo** | Effekt | Bandecho mit 3 Tonköpfen, Bass/Mid/Treble, Wow & Flutter, Tape Stop, Dub Throw, Runaway, Tap Tempo |

Formate: VST3 (Mac + Windows), AU (Mac), Standalone.

## Fertige Plugins herunterladen

Jeder Push auf `main` baut beide Plugins auf GitHub für Mac (Apple Silicon + Intel) und Windows:
**Actions → letzter Lauf „Build plugins“ → Artifacts**. Ein Tag `v1.2.1` legt zusätzlich ein Release mit den ZIPs an.

Installation: siehe `INSTALL.txt` (liegt auch in jedem ZIP).

## Selbst bauen

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
JUCE 8 wird automatisch geladen. Prüfprogramm ohne DAW (rendert WAV + PNG): `-DDUB_TESTS=ON`, Ziele `DubTestSiren`, `DubTestEcho`.

## Aufbau

- `Source/DspCore.h` – Klangerzeugung (Port der Web-Audio-Kette, Filter nach Web-Audio-Formeln)
- `Source/PluginProcessor.*` – Parameter, MIDI (Note = SPEED, Sustain = Hold), Tempo-Sync
- `Source/PluginEditor.*` – Oberfläche aus den Original-Grafiken (`Assets/`)

## Lizenz

Code und Grafiken © GeyserHaus e.V. Gebaut mit JUCE 8 – für die Weitergabe gilt die JUCE-Lizenz
(kostenlose „Starter“-Lizenz bis 50.000 $ Jahresumsatz, sonst AGPLv3 oder kommerzielle Lizenz).
VST ist eine Marke der Steinberg Media Technologies GmbH.
