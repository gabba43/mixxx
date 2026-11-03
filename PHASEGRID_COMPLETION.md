<!-- Autonom durch einen KI-Agenten erstellte Dokumentation. -->

# Korrektur der ergänzten Phasenhistogramm-Pipeline

Dieser Bericht beschreibt die Weiterentwicklung nach `cd0af73153`. Der damalige
Stand und seine Messwerte bleiben in
[PHASEGRID_INTEGRATION.md](PHASEGRID_INTEGRATION.md) und
[PHASEGRID_BENCHMARK_RESULTS.json](PHASEGRID_BENCHMARK_RESULTS.json) erhalten.
Die zehn Dateien der portablen Referenz sind weiterhin bytegleich mit
[SOURCE_MANIFEST.json](lib/phasegrid/SOURCE_MANIFEST.json).

## Ursache und Abgrenzung zum ursprünglichen Verfahren

Die untersuchte Originalsoftware setzt mehr Komponenten ein als die erste
portable Nachbildung. Ihre Transientenerzeugung ist nicht vollständig
rekonstruiert. Die erste Ergänzung verwendete eine neue RMS-Novelty, eine
vereinfachte Auswahl nach Intervallvorkommen und einen neu vorgeschlagenen
Annahme-Guard. Ein erfolgreicher Algorithmus in der Originalsoftware kann
deshalb noch nicht mit dieser Nachbildung gleichgesetzt werden.

Der Vergleich belegt keinen grundsätzlichen Konflikt zwischen Mixxx und dem
Phasenhistogrammverfahren. Queen Mary ist ein eigenständiger Vergleichsanalyzer;
seine Ergebnisse sind keine unabhängigen musikalischen Sollwerte.

Zwei konkrete Fehler wurden festgestellt:

1. Die ergänzte Regel `max(histogram) / sum(histogram) >= 0.06` hängt von der
   Binzahl ab. Bei 120 BPM und 1024 Bins kann bereits ein vollständig richtiger
   Ereignisstrom mit weniger als ±4 ms Timingstreuung unter dieser Grenze liegen.
   Die Regel stammt nicht aus dem belegten Originalverfahren.
2. Die rekonstruierte Phasenkorrektur liefert einen absoluten Offset, der eine
   Periodengrenze überschreiten kann. Der Adapter wies negative Offsets ab,
   obwohl sie ein gültiges, periodisch äquivalentes Grid beschreiben. Die neue
   Orchestrierung normalisiert erst die endgültige Ausgabe auf eine Periode.

Die Quellen belegen außerdem eine ursprüngliche lokale Gridbewertung mit
Treffern und Ereignisgewichten sowie vorherigem Tempo als Kandidaten. Die reine
Count-Auswahl bildete diese Stufe nicht nach. Originale Standardgrenzen von
110–220 BPM sind belegt, können jedoch pro Track überschrieben werden. Der
ursprüngliche Wert der Grid-Toleranz ist in den verfügbaren Belegen nicht
aufgelöst. Die neue Toleranz bleibt daher ausdrücklich eine neue Entscheidung.

## Neue, getrennt testbare Komponenten

- `lib/phasegrid/multiband_detector.*` ergänzt eine eigene Streaming-Erkennung
  aus drei Frequenzbändern, adaptiven positiven Energieänderungen und
  zeitlichem Peak-Abstand. Stereo-Energie bleibt auch bei Gegenphase erhalten.
  Es werden keine Queen-Mary-Beats als versteckte Eingabe benutzt.
- `lib/phasegrid/completion.*` bildet mehrere lokale Tempokandidaten, bewertet
  ihre tatsächliche gewichtete Grid-Unterstützung und verfeinert mehrere Seeds
  mit der unveränderten portablen BPM-Feinsuche. Vorherige lokale Tempi und
  Halb-/Doppeltempo-Kandidaten werden berücksichtigt.
- Die Phasenbewertung arbeitet mit einer expliziten Zeittoleranz von 25 ms.
  Sie hängt nicht vom Wert des höchsten einzelnen Histogrammbins ab. Ein
  exakter kreisförmiger Dreieckskernel-Score verhindert lokale Phasenmaxima der
  ersten Fassung.
- Gewichte werden für die globale Analyse über Zeitabschnitte ausgeglichen,
  damit ein einzelner lauter Einschaltvorgang nicht den gesamten Track bestimmt.
  Kleine letzte Abschnitte erhalten ein entsprechend kleineres Gewicht.
- Die Annahme bewertet Untergrundabstand, wirksame Ereigniszahl, unterstützte
  Beatpositionen und zeitliche Abdeckung. Fehlende Zwischenbeats werden bei
  Tempo-Oktaven berücksichtigt. Zeitgleiche oder sehr nahe Duplikate dürfen die
  wirksame Ereigniszahl nicht künstlich erhöhen.
