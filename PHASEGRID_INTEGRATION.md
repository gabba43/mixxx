<!-- Autonom durch einen KI-Agenten erstellte Dokumentation. -->

# Native Integration der experimentellen Phasenhistogramm-Analyse

Die portable Referenz ist als **zusätzlicher, ausdrücklich auswählbarer nativer
Analyzer** eingebunden. Ihre ID ist `phase-histogram:1`; in der Oberfläche heißt
sie `Phase Histogram (Experimental, constant tempo)`. Queen Mary bleibt der
Standard. SoundTouch und Queen Mary bleiben eigenständige Analyseverfahren.

Diese Integration macht aus dem experimentellen Prototyp keinen nachgewiesenen
Nachbau der untersuchten Originalanwendung. Insbesondere ist die Erzeugung der
Transienten aus Audio eine neue Entwicklung. Eine bit- oder ergebnisidentische
Rekonstruktion ist nicht belegt.

## Umfang und Auswahl

- Die neue Auswahl liefert ein **konstantes Tempo mit absoluter Beatphase**.
  Automatische Variable-/Constant-Auswahl, kontinuierliche Tempoverfolgung und
  eine musikalisch verifizierte Taktanfangserkennung gehören nicht dazu.
- Für diesen Anbieter zeigt die Oberfläche Constant an und deaktiviert die
  Tempoauswahl sowie Fast Analysis. Die bisherige globale Tempo-/Fast-Einstellung
  bleibt für andere Anbieter erhalten.
- Der neue Analyzer verarbeitet den vollständigen Track. Er darf nicht an der
  früheren Fast-Analysis-Grenze abgeschnitten werden.
- Es werden keine Modelle, Worker, Modell-Downloads oder Python-Inferenzpakete
  eingeführt. Python wird ausschließlich für die optionalen Vergleichs- und
  Installationshilfen verwendet; die Analyse selbst läuft in C++.

## Schichten und Schnittstellen

| Schicht | Ort | Aufgabe |
| --- | --- | --- |
| Portable Ereignistypen | `lib/phasegrid/core_types.h` | Sekunden und getrennte allgemeine/Bass-Gewichte |
| Neuer Transientendetektor | `lib/phasegrid/transient_detector.*` | Streaming-Energienovelty aus PCM, Zustands- und Eingabeprüfung |
| Mathematischer Kern | `lib/phasegrid/phase_grid_core.*` | Intervallstimmen, Tempo-Konsens, Histogramme, Feinsuche, Phasenkorrektur und separate Ankerrekonstruktion |
| Austauschbare Auswahlstrategien | `lib/phasegrid/analysis_pipeline.*` | Lokale Auswahl, Oktaventscheidung und grobe Phase als explizite Strategien |
| Nativer Plugin-Adapter | `src/analyzer/plugins/analyzerphasegrid.*` | Mixxx-Lebenszyklus, Eingabeformat, Ergebnisprüfung, Konfiguration und Diagnostik |
| Mixxx-Analyse und Speicherung | `src/analyzer/analyzerbeats.*` | Anbieterwahl, vollständiger Eingangsstrom, Schutz vorhandener Grids und Übernahme des fertigen Ergebnisses |

Die portable Bibliothek benötigt C++17 und die Standardbibliothek. Mixxx baut sie
mit seiner vorhandenen C++20-Toolchain als statisches PIC-Target
`mixxx-phasegrid`. Sie verwendet weder Qt noch Queen Mary oder Mixxx-Trackklassen.
Der schmale Adapter verwendet die bereits vorhandenen Qt-/Mixxx-Abhängigkeiten.
Es werden keine zusätzlichen Analysebibliotheken benötigt.

Mixxx aktiviert normalerweise `-ffast-math`. Für diese Bibliothek, ihre portablen
Tests und die Adapterdatei werden gezielt strikte Gleitkommaoptionen verwendet;
LTO und die Wiederverwendung des Fast-Math-PCH sind an dieser Grenze deaktiviert.
Der erste native Testlauf zeigte andernfalls weggeoptimierte NaN-/Inf-Prüfungen.
Der äußere Wrapper verwendet Mixxx' vorhandenes `util_isfinite()`. Diese
Build-Korrektur verändert keine Referenzdatei und keine Compileroption der
bisherigen Analyzer.

