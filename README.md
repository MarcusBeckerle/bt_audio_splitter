# BT Audio Splitter

Eine kleine Windows-App von **Marcus Beckerle**, um einen Film über mehrere
Kopfhörer zu hören. Native C#-/WPF-Oberfläche, bis zu fünf Windows-Audioausgaben,
getrennte Lautstärken und automatische Wiederaufnahme nach Geräteausfällen.
Es werden keine Mikrofone geöffnet.

Projekt und Downloads: https://github.com/MarcusBeckerle/bt_audio_splitter

## Download und Voraussetzungen

Unter [Releases](https://github.com/MarcusBeckerle/bt_audio_splitter/releases)
die Windows-x64-EXE oder das ZIP herunterladen. Die EXE kann direkt auf den
anderen Rechner kopiert werden; im ZIP liegen zusätzlich die Dokumentation
und Lizenzdateien. Die Lizenztexte sind auch in der EXE unter „Über“ enthalten.

- Windows 10 ab Version 22H2 oder Windows 11, **x64**.
- Kein separat installiertes .NET, Python, Node.js oder sonstiger Interpreter
  nötig. Die .NET-Desktop-Laufzeit steckt in der EXE.
- Bluetooth-Kopfhörer müssen mit Windows gekoppelt sein. Auch USB-, HDMI-
  und andere Audioausgaben können verwendet werden.
- Einmalige Installation von **VB-CABLE** für den virtuellen Audioeingang.
  Die App lädt dafür etwa 1,3 MB direkt vom Hersteller. Administratorrechte
  und ein anschließender Windows-Neustart sind erforderlich.
- Für den normalen Betrieb sind keine Administratorrechte und kein Internet
  notwendig. Die EXE ist nicht mit einem kommerziellen Codesignatur-Zertifikat
  signiert; Windows kann deshalb eine SmartScreen-Warnung anzeigen.

Die erste Version ist eine **Vorabversion**. Die Logik ist automatisiert
getestet; ein vollständiger Hörtest mit zwei bis fünf Bluetooth-Kopfhörern und
Treiberinstallation auf dem Zielrechner steht noch aus.

## In wenigen Schritten hören

1. EXE starten und **Einrichtung → Treiber installieren** wählen. Das unveränderte
   VB-CABLE-Setup öffnet sich mit Windows-Administratorabfrage. Dort **Install
   Driver** wählen und Windows anschließend neu starten.
2. App erneut öffnen. Sie erkennt den normalen VB-CABLE-Eingang automatisch.
   Unter **Windows-Namen setzen** kann dieser mit Administratorfreigabe in
   **Audio Splitter** umbenannt werden. Bis dahin heißt er in Windows
   **CABLE Input (VB-Audio Virtual Cable)**; die App zeigt den tatsächlichen
   Windows-Namen unter dem Audioeingang an. Das Umbenennen ist optional.
3. Im Filmplayer oder in den Windows-Soundeinstellungen **Audio Splitter**
   beziehungsweise **CABLE Input** als Ausgabe wählen. Bei einem Standardgerätwechsel
   gegebenenfalls den Player neu starten. Die App ändert das Standardgerät nicht selbst.
4. Kopfhörer unter **Hinzufügen** auswählen. Neu hinzugefügte Ausgaben starten
   mit 70 % App-Lautstärke. Mit den Reglern die Lautstärke je Gerät einstellen.
5. Die App während des Films geöffnet lassen; Minimieren über die Taskleiste
   funktioniert. Schließen beendet alle Audioausgaben der App.

Nach einer Treiberinstallation auch das Windows-Standardmikrofon prüfen:
Windows beziehungsweise der Herstellerinstaller kann Standardgeräte ändern.
Die App selbst fordert keinen Mikrofonzugriff an.

## Geräte und Zustände

| Bedienung / Zustand | Verhalten |
| --- | --- |
| Verbinden | Die Ausgabe wird aktiviert; bei fehlendem Gerät wartet die App. |
| Verbunden | Der Ausgabestream ist geöffnet. Ob gerade ein Film läuft, erkennt dieser Status nicht. |
| Wartet auf Kopfhörer | Sobald Windows das Gerät wieder als Audioausgabe bereitstellt, versucht die App automatisch erneut zu starten. |
| Pausieren | Die Ausgabe stoppt. Sie bleibt auch nach Aus-/Einschalten und App-Neustart pausiert. |
| Gerät entfernen | Entfernt die Ausgabe samt Lautstärke und Verbindungswunsch aus dieser App. Die Windows-Bluetooth-Kopplung bleibt bestehen. |
| Geräteinformationen | Zeigt den Windows-Gerätenamen und die eindeutige Audioendpoint-ID. |

Windows führt das Bluetooth-Koppeln und den Funkverbindungsaufbau aus. Die App
öffnet und überwacht die daraus entstehenden Audiostreams; sie kann das
Wiederverbinden eines Kopfhörers durch Windows nicht erzwingen.

Geräte werden über ihre Endpoint-ID gespeichert. Gleichnamige Geräte können
verschiedene Lautstärken haben; im Auswahldialog hilft ein kurzer ID-Zusatz bei
der Unterscheidung. Nach Entkoppeln, Treiberwechsel oder erneutem Koppeln kann
Windows eine neue ID vergeben. Dann den alten Eintrag entfernen und das neue
Gerät hinzufügen. Ein ausgetauschter virtueller Eingang lässt sich unter
**Einrichtung → Eingang wählen** auswählen.

## Filmqualität und technische Grenzen

- Die App verwendet WASAPI im gemeinsamen Modus und verarbeitet die Lautstärke
  als 32-Bit-Fließkomma. Windows/NAudio passen das Format an jede Ausgabe an.
  Sie kodiert selbst kein MP3/AAC und verwendet kein Mikrofonprofil.
- Den virtuellen Eingang für Filme auf **Stereo, vorzugsweise 48 kHz** lassen.
  Der Treiber verwendet standardmäßig Stereo. Surround-/Mehrkanalformate werden
  von dieser Version nicht als eigener Film-Downmix unterstützt.
- Erkennbare Hands-Free-/Freisprech-Endpunkte werden aus der Auswahl entfernt.
  Andere Apps mit Mikrofonzugriff können Windows trotzdem in einen schlechteren
  Bluetooth-Modus versetzen. Die App erzwingt keinen Bluetooth-Codec.
- Bis zu fünf Ausgaben sind in der App möglich. Wie viele **Bluetooth**-Streams
  gleichzeitig stabil funktionieren, bestimmt der Adapter samt Treiber, Codec,
  Funkumgebung und Kopfhörern. Eine Garantie für fünf Geräte gibt es nicht.
- Unterschiedliche Kopfhörer haben unterschiedliche Latenzen. Es gibt in dieser
  Version keine automatische Laufzeitmessung und keine exakte Synchronisierung
  der Geräteuhren. Im Player bei Bedarf Bild/Ton-Versatz anpassen. Das gleicht
  jedoch nicht unterschiedliche Verzögerungen zwischen Kopfhörern aus.
- Jeder Ausgang besitzt einen begrenzten Live-Puffer. Bei Überlauf werden alte
  vollständige Frames verworfen; bei Unterlauf wird Stille ausgegeben. So wächst
  die Verzögerung nicht unbegrenzt. Bei Aussetzern oder langfristiger Clock-Drift
  können trotzdem kurze hörbare Übergänge entstehen.
- Geschützte Medien, die Windows-Loopback-Capture unterbinden, sind möglicherweise
  nicht übertragbar. Exclusive-Mode-Ausgabe des Players ist nicht vorgesehen.

Die App-Regler ändern nur den weitergeleiteten Ton. Die Lautstärke im Player,
am virtuellen Eingang, in Windows und am Kopfhörer wirkt zusätzlich.

## Treiber entfernen und App deinstallieren

Unter **Einrichtung → Treiber entfernen** öffnet die App das offizielle
VB-CABLE-Setup. **Remove Driver** wählen und Windows neu starten. Das entfernt
VB-CABLE systemweit, also auch für andere Programme, die es nutzen. Das
ursprüngliche ZIP bleibt zum erneuten Start des Installers lokal im Cache.

Zum Entfernen der App die EXE löschen. Optional den Ordner
`%LOCALAPPDATA%\BtAudioSplitter` löschen; darin befinden sich Einstellungen,
Fehlerprotokoll und Treibercache. Nach Entfernen des virtuellen Geräts in Windows
wieder eine vorhandene Audioausgabe auswählen.

VB-CABLE ist **Donationware von VB-Audio**, mit eigenen Lizenzbedingungen.
Unterstützung willkommen: https://vb-audio.com/Cable/

## Entwicklung und Build

Voraussetzungen auf dem **Entwicklungsrechner**: Windows x64, PowerShell und
.NET SDK **10.0.402** (in `global.json` fixiert). Alternativ lädt `-Bootstrap` ein per SHA-512 geprüftes Microsoft-SDK
lokal nach `.tools`; es findet keine globale SDK-Installation statt.

```powershell
# Mit installiertem .NET 10 SDK:
.\build.ps1

# Ohne SDK: lokal herunterladen, dann testen und bauen:
.\build.ps1 -Bootstrap

# Eigene Versionsnummer:
.\build.ps1 -Version 0.1.1
```

Falls PowerShell ein heruntergeladenes Skript blockiert, nach Prüfung des
Inhalts einmalig `Unblock-File .\build.ps1` ausführen. Eine globale Änderung
der Ausführungsrichtlinie ist nicht erforderlich.

Das Skript stellt die gesperrten NuGet-Abhängigkeiten wieder her, führt die
Tests aus, veröffentlicht eine eigenständig lauffähige x64-EXE und erstellt:

```text
artifacts/release/
  BT-Audio-Splitter-0.1.0-win-x64.exe
  BT-Audio-Splitter-0.1.0-win-x64.zip
  SHA256SUMS-0.1.0.txt
```

Nur diese Dateien weitergeben; `.tools`, `bin` und `obj` sind Build-Dateien.
`packages.lock.json` fixiert die Paketversionen. Bei einem bewussten
Dependency-/SDK-Update Lockfiles und Runtime-Lizenzhinweise aktualisieren.

## Projektaufbau

```text
src/BtAudioSplitter.Core/    Zustandslogik, atomare Einstellungen, Live-Puffer
src/BtAudioSplitter/         WPF-Oberfläche, WASAPI, Windows-Treibereinrichtung
tests/BtAudioSplitter.Tests/ Deterministische Tests mit simulierter Audiohardware
licenses/                   Lizenztexte der mitgelieferten Komponenten
build.ps1                   Tests, portable EXE, ZIP und Prüfsummen
```

`Router` verwaltet den gespeicherten Benutzerwunsch getrennt vom aktuellen
Gerätezustand. Ein einzelner MTA-Worker serialisiert COM-Lifecycle-Aufrufe
außerhalb des UI-Threads. Capture verteilt Daten an unabhängige, threadsichere
Ausgabepuffer. Bei Pause/Entfernen werden Streams beendet, bei Device-Recovery
werden sie anhand der gespeicherten Absicht neu aufgebaut. Das Pollingintervall
beträgt etwa eine Sekunde, zuzüglich Windows-Verbindungsaufbau.

Die UI verarbeitet keine Audioframes. Lautstärkeänderungen werden gedrosselt
an den Worker übertragen und atomar in JSON geschrieben. Beschädigte
Einstellungen werden gesichert und mit einem sichtbaren Hinweis ersetzt.
Es gibt keine Telemetrie, Cloud-Konten oder Audiodateiaufzeichnung.

## Tests und Diagnose

```powershell
dotnet run --project tests/BtAudioSplitter.Tests -c Release

# Read-only: verfügbare Windows-Ausgabeendpunkte als JSON
& '.\BT Audio Splitter.exe' --diagnose '.\audio-devices.json'

# Optional: stiller nativer WASAPI-Streamtest auf einer aktiven Stereo-Ausgabe
& '.\BT Audio Splitter.exe' --audio-self-test '.\audio-self-test.json'
```

Die automatisierten Tests prüfen Wiederverbinden, Pause über Neustarts,
Entfernen, fünf Ausgaben, getrennte Lautstärken, Capture-/Ausgabefehler,
Treiberwartung, Feedback-Vermeidung, Buffer-Überlauf/Unterlauf und Dateipersistenz.
Sie ersetzen keinen Bluetooth-Hardwaretest.

Der optionale native Selbsttest öffnet etwa eine Sekunde lang eine vorhandene
Ausgabe und deren Loopback-Eingang. Er gibt nur Nullsamples bei App-Lautstärke
null aus, ändert keine Windows-Lautstärke und speichert keine Audiodaten.
Auch dieser Test bestätigt keine Bluetooth-Synchronität oder Treiberinstallation.

Manueller Abnahmetest auf dem Zielrechner:

1. VB-CABLE installieren, neu starten, Eingang benennen und im Player wählen.
2. Zwei Kopfhörer hinzufügen; denselben Film hören und Lautstärken getrennt regeln.
3. Einen Kopfhörer ausschalten: der andere muss weiterlaufen. Wieder einschalten
   und auf die Windows-Verbindung und anschließende Wiederaufnahme warten.
4. Pausierten Kopfhörer aus-/einschalten: er darf nicht automatisch wiedergeben.
5. App neu starten: Lautstärken und Pausen müssen erhalten sein.
6. Gerät entfernen; es darf bei Wiederkehr nicht erneut hinzugefügt werden.
7. Einen längeren Film ansehen und Latenz, Drift und Funkstabilität prüfen.
8. Falls vorhanden, auf drei bis fünf Ausgaben erweitern; Treiberentfernung testen.

Fehlerprotokoll: `%LOCALAPPDATA%\BtAudioSplitter\app.log` (begrenzt, eine Rotation).
Vor dem Teilen Geräteinformationen und persönliche Pfade in Diagnose/Logs prüfen.

## Lizenz

Copyright (c) 2026 **Marcus Beckerle**.

Nichtkommerzielle Nutzung unter [PolyForm Noncommercial 1.0.0](LICENSE).
Name und Projektlink gemäß [NOTICE](NOTICE) bei Weitergabe erhalten.
Für kommerzielle Nutzungen außerhalb dieser Lizenz ist eine separate
kostenpflichtige Vereinbarung erforderlich: [Kommerzielle Lizenz](COMMERCIAL-LICENSE.md).

Details: [Lizenzübersicht](LICENSING.md), [Fremdkomponenten](THIRD-PARTY-NOTICES.md).
Das Projekt ist source available, nicht OSI Open Source.