- Die rekonstruierte geometrische Phasenkorrektur wird weiterhin berechnet.
  Falls sie durch viele sehr schwache Ereignisse die gewichtete Unterstützung
  verschlechtert, bleibt die bessere gewichtete Phase erhalten. Beide Phasen
  und die Entscheidung werden diagnostisch aufbewahrt.
- Auch abgelehnte Kandidaten behalten BPM, Phase, Alternativen und
  Ablehnungsgründe. Ein abgelehnter Kandidat wird nicht als Trackgrid gespeichert.

### Konstante Schätzung und Nachweis der Konstanz

`AcceptanceMode::StrictConstant` prüft, ob ein konstantes Grid über den Track
gestützt wird. Dieser Standard der portablen Diagnose weist deutliche
Tempo-/Phasenänderungen ab. Er ist kein sinnvoller Zwang für jede ausdrücklich
gewünschte Constant-Schätzung: Ein bestes konstantes Modell kann auch bei
schwankendem Tempo berechnet werden, wie beim bestehenden Queen-Mary-Constant-Modus.

Der Mixxx-Adapter wählt daher ausdrücklich `BestFitConstant`. Zunächst bleibt ein
streng gestützter Kandidat bevorzugt. Falls keiner die globale Konstanzprüfung
besteht, darf ausreichend starke lokale Rhythmusevidenz eine konstante
Näherung liefern. Dann bestimmen die lokalen Grid-Stimmen den Tempokandidaten;
`best_fit_estimate=true` und `constant_supported=false` halten die Unterscheidung
fest. Die Qualitätshinweise bleiben erhalten. Eine solche Schätzung verfolgt
keine Tempoänderung und ist keine variable Beatkarte.

Der Best-Fit-Modus setzt keine Schwelle herab. Er verwendet die vorhandenen
Anforderungen an lokale Grid-Unterstützung, wirksame Ereignisse und zeitliche
Abdeckung. Stille, unstrukturiertes Rauschen und kurze rhythmische Inseln in
überwiegend evidenzlosem Audio dürfen deshalb weiterhin kein Grid erzeugen.
Die Moduswahl ist Teil der Cache-Metadaten. Die Vergleichsprobe bietet zusätzlich
`completion-strict`, um beide Entscheidungen auf identischen Samples zu prüfen.

Diese Schritte sind eine ausdrücklich vorgeschlagene Vervollständigung. Sie sind
kein nachgewiesener bitidentischer Nachbau der Originalsoftware. Die bestehenden
Queen-Mary-/SoundTouch-Verfahren und ihre Nachkorrekturen werden nicht verändert.
Die neue Analyzer-Auswahl bleibt experimentell und liefert weiterhin nur ein
konstantes Grid. Aus lokaler Tempo-Evidenz wird keine ungetestete variable Karte
erzeugt und kein heimlicher Wechsel zu Queen Mary vorgenommen.

Das Maß `significance` ist ein heuristischer Abstand zu gleichverteilter
Phasenevidenz. Es ist keine kalibrierte Wahrscheinlichkeit für ein musikalisch
richtiges Grid. Die Annahmeparameter sind durch die dokumentierten Tests
abgesichert; ihre allgemeine Güte auf beliebiger Musik ist damit nicht bewiesen.

## Prüfverfahren und Reproduktion

`tools/phasegrid_completion_probe.cpp` verarbeitet lokales Float-PCM ohne Qt und
ohne weitere Analysebibliothek. Es kann die alte/neue Erkennung jeweils mit der
Referenz-/Vervollständigungspipeline kombinieren. Der native, normalerweise
übersprungene Test `PhaseGridFileComparisonTest.ExportConfiguredPcm` exportiert
die von Mixxx dekodierten Samples für diesen Vergleich. Die acht Exporte besitzen
dieselben PCM-Hashes wie der vorherige native Vergleich.

Ein eigener Export nach einem normalen Test-Build:

```sh
mkdir -p build/phasegrid-pcm
QT_QPA_PLATFORM=offscreen \
MIXXX_PHASEGRID_INPUT="/pfad/zur/aufnahme.mp3" \
MIXXX_PHASEGRID_OUTPUT="$PWD/build/phasegrid-pcm/aufnahme.json" \
MIXXX_PHASEGRID_PCM="$PWD/build/phasegrid-pcm/aufnahme.f32" \
  build/mixxx-test --gtest_filter=PhaseGridFileComparisonTest.ExportConfiguredPcm
python3 - <<'PY'
import json
from pathlib import Path
directory = Path("build/phasegrid-pcm")
files = [json.loads(p.read_text()) for p in sorted(directory.glob("*.json"))
         if p.name != "manifest.json"]
(directory / "manifest.json").write_text(json.dumps({"files": files}, indent=2))
PY
```