### PCM, Zeit und Kanäle

Der Plugin-Adapter erhält interleavtes Stereo-PCM. Samplerate und Zeit bleiben
nativ; Ereigniszeiten werden in Sekunden geführt. Nur die lokale Intervallstufe
benutzt ihre ausdrücklich definierte virtuelle 44.100-Hz-Basis. Diese ist nicht
mit der Samplerate des eingehenden Tracks gleichzusetzen.

Die äußere Analyse dupliziert Mono zu Stereo und verwendet bei STEM-Dateien die
vorhandene Auswahl des Rhythmuskanals beziehungsweise den vorhandenen Downmix.
Andere Kanalzahlen werden für diesen Anbieter vor der Verarbeitung abgewiesen.
Sampleraten werden im Vertrag des neuen Detektors geprüft. Unvollständige Frames,
NaN/Inf, ungültige Zeiger oder ein beschädigter Lebenszyklus führen zu einem
Fehlerstatus und dürfen kein teilweise berechnetes Grid veröffentlichen.

Der Analysepfad gehört in den bestehenden Analyse-Worker, nicht in den
Echtzeit-Audiothread. Der Detektor behält den Energiezustand über Chunk-Grenzen
hinweg; Ereignisse werden für die anschließende globale Suche gesammelt.

### Fertige Ergebnisse umgehen fremde Nachkorrekturen

`AnalyzerBeatsPlugin` besitzt zusätzlich den Vertrag
`providesResolvedBeatGrid()` / `resolvedBeatGrid(subVersion)`. Für bestehende
Plugins ist dieser standardmäßig deaktiviert. Der neue Analyzer liefert darüber
sein bereits aufgelöstes Ergebnis.

`AnalyzerBeats::storeResults()` übernimmt dieses Ergebnis unmittelbar mit
`Track::trySetBeats()`. Der Gridaufbau erfolgt über `Beats::fromConstTempo()` mit
dem berechneten BPM-Wert und dem absoluten Phasenoffset, auf die nächste native
Frame-Grenze gerundet. Diese Ausgabequantisierung ist eine Integrationsentscheidung
und keine neue Tempo- oder Phasenschätzung.

Für diesen Pfad werden **weder** `BeatFactory::makePreferredBeats()` **noch**
`BeatGridEvidence` oder Queen-Mary-Nachverfeinerung ausgeführt. Andernfalls würden
ein weiterer Tempo-Fit, eine Glättung oder eine Audio-Phasenkorrektur das neue
Verfahren unbemerkt verändern. Queen Mary bleibt eine getrennte Vergleichsquelle;
sein `TempoTrackV2` wird nicht als versteckte Stufe des neuen Kerns eingesetzt.

Ein Grid wird nur nach einem vollständigen, fehlerfreien Eingabestrom und einer
erfolgreichen Finalisierung angehängt. Stille, fehlende Evidenz oder eine
unzulässige Ausgabe ersetzen kein vorhandenes Grid. Manuelle und importierte
Grids werden entsprechend den expliziten Reanalyse-Einstellungen behandelt;
BPM-Locks bleiben geschützt und werden bei der Trackübernahme erneut geprüft.

## Experimentelle Entscheidungen und Grenzen

Der Adapter verwendet `decompiled_radius_one_plan()` mit 128, 256, 512 und 1024
Bins sowie dem ausdrücklich gewählten asymmetrischen Kandidatenraster. Die
Gewinnerhistogramme aller Stufen tragen zum Phasenakkumulator bei. Die kleine
abschließende Phasenkorrektur arbeitet geometrisch mit Ereignispositionen, nicht
mit deren Amplituden, und liefert einen neuen absoluten Offset.

Die verwendete Strategie `proposed-count-only-v1` bleibt dagegen bewusst eine
vereinfachte experimentelle Strategie. Sie ersetzt nicht die vollständige lokale
Gridbewertung, die ursprüngliche Oktavheuristik oder die Bass-/Vier-Beat-Auswahl
der untersuchten Anwendung. Ein rekonstruierter Suchplan allein stellt keine
Gleichheit des gesamten Analyseverfahrens her.

