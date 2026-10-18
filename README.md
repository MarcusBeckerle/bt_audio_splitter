# BT Audio Splitter

Eine native Windows-App von **Marcus Beckerle**, um einen Film über bis zu fünf
Audioausgaben zu hören. Echte Gerätenamen, getrennte Lautstärken, automatische
Wiederaufnahme und eine kleine, helle Oberfläche. Kein Mikrofonzugriff.

**Die portable x64-EXE ist kleiner als 1 MB.** C++20, Win32 und WASAPI, ohne
.NET, Electron, Python, Node.js oder separat installierbare Laufzeit.

[Downloads](https://github.com/MarcusBeckerle/bt_audio_splitter/releases) ·
[Projekt](https://github.com/MarcusBeckerle/bt_audio_splitter)

## Starten

1. Die EXE aus dem Release herunterladen und starten. Alternativ ZIP entpacken;
   es enthält zusätzlich README und Lizenzdateien. Die EXE allein reicht;
   Lizenztexte sind auch unter **Über Audio Splitter · Lizenzen** enthalten.
2. **Einrichtung → Treiber installieren** wählen. Die App lädt das unveränderte
   **VB-CABLE**-Paket vom Hersteller (ca. 1,3 MB), prüft SHA-256 und die
   Windows-Signatur des Setups. In der Administratorabfrage zustimmen, im
   Herstellerfenster **Install Driver** wählen und Windows neu starten.
3. **Windows-Namen setzen** benennt den virtuellen Eingang mit Administratorfreigabe
   in **Audio Splitter** um. Bis dahin heißt er in Windows
   **CABLE Input (VB-Audio Virtual Cable)**. Die App zeigt unter dem Eingang
   stets den tatsächlichen Windows-Namen an. Bei mehreren Kabeln **Eingang wählen**.
4. Im Filmplayer oder in den Windows-Soundeinstellungen **Audio Splitter**
   beziehungsweise **CABLE Input** als Ausgabe wählen. Nach einem Wechsel
   gegebenenfalls den Player neu starten. Die App ändert das Standardgerät nicht.
5. Kopfhörer zuerst mit Windows koppeln. **Hinzufügen** öffnet die verfügbaren
   Ausgaben mit Windows-Namen, Status und eindeutiger Gerätekennung. Auswählen
   und für jeden Kopfhörer die gewünschte Lautstärke einstellen.

Voraussetzungen: **Windows 10 22H2 oder Windows 11, x64**. Der Windows-Bluetooth-
Treiber muss die verwendeten Kopfhörer unterstützen. Auch USB-/HDMI-Ausgaben
sind möglich. Der normale Betrieb benötigt weder Administratorrechte noch Internet.
Nur die Einrichtung lädt einen Treiber herunter und verwendet den in Windows
enthaltenen ZIP-Entpacker über Windows PowerShell. Es ist nichts nachzuinstallieren.
GitHub-Releases ab 0.1.2 sind selbstsigniert als **Marcus Beckerle**. Das ist keine
öffentlich vertrauenswürdige Signatur; Windows kann weiterhin eine SmartScreen-
Warnung zeigen. Details: [Signierung und Release-Ablauf](signing/README.md).

VB-CABLE kann bei seiner Installation Windows-Standardgeräte ändern, auch die
Kommunikationseingabe. Danach die Windows-Einstellungen überprüfen. Die App
selbst öffnet ausschließlich Render-Endpunkte und deren WASAPI-Loopback.

## Bedienung

Der Eingang und die Einrichtung zeigen den Treiberstatus mit Text und Farbe:
**Rot: Treiber fehlt, Eingang nicht bereit oder Fehler. Grün: Eingang bereit.**
Während Prüfung oder Einrichtung ist die Anzeige gelb. Nicht mehr vorhandene
Windows-Geräte werden nicht als installierter Treiber gewertet.

| Bedienelement | Wirkung |
| --- | --- |
| Hinzufügen | Bis zu fünf unterschiedliche Windows-Ausgaben auswählen; Offline-Geräte sind erlaubt. |
| Verbinden | Wiedergabe aktivieren und bei erneuter Verfügbarkeit automatisch fortsetzen. |
| Pausieren | Stream beenden; bleibt auch nach Aus-/Einschalten und App-Neustart pausiert. |
| Lautstärke | Nur diesen weitergeleiteten Stream regeln; Wert wird gespeichert. |
| ⋯ → Geräteinformationen | Vollständiger Windows-Name, Gerätekennung und Status/Fehler. |
| ⋯ → Gerät entfernen | Stream beenden und gespeicherte Zuordnung löschen. Windows-Kopplung bleibt bestehen. |
| Einrichtung | Treiber, Windows-Name, Eingangsauswahl sowie Bluetooth-/Soundeinstellungen. |

Die App merkt sich Geräte anhand ihrer **Windows-Endpunkt-ID**, nicht anhand des
Namens. Gleich benannte Kopfhörer bleiben getrennt. Nach Entfernen und erneutem
Koppeln in Windows kann eine neue ID entstehen; dann die alte Zuordnung entfernen
und das Gerät neu hinzufügen.

„Verbinden“ bedeutet: Audio automatisch starten, sobald **Windows das Gerät wieder
verbunden hat**. Die App führt kein Bluetooth-Pairing durch und kann keine vom
Windows-Treiber abgelehnte Funkverbindung erzwingen. Geräte werden ungefähr
einmal pro Sekunde geprüft; Aufbau eines neuen Audiostreams kommt hinzu.
Beim Schließen der App endet die Weiterleitung; sie läuft nicht versteckt im Tray.

## Filmqualität und Grenzen

- Stereo mit 32-Bit-Fließkommaverarbeitung. WASAPI im gemeinsamen Modus verwendet
  Windows' hochwertigen Formatkonverter separat für jede Ausgabe. Die App
  komprimiert den Ton nicht zusätzlich und fordert kein Mikrofonprofil an.
- Den virtuellen Eingang auf **Stereo, vorzugsweise 48 kHz** stellen. Unterstützt
  werden Stereo-Float32 und PCM16/24/32 mit 8–192 kHz; kein eigener Surround-Downmix.
- Erkennbare Hands-Free-/Freisprech-Endpunkte sind ausgeschlossen. Andere Apps
  mit Mikrofonzugriff können Windows trotzdem in einen schlechteren Bluetooth-
  Modus versetzen. Bluetooth-Codec und Funkqualität bestimmt Windows samt Hardware.
- **Bis zu fünf Ausgaben** sind in der App unterstützt. Fünf gleichzeitige
  **Bluetooth**-Streams hängen vom Adapter, Treiber, Codec und Funkumfeld ab.
  Diese Hardwarekapazität kann die App nicht garantieren.
- Verschiedene Kopfhörer haben unterschiedliche Latenzen. Es gibt keine
  automatische Laufzeitmessung oder exakte Synchronisierung ihrer Geräteuhren.
  Ein Bild/Ton-Versatz im Player korrigiert den gemeinsamen Versatz, nicht
  unterschiedliche Verzögerungen zwischen Kopfhörern.
- Pro Ausgabe: eigener Audiothread, 180-ms-Live-Puffer, WASAPI-Puffer von
  angeforderten 80 ms. Bei Überlauf werden alte vollständige Frames verworfen;
  bei Unterlauf wird Stille ausgegeben. Die Verzögerung wächst dadurch nicht
  unbegrenzt, bei Aussetzern/Clock-Drift können hörbare Übergänge entstehen.
- Geschützte Medien können Loopback-Capture verhindern. Exclusive-Mode-Ausgabe
  des Players ist nicht vorgesehen. Die App zeichnet keine Audiodateien auf.

Player-, Windows- und Kopfhörerlautstärke wirken zusätzlich zu den App-Reglern.

## Entfernen

**Einrichtung → Treiber entfernen** öffnet nach Hinweis erneut das offizielle
VB-CABLE-Setup. Dort **Remove Driver** wählen und Windows neu starten. Dies
entfernt den Treiber systemweit, auch für andere Programme. Hierfür ist erneut
ein Download nötig. Temporäre Setup-Dateien werden nach Abschluss entfernt.

Zum Entfernen der App die EXE löschen. Optional
`%LOCALAPPDATA%\BtAudioSplitter` löschen: dort liegen `settings.txt`, begrenzte
Fehlerprotokolle und gegebenenfalls nach einem Absturz verbliebene Setup-Dateien.
In Windows nach Treiberentfernung eine vorhandene Audioausgabe wählen.

**VB-CABLE ist Donationware von VB-Audio**, mit eigenen Lizenzbedingungen.
[Hersteller und Unterstützung](https://vb-audio.com/Cable/).

## Bauen

Auf dem Entwicklungsrechner: Windows x64 und PowerShell. Das Buildskript lädt
bei Bedarf **LLVM-MinGW 20261006 / LLVM 23.1.3 (UCRT x64)** in `.tools`, prüft
die festgelegte SHA-256-Prüfsumme und installiert keine globalen Werkzeuge.
Der Compilerdownload ist ca. 191 MB; er wird **nicht** mit der App ausgeliefert.

```powershell
# Erstmalig: Compiler lokal herunterladen, testen, bauen und paketieren
.\build.ps1 -Bootstrap

# Weitere Builds mit vorhandener lokaler Toolchain
.\build.ps1

# Entwicklerbuild mit Debugsymbolen
.\build.ps1 -Configuration Debug
```

Bei heruntergeladenem Skript gegebenenfalls nach Durchsicht `Unblock-File
.\build.ps1` ausführen. Keine globale Änderung der Ausführungsrichtlinie nötig.
Lokale Builds verwenden die Datei `VERSION`. Mit `build.ps1 -Version 0.1.2`
kann die Version überschrieben werden. Release-Builds übernehmen sie automatisch
aus dem GitHub-Tag; Windows-Metadaten und „Über“-Dialog werden gemeinsam erzeugt.

Das Skript behandelt Compilerwarnungen als Fehler, führt die Tests aus und
prüft die importierten DLLs auf unerwünschte Compilerlaufzeiten. C++-Hilfsroutinen
sind statisch eingebunden; die übrigen DLLs sind Windows-Systemkomponenten.
Es werden keine EXE-Packer verwendet. Release-Dateien:

```text
artifacts/release/
  BT-Audio-Splitter-0.1.2-win-x64.exe
  BT-Audio-Splitter-0.1.2-win-x64.zip
  SHA256SUMS.txt
```

Nur diese Dateien weitergeben. `.tools` und `artifacts/native` sind Build-Dateien.
GitHub Actions führt denselben Build samt Tests auf `windows-latest` aus.
Beim Veröffentlichen eines Releases mit Tag `vX.Y.Z` baut der separate Workflow
**Build and sign release** den Tag, signiert mit den geschützten Environment-
Secrets und lädt EXE, ZIP, öffentliche CER und Prüfsummen an das Release.
Das ZIP enthält ebenfalls die signierte EXE. Ein Entwurf startet noch keinen Build.
Der private Schlüssel wird nicht bei normalen Push-/PR-Builds bereitgestellt.

## Aufbau und Stabilität

```text
native/core.hpp       Einstellungen, Zustandsregeln, begrenzter Stereo-Ringpuffer
native/pipe.hpp       Threadsichere Audiopuffer und Verteilung an mehrere Ausgaben
native/platform.*     UTF-8, COM-/Handle-Lebensdauer, atomare Dateien, Logrotation
native/audio.*        WASAPI, Gerätesuche, Reconnect, getrennte MTA-Audiothreads
native/setup.*        Geprüfter Herstellerdownload, Setup und erhöhter Namenshelfer
native/ui.*           DPI-fähige Win32-Oberfläche, native Tastaturbedienung, Dialoge
native/main.cpp       Einzelinstanz, Start und Diagnoseargumente
native/tests/         Automatisierte Zustands-, Puffer-, Parallelitäts- und Dateitests
licenses/             Lizenzhinweise der eingebundenen Laufzeitkomponenten
build.ps1             Gepinnte Toolchain, Tests, EXE, ZIP und Prüfsummen
```

Der Kontrollthread verwaltet Benutzerabsicht und Gerätezustand getrennt.
Capture und **jede einzelne Ausgabe** haben einen eigenen MTA-Thread mit MMCSS-
Audio-Priorität. COM-Objekte bleiben auf ihrem erzeugenden Thread. Selbst ein
langsamer Geräteaufbau blockiert damit keine anderen laufenden Audiostreams.
Die Verteilung verwendet unveränderliche Listen und kurze Puffer-Sperren;
unter diesen Sperren gibt es keine Treiberaufrufe.

Die UI erhält Wertkopien und verarbeitet keine Audioframes. Verbindungsfehler
werden getrennt behandelt und versucht zu beheben. Pause/Entfernen schließt
die jeweilige Warteschlange und beendet den Stream. Änderungen werden höchstens
alle 250 ms gespeichert und beim normalen Beenden abschließend gesichert.
Ein temporärer Datensatz wird zuerst geschrieben und geflusht, dann atomar
ersetzt. Beschädigte Einstellungen werden separat gesichert und sichtbar gemeldet.

Keine Telemetrie, Cloud-Konten oder Hintergrundupdates. Der eigene Projektcode
hat keine zusätzlichen Bibliotheksabhängigkeiten; er verwendet die Windows-APIs
und die statisch eingebundenen C++-/MinGW-Hilfsroutinen.

## Tests und Abnahme

`build.ps1` führt 16 automatisierte Tests aus: Zustandsregeln für Wiederkehr und
Pause, dauerhafte Entfernung, fünf Geräte, IDs/Unicode/Lautstärken, defekte
Einstellungen, Pufferverhalten gegen ein Zufalls-Referenzmodell, getrennte
Verteilung, parallele Zugriffe und atomare Dateiersetzung.

Zusätzlich kann die fertige EXE selbst geprüft werden:

```powershell
# Windows-Ausgabeendpunkte: Textbericht, keine Änderungen
.\AudioSplitter.exe --diagnose .\audio-devices.txt

# Optionaler ca. 6-sekündiger stiller WASAPI-Test an einer aktiven Stereo-Ausgabe
.\AudioSplitter.exe --audio-self-test .\audio-self-test.txt

# Vorschau mit Beispielgeräten; kein Audio und kein Desktop-Screenshot
.\AudioSplitter.exe --preview .\ui-preview.png
```

Der Audiotest prüft Loopback, Formatkonvertierung, fünf Ausgabethreads, eine
absichtlich ungültige Geräte-ID und wiederholten Neuaufbau einer Ausgabe.
Er spielt ausschließlich Stille, verändert keine Windows-Lautstärke und
speichert keine Audiodaten. Er ist **kein Test mit fünf Bluetooth-Kopfhörern**.

**Version 0.1.x ist eine Vorabversion.** Automatisierte Tests, UI-Vorschau und
nativer Audiotest sind bestanden. Noch offen: echte Bluetooth-Hardwareabnahme
und Treiberinstallation/-entfernung auf dem Zielrechner. Abnahmeschritte:

1. Treiber installieren, neu starten, Eingang benennen und im Player wählen.
2. Zwei Kopfhörer hinzufügen, Film hören, Lautstärken getrennt regeln.
3. Einen ausschalten: der andere muss weiterlaufen. Einschalten, Windows-
   Wiederverbindung und automatische Wiedergabe abwarten.
4. Pausiertes Gerät aus-/einschalten: keine automatische Wiedergabe.
5. App neu starten: Lautstärken und Pausen bleiben erhalten.
6. Gerät entfernen: keine Wiederaufnahme oder erneute automatische Aufnahme.
7. Einen längeren Film sehen und Latenz, Drift und Funkstabilität prüfen.
8. Wenn verfügbar, auf drei bis fünf Ausgaben erweitern; Treiberentfernung testen.

Fehlerprotokoll: `%LOCALAPPDATA%\BtAudioSplitter\app.log`, maximal ca. 1 MB
plus eine Rotation. Vor dem Teilen Diagnose/Logs auf persönliche Angaben prüfen.

## Lizenz

Copyright (c) 2026 **Marcus Beckerle**.
Nichtkommerzielle Nutzung unter [PolyForm Noncommercial 1.0.0](LICENSE).
Name und Projektlink gemäß [NOTICE](NOTICE) bei Weitergabe erhalten.
Für kommerzielle Nutzungen außerhalb dieser Lizenz ist eine separate
kostenpflichtige Vereinbarung erforderlich: [Kommerzielle Lizenz](COMMERCIAL-LICENSE.md).

[Lizenzübersicht](LICENSING.md) · [Fremdkomponenten](THIRD-PARTY-NOTICES.md).
Das Projekt ist source available, nicht OSI Open Source.