Weitere Aufnahmen können mit jeweils eigenen PCM-/Metadatenpfaden exportiert
werden. Der Export enthält kein musikalisches Soll-Grid. Er benötigt lokal
Speicher für das unkomprimierte Stereo-Audio; diese Dateien werden nicht ins
Repository übernommen.

```sh
cmake --build build --target phasegrid-completion-probe --parallel 4
python3 tools/evaluate_phasegrid_completion.py \
  --probe build/phasegrid-completion-probe \
  --manifest build/phasegrid-pcm/manifest.json \
  --output-dir build/phasegrid-completion-results
```

Der Vergleich umfasst die vier Frontend-/Pipeline-Kombinationen und zusätzlich
die strenge neue Pipeline, mit getrennten Ergebnissen für
veröffentlichte Grids und nur diagnostisch vorhandene Kandidaten. Die erzeugten
Testsignale verwenden bekannte Pulszeiten, Stereo-Gegenphase, wechselnde Pegel,
Akzente, Jitter, Offbeats, kontinuierlichen Bass, Rauschen sowie Tempoänderungen.
Die als Kalibrierung und zurückgehaltene Variation bezeichneten synthetischen
Gruppen verwenden unterschiedliche Seeds und Tempi. Nachdem deren Ergebnisse
zur Fehlersuche eingesehen wurden, sind sie kein unangetasteter Blindtest mehr.
Eine weitere unabhängige Ereignisprüfung wurde erst nach dem Einfrieren der
Korrektur ausgeführt.

Echte Aufnahmen besitzen weiterhin keine unabhängigen Soll-Beats. Ihre
Annahmequote und ihre Übereinstimmung mit Queen Mary sind keine Trefferquote.
Laufzeiten der isolierten Probe sind einzelne Diagnosemessungen; der native
Vergleich erfasst zusätzlich den tatsächlichen Mixxx-Adapter.

## Abschließende Messungen

Stand: 3. November 2026. Anwendung und Vergleichsprogramme bauen erfolgreich.
Der vollständige CTest-Lauf meldet **1.352 Fälle ohne Fehler** in 78,81 Sekunden;
die zwei dateigebundenen Diagnosefälle wurden dabei erwartungsgemäß übersprungen.
Die 38 zuvor deaktivierten Tests bleiben deaktiviert. Die fünf portablen
Testprogramme umfassen 41 unveränderte Referenzprüfungen, 18 neue Frontend- und
30 neue Vervollständigungsprüfungen. Die beiden neuen Module bestehen zusätzlich
AddressSanitizer/UndefinedBehaviorSanitizer; LeakSanitizer wurde wegen des
bekannten ptrace-Konflikts der Umgebung deaktiviert.

Die 24 früheren Queen-Mary-/SoundTouch-Vergleichsmetriken stimmen exakt mit dem
Stand vor dieser Arbeit überein. Alle zehn Referenzdateien und acht geschützten
Legacy-Dateien besitzen weiterhin ihre ursprünglichen Hashes. Das neue
Vergleichswerkzeug besteht Black, Flake8 und 15 Metrik-/Modusprüfungen.

[PHASEGRID_COMPLETION_RESULTS.json](PHASEGRID_COMPLETION_RESULTS.json) enthält
kompakte Messdaten und die Quellhashes. Die vollständigen Kandidaten, Beats und
Logs bleiben im lokalen Sicherungsarchiv erhalten. Der Variantenvergleich umfasst
**110 Läufe**: 14 erzeugte Audiosignale und acht Aufnahmen in fünf Kombinationen.
Alle 88 dort gebildeten Vergleichspaare verwenden identisches PCM. Der erneute
native Mixxx-Vergleich umfasst **51 Läufe**; alle 51 Paare besitzen identische
Quelldatei- und PCM-Hashes. Es gab keine Ausführungs- oder Dekodierfehler.

### Bekannte Soll-Beats

Die Kombination aus neuer Erkennung und neuer Pipeline nimmt alle neun
konstanten Signale an und erreicht auf jedem Beat-F1 = 1 bei 70 ms Toleranz.
Der größte BPM-Fehler beträgt 0,00793 BPM; der größte mediane absolute Beatfehler
eines Signals beträgt 4,61 ms. Die beiden Rauschsignale und die Stille erhalten
kein Grid. Die strenge Variante weist zusätzlich beide Tempoänderungsfälle ab.

Der ausdrückliche Best-Fit-Modus liefert für den Sprung 112 → 136 BPM eine
konstante 136,0002-BPM-Näherung und für den Anstieg 100 → 145 BPM etwa
135,8969 BPM. Beide sind als `best_fit_estimate=true`,
`constant_supported=false` gekennzeichnet. Ihre Beat-F1-Werte von 0,677 und
0,377 zeigen die erwartete begrenzte Eignung eines einzigen konstanten Grids.
Diese Fälle werden nicht als erfolgreiche Tempoverfolgung gezählt.