Der neue Energienovelty-Detektor definiert Zeitlage, Schwellen und Gewichte neu.
Seine synthetischen Timingtests belegen keine Gleichheit mit den ursprünglichen
gespeicherten Transienten und keine allgemeine Qualität auf beliebiger Musik.
Mehrdeutige metrische Ebenen, langsame Einschwingvorgänge, schwache Transienten,
Breakbeats und tatsächliche Tempowechsel bleiben Gegenstand der vergleichenden
Bewertung. Insbesondere kann ein konstanter Analyzer einen echten Tempowechsel
nicht durch ein variables Grid ausdrücken.

Die separate Ankerrekonstruktion im portablen Kern erzeugt Beats aus vorhandenen
Ankern und bewahrt ein abschließendes Arbeitstempo. Sie ist kein automatischer
Variable-Tempo-Detektor und wird durch diese konstante Plugin-Ausgabe nicht zu
einem solchen gemacht.

Der portable Kern weist Eingaben und Arbeitsmengen außerhalb seiner Verträge
explizit ab; sehr dichte oder lange Ereignisfolgen können diese Grenzen erreichen.
Ein portabler Abbruch-Callback existiert in der Pipeline. Der aktuelle native
`finalize()`-Aufruf reicht diesen Callback noch nicht weiter; eine bereits
laufende synchrone Feinsuche ist daher nicht granular über diesen Mechanismus
abbrechbar.

## Eingefrorene Referenz und Cache

[SOURCE_MANIFEST.json](lib/phasegrid/SOURCE_MANIFEST.json) identifiziert den
unveränderten portablen Quellstand als
`phasegrid-portable-reference-40be614e6103`. Alle zehn dort aufgeführten
Implementierungs-, Header- und Testdateien wurden bytegleich übernommen.
Adapter, Build-Verdrahtung und diese Dokumentation liegen außerhalb dieser
Referenz. Es wurden keine Rohdekompilate, Assembly-Dateien, Originalprogramme
oder Audioaufnahmen in das Repository kopiert.

Der bestehende eingefrorene Stand ist die Basis des Vergleichs. Änderungen am
Algorithmus müssen anschließend als eigener Schritt mit neuer Referenz und
erneuten Vergleichsergebnissen bewertet werden; sie dürfen nicht als bloße
Adapteränderung in diese Integration einfließen.

Die gespeicherte Analyseversion enthält Anbieter-, Kern-, Frontend-, Strategie-,
Suchplan-, Konfigurations- und Annahmeversionen. Ein Konfigurationshash erfasst
unter anderem Samplerate, Quellkanalzahl und wirksame Detektor-/Suchparameter.
Für STEM wird auch die Kanalstrategie berücksichtigt. Zusätzlich erzeugt CMake
einen Fingerabdruck der sieben portablen Produktionsdateien. Dieser ist kein
vollständiger Build-Hash; Adapteränderungen erfordern weiterhin eine bewusste
Versions-/Konfigurationspflege. Globale Auto-/Variable-Einstellungen dürfen
denselben konstanten Anbieter nicht fälschlich als variablen Cache behandeln.

## Tests und Reproduktion

Die bytegleich übernommenen Dateien bestehen mit Host-GCC 14.2 und
`-std=c++17 -Wall -Wextra -Wpedantic -Werror -O2` alle **41 portablen Prüfungen**:
12 Detektor-, 18 Kern- und 11 Pipeline-Prüfungen. Die einzelnen Befehle stehen in
[lib/phasegrid/README.md](lib/phasegrid/README.md). Das sind ausgeführte
Prototyptests; sie werden nicht als Nachweis einer Originalprogramm-Gleichheit
ausgegeben.

CMake registriert die drei separaten portablen Testprogramme zusätzlich bei
CTest. Ihre Quellen besitzen jeweils ein eigenes `main()` und gehören nicht in
das bestehende Google-Test-Hauptprogramm. Die nativen Adaptertests stehen in
`src/test/phasegrid_analyzer_test.cpp`; sie prüfen unter anderem:

- ausdrückliche Anbieterwahl und unveränderte bestehende Standardauswahl;
- Wiederinitialisierung, Stille und fehlgeschlagene beziehungsweise unvollständige
  Eingaben ohne Veröffentlichung eines Teilresultats;
- direkte Übernahme von BPM und Phase ohne die alten Nachkorrekturen;
- vollständige Analyse auch hinter der bisherigen Fast-Analysis-Grenze;
- Mono-/Stereo-Gleichheit, STEM-Kanalpolitik und Cache-Metadaten;
- Schutz vorhandener manueller, importierter oder gesperrter Grids.

Die Dateivergleichsschnittstelle ist
`PhaseGridFileComparisonTest.AnalyzeConfiguredFile` in
`src/test/phasegrid_comparison_test.cpp`. Ohne explizit gesetzte Ein-/Ausgabedatei
wird dieser dateigebundene Test übersprungen. Er dekodiert über Mixxx und führt
die jeweils ausgewählte Analyse auf dem resultierenden PCM aus.

Nach dem Build lassen sich die relevanten Tests beispielsweise so aufrufen:

```sh
ctest --test-dir build --output-on-failure -R 'PhaseGrid|PhaseHistogram'
```

Das Vergleichswerkzeug `tools/compare_phasegrid.py` vergleicht standardmäßig
Queen Mary Auto, Queen Mary Constant und Phase Histogram Constant in getrennten
Testprozessen. Synthetische Dateien besitzen bekannte Soll-Beats. Reale
Aufnahmen erhalten keine erfundenen Sollwerte: Übereinstimmung zweier Analyzer
ist dort eine Paarvergleichsgröße und keine belegte Treffergenauigkeit.
Dekodier-, Analyse- und Speicherumfang sind im jeweiligen Bericht getrennt
auszuweisen.

## Abschließende Build-, Test- und Vergleichsergebnisse

Stand: 2. November 2026, Vergleichsbasis `c85a8ae16b`. Debian 13, GCC 14.2,
CMake 3.31.6, Qt 6.8.2, vier verfügbare CPU-Kerne im Entwicklungscontainer.

Anwendung und Tests bauen erfolgreich. Das Installationsskript wurde mit
`--no-deps --build-dir build --prefix /usr/local --jobs 4` im vorbereiteten
Container tatsächlich ausgeführt: Build, vollständiger CTest-Lauf, Installation
und Versionsprüfung waren erfolgreich. CTest meldet **1.347 Tests ohne Fehler**
in 75,34 Sekunden; darin ist der ohne Dateiparameter erwartungsgemäß übersprungene
Dateivergleich enthalten. 38 bereits vorher deaktivierte Tests bleiben
deaktiviert. Nach Ergänzung der Ablehnungsdiagnostik bestanden erneut alle
16 betroffenen CTest-Fälle, einschließlich der drei Programme mit den
41 portablen Einzelprüfungen. Die zuletzt gebauten Programme wurden installiert.
Paketinstallation auf einem leeren System und physische DJ-Hardware wurden bei
diesem Lauf nicht getestet.

Die acht geschützten Legacy-Quelldateien stimmen bytegleich mit `c85a8ae16b`
überein. Alle **24 früheren Vergleichsfälle liefern exakt dieselben gespeicherten
BPM-/Grid-/Timingfehlermetriken** wie vor dieser Integration; die gemessene
Ausführungsdauer ist dabei kein Gleichheitskriterium. Alle zehn portablen
Referenzdateien stimmen weiterhin mit ihrem Manifest überein.

Die Skripte bestehen zusätzlich 10 Installer-Kontrollflusstests und
12 Metriktests sowie Bash-Syntaxprüfung, ShellCheck, Black und Flake8. CMake
besteht Gersemi und CMakeLint. Ein Test an realer DJ-Hardware bleibt erforderlich.

### Gemeinsamer Audiokorpus

Der abschließende Lauf enthält **51 Messungen**: neun deterministische WAVs und
alle acht hochgeladenen Aufnahmen, jeweils Queen Mary Auto, Queen Mary Constant
und Phase Histogram Constant. Alle 51 Konfigurationspaare besitzen identische
Quelldatei- und dekodierte PCM-Hashes; es gab keine Ausführungs-/Dekodierfehler.
[PHASEGRID_BENCHMARK_RESULTS.json](PHASEGRID_BENCHMARK_RESULTS.json) enthält die
Messwerte und Ablehnungsdiagnosen. Vollständige Beats, Paarmetriken und Logs
liegen lokal unter `build/phasegrid-comparison-final/` und im Sicherungsarchiv.