Eine nach dem Einfrieren der Pipeline ausgeführte Ereignisprüfung nimmt 44
rhythmische Fälle an und weist zwölf weitere Zufallsrauschfälle ab. 36 der 44
Ergebnisse treffen das vorgegebene Beat-Niveau; acht liefern Doppeltempo bei
zusätzlichen schwachen Zwischenereignissen. Außerhalb dieser Oktavabweichungen
liegen der größte BPM-Fehler bei 0,0097 BPM und der größte Phasenfehler bei
3,45 ms. Diese Oktavfälle bleiben eine dokumentierte Einschränkung; sie werden
nicht als 44 richtige Temposchätzungen ausgegeben.

### Hochgeladene Aufnahmen

| Aufnahme | Native Ausgabe | Konstanz unterstützt | Lokale Rhythmusabdeckung |
| --- | --- | --- | ---: |
| Temper – No Favors | 123,1912 BPM, konstante Näherung | nein | 94,9 % |
| Peech Boys – Don't Make Me Wait | 115,3300 BPM, konstante Näherung | nein | 83,5 % |
| Code Red – Conquering Lion | 170,2591 BPM, konstante Näherung | nein | 63,5 % |
| Loleatta Holloway – Love Sensation | 116,6469 BPM, konstante Näherung | nein | 83,8 % |
| 49ers – Keep your love | 129,9887 BPM, konstantes Grid | ja | 77,5 % |
| Apollo Two – Atlantis | keine Ausgabe; Diagnosekandidat 157,4002 BPM | nein | 23,5 % |
| Origin Unknown – Valley Of The Shadows | keine Ausgabe; Diagnosekandidat 159,8478 BPM | nein | 56,3 % |
| Remarc – RIP | keine Ausgabe; Diagnosekandidat 164,8698 BPM | nein | 54,8 % |

Damit erhalten **fünf statt zuvor null Aufnahmen** eine Schätzung. Die weiteren
drei scheitern an unzureichend über die Aufnahme verteilter starker lokaler
Rhythmusevidenz. Das ist eine offen gebliebene Grenze dieser Rekonstruktion;
es beweist weder fehlenden Rhythmus in der Musik noch einen Fehler der
Originalsoftware. Ebenso beweisen die fünf angenommenen Ergebnisse ohne
unabhängige Soll-Beats keine bestimmte musikalische Genauigkeit. Vier sind
ausdrücklich konstante Näherungen, keine variable Karte.

Die native Analyse benötigt bei diesen acht Aufnahmen etwa 0,31–0,52 Sekunden;
der gesamte frische Prozess erreicht etwa 99–109 MiB Spitzen-RSS. Die Zeit
enthält Initialisierung, Frontend, Suche und Finalisierung, ohne Dekodierung,
Hashing und Export. Der Speicher umfasst auch Qt und Decoder. Die früheren
Begrenzungen und die Aussagen zum Messumfang gelten weiterhin.

Die beiden eigenständigen Stufen wurden getrennt geprüft: Auf den fünf konstanten
synthetischen Variationssignalen erreicht die alte Erkennung mit alter Pipeline
im Mittel einen diagnostischen Beat-F1 von 0,732, die alte Erkennung mit neuer
Pipeline 0,860 und die neue Kombination 1,000. Die neue Erkennung mit alter
Pipeline erreicht nur 0,679. Beide Ergänzungen sind daher wesentlich; eine bloße
Schwellensenkung oder ein Austausch nur der Erkennung löst die gezeigten Fehler
nicht. Diese Werte sind auf den dokumentierten erzeugten Korpus begrenzt.

## Verbleibende Gegenprüfung

Der ursprüngliche Fehler ist kein grundsätzliches Format- oder Schnittstellen-
problem von Mixxx/Queen Mary. Die erste Nachbildung enthielt eigene, unvollständige
Ergänzungen; die konkreten Annahme- und Phasenfehler sind jetzt behoben. Für einen
Nachweis der Gleichwertigkeit zur anderen Software fehlen weiterhin deren genaue
Transienten-/Gewichtsbildung, einzelne Auswahlparameter und unabhängige reale
Referenzgrids. Insbesondere sind BPM-Oktaven, die drei verbliebenen Ablehnungen
und variable Beatkarten noch nicht durch diesen Teststand gelöst. Eine bekannte
Softwareversion, deren Analysemodus und exportierte Soll-Beats erlauben die
gezielte nächste Gegenprüfung, ohne Queen Mary als vermeintliche Ground Truth
oder neue Schwellen als Originalverhalten auszugeben.

<!-- Ende der autonom durch einen KI-Agenten erstellten Dokumentation. -->