Beats werden nach Ausschluss kurzer Randbereiche chronologisch eins zu eins
mit 70 ms Toleranz zugeordnet. Die Zuordnung maximiert die Trefferzahl; ihre
Zeitfehler sind keine separate Optimierung auf den jeweils nächsten Beat.
Driftfits werden nur bei ausreichender Abdeckung als solche ausgewiesen.
Laufzeiten umfassen Initialisierung, Verarbeitung und Finalisierung, ohne
Dekodierung/Hashing/Export. Speicher ist Linux `VmHWM` des frischen nativen
Prozesses einschließlich Qt, Decoder und Ergebnisaufbau, vor abschließender
JSON-Serialisierung. Es handelt sich um einen abschließenden Messdurchgang,
keine statistische Laufzeitstudie und keine isolierte Heap-Messung.

### Bekannte Soll-Beats

| Signal | Queen Mary Auto: Beat-F1 | Queen Mary Constant: Beat-F1 | Neuer Analyzer: Beat-F1 / Verhalten |
| --- | ---: | ---: | --- |
| 120 BPM, 44,1 kHz | 1,000 | 1,000 | 1,000; 119,999727 BPM |
| 132,06 BPM | 1,000 | 1,000 | 1,000; 132,060456 BPM |
| 174 BPM mit Akzenten | 1,000 | 1,000 | 1,000; 174,000147 BPM |
| 120 BPM, 48 kHz | 1,000 | 1,000 | 1,000; 119,999200 BPM |
| Sprung 120 → 138 BPM | 1,000 | 0,634 | 0,656; konstantes 120-BPM-Grid |
| Anstieg 110 → 140 BPM | 1,000 | 0,463 | kein Ergebnis |
| Halber Beat Phasensprung | 0,977 | 0,500 | 0,500 |
| Stille | kein Ergebnis | kein Ergebnis | kein Ergebnis |
| Nur drei Sekunden | 0,889 | 0,889 | zu wenig Evidenz; kein Ergebnis |

Auf den vier unveränderten konstanten Signalen beträgt der mediane absolute
Beatfehler des neuen Analyzers 2,50–3,51 ms, bei Queen Mary 3,76–4,68 ms. Seine
qualifizierten Driftfits liegen dort betragsmäßig bei 0,05–0,40 ms/min; das sind
kurze, einfache Signale und kein Nachweis für Langzeitstabilität auf Musik.
Die Analyse benötigt dort 17,8–21,6 ms gegenüber 138–221 ms bei Queen Mary
Constant. Die Prozessspeicherwerte liegen überwiegend um 92 MiB und enthalten
den gemeinsamen Qt-/Decoder-Grundbedarf.

Beim Temposprung verfolgt Queen Mary Auto das variable Grid. Die grobe
Auswertung über 8-Sekunden-Fenster lokalisiert den Wechsel etwa 2,18 Sekunden
vor dem Sollpunkt; diese Fenstergröße begrenzt die Aussage über den Zeitpunkt.
Der neue konstante Analyzer kann weder diesen Wechsel noch den Phasensprung
als variable Karte ausdrücken. Ein kleiner Fehler nur auf übrig gebliebenen
Treffern darf diese fehlende Abdeckung nicht verdecken.

### Hochgeladene Musikaufnahmen

Es gibt hier keine manuell markierten Soll-Beats. BPM und Auto-Entscheidungen
von Queen Mary sind deshalb Vergleichsausgaben, keine Ground Truth.

| Aufnahme | QM Auto BPM / Grid | QM Constant BPM | Neuer Analyzer | Analyse ms QM Auto / neu | Peak MiB QM Auto / neu |
| --- | --- | ---: | --- | ---: | ---: |
| 49ers – Keep your love | 129,990 / Constant | 129,990 | abgelehnt | 2659 / 202 | 121,6 / 105,7 |
| Peech Boys – Don't Make Me Wait | 115,327 / Constant | 115,327 | abgelehnt | 2662 / 230 | 124,8 / 108,3 |
| Loleatta Holloway – Love Sensation | 116,720 / Variable | 116,723 | abgelehnt | 3160 / 228 | 125,9 / 108,8 |
| Temper – No Favors | 123,150 / Variable | 123,200 | abgelehnt | 2699 / 231 | 124,1 / 108,5 |
| Code Red – Conquering Lion | 85,170 / Variable | 84,760 | abgelehnt | 1609 / 144 | 112,3 / 99,1 |
| Apollo Two – Atlantis | 78,700 / Variable | 78,700 | abgelehnt | 2496 / 212 | 116,2 / 100,0 |
| Origin Unknown – Valley Of The Shadows | 159,854 / Constant | 159,854 | abgelehnt | 1751 / 153 | 111,8 / 99,7 |
| Remarc – RIP | 164,868 / Constant | 164,868 | abgelehnt | 1867 / 132 | 112,8 / 99,5 |

**Der neue Analyzer liefert auf keiner dieser acht Aufnahmen ein verwendbares
Grid.** Beatpositionen, Drift und Tempoänderungen seiner Ausgabe sind hier
folglich nicht messbar. Seine kürzere Laufzeit ist kein Qualitätsvorteil,
weil er kein gleichwertiges Ergebnis erzeugt.

Die ergänzte Diagnostik grenzt den Grund ein: Audio wurde vollständig verarbeitet,
Transienten und lokale Tempovorschläge wurden gebildet, die Feinsuche lief durch.
Alle acht Kandidaten scheitern anschließend an der bestehenden vorgeschlagenen
Konzentrationsschwelle von `minimum_peak_fraction = 0.06`. Gemessene Spitzenanteile
liegen zwischen 0,00169 und 0,02852. Die Rohkandidaten weichen teilweise erheblich
von Queen Mary ab; zum Beispiel 112,147 gegenüber 78,700 BPM bei Atlantis und
116,556 gegenüber 159,854 BPM bei Valley Of The Shadows. Diese Rohwerte sind
ausdrücklich keine akzeptierten Analyseergebnisse. Bei Keep your love und RIP
sind die Rohkandidaten dagegen ähnlich, werden aber ebenfalls abgewiesen.

Das ist eine nachgewiesene Grenze der unkalibrierten Referenzstrategie und ihrer
neuen Transientenerzeugung, keine fehlende Mixxx-Ergebnisspeicherung. Eine bloße
Absenkung der Schwelle würde auch bisher abgewiesene, möglicherweise falsche
Kandidaten veröffentlichen. Für eine belastbare Vervollständigung fehlen die
musikalische Kalibrierung sowie die ursprüngliche lokale Grid-/Oktavbewertung;
bekannte Soll-Beats auf realen Aufnahmen werden dafür benötigt. Diese Lücken
werden nicht als durch synthetische Tests behoben ausgegeben.

### Entscheidung über weitere Regeln

Die bisherigen Nachkorrekturen bleiben für den neuen Analyzer ausgeschaltet.
Es gibt keinen Nachweis, dass ihre Übernahme die abweichende Transientenevidenz
und Kandidatenauswahl richtig korrigiert; derzeit werden die Ergebnisse bereits
vor diesen Stufen abgelehnt. Der neue Anbieter erhält auch keine Auto-Auswahl:
Die Sollsignale belegen, dass echte Tempo-/Phasenänderungen eine variable Karte
benötigen, die dieser Kern nicht schätzt. Ein versteckter Wechsel zu Queen Mary
wäre ein anderes Verfahren. Queen Mary bleibt daher Standard und Variable-/Auto-
Analyzer; der neue Anbieter bleibt eine ausdrücklich experimentelle, isoliert
testbare Integration. Als nächster fachlicher Schritt sind Transientenevidenz,
Kandidatenauswahl und eine über Histogrammauflösungen sinnvoll kalibrierte
Annahmeentscheidung getrennt mit annotierten Musikbeispielen zu prüfen.

Reproduktion nach einem normalen Test-Build:

```sh
python3 tools/compare_phasegrid.py --synthetic \
  --input-dir /pfad/zu/den/acht/aufnahmen \
  --test-binary build/mixxx-test --output-dir build/phasegrid-comparison
```

<!-- Ende der autonom durch einen KI-Agenten erstellten Dokumentation. -->
