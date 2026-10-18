# Selbstsignierte Windows-Releases

Die GitHub-Releases ab 0.1.1 werden mit einer Authenticode-Signatur für
**Marcus Beckerle** und einem RFC-3161-Zeitstempel versehen. Das Zertifikat ist
**selbstsigniert**: Es bestätigt keine durch eine öffentliche Zertifizierungsstelle
geprüfte Identität und beseitigt keine SmartScreen-Warnung auf fremden Rechnern.

Öffentliches Zertifikat: `Marcus-Beckerle.cer`

- Inhaber und Aussteller: `CN=Marcus Beckerle`
- SHA-1-Fingerabdruck zur Zertifikatsidentifikation:
  `9F19841B3CFA93CDE8C6AB20CD1EDE1CD989755D`
- Gültig bis 18. Oktober 2029.
- RSA 3072 Bit, Code-Signing-Verwendungszweck; EXE-Dateisignatur mit SHA-256.

SHA-1 wird hier ausschließlich als Windows-Zertifikatskennung verwendet.
Dateiprüfsummen und Signaturen verwenden SHA-256. Ein Zeitstempel erlaubt die
Prüfung des Signierzeitpunkts, macht das selbstsignierte Zertifikat aber nicht
öffentlich vertrauenswürdig. Ein nicht lokal vertrauendes Windows kann bei
`Get-AuthenticodeSignature` deshalb einen Vertrauensfehler melden.

Die `.cer` enthält nur das öffentliche Zertifikat. Sie darf weitergegeben
werden und liegt auch im Release. Die App installiert sie **nicht** automatisch
als vertrauenswürdig. Der private Schlüssel und PFX-Passwörter gehören niemals
ins Repository, Release, Chat oder Build-Log.

## Neues Release veröffentlichen

1. Gewünschten Quellstand in `main` committen und pushen.
2. Unter GitHub → Releases ein Release mit neuem Tag `vX.Y.Z` erstellen,
   beispielsweise `v0.1.2`. Auch `v0.1.2-beta.1` ist möglich.
3. Release bzw. Prerelease veröffentlichen. Ein gespeicherter Entwurf startet
   den Workflow noch nicht.
4. **Actions → Build and sign release** abwarten. Der Workflow baut exakt den
   Release-Commit, führt Tests aus, signiert und prüft die EXE und lädt EXE,
   ZIP, öffentliches Zertifikat und `SHA256SUMS.txt` an dieses Release.

Die Dateien erscheinen erst nach erfolgreichem Durchlauf. Bei Fehlern werden
keine neuen ungeprüften Dateien veröffentlicht. Ein erneuter Durchlauf kann
unter Actions manuell für einen bestehenden Release-Tag gestartet werden; er
ersetzt die Assets dieses Releases. Dafür den Workflow auf `main` starten.
Bestehende Release-Tags nicht verschieben, sondern neue Versionen veröffentlichen.

Die Versionsnummer kommt im Release-Build aus dem Tag. Ein lokaler Build nutzt
`VERSION`, alternativ `build.ps1 -Version 0.1.2`. Dadurch stimmen Dateinamen,
Windows-Dateieigenschaften und „Über“-Dialog überein. Lokale/PR-/Main-Builds
bleiben unsigniert; sie erhalten keinen Zugriff auf die Signiergeheimnisse.

## Schutz des Schlüssels

- Der private Schlüssel liegt lokal im Windows-Zertifikatspeicher
  **Aktueller Benutzer → Eigene Zertifikate (My)**.
- Für GitHub wird ein passwortgeschützter PFX-Export im Speicher erzeugt.
  `gh secret set` verschlüsselt die Übertragung für GitHubs Secret-Speicher.
- Environment **release-signing**: Secrets `WINDOWS_SIGNING_PFX` und
  `WINDOWS_SIGNING_PASSWORD`; zugelassene Referenzen: Tags `v*` und Branch `main`.
- Jeder Release-Commit muss bereits in `main` enthalten sein. Bauen und Signieren
  laufen auf getrennten GitHub-Runnern; nur der Signierschritt erhält die Secrets.
- Der Signierschritt prüft den Schlüssel gegen das eingecheckte öffentliche
  Zertifikat und importiert ihn nicht exportierbar auf dem kurzlebigen Runner.
- Nur zur Signaturprüfung vertraut dieser Runner vorübergehend genau diesem
  Zertifikat. Der Schritt entfernt anschließend Schlüssel, temporäre Vertrauens-
  eintragung und PFX-Datei. Entwickler- und Endnutzer-Vertrauensspeicher werden
  nicht verändert.
- GitHub-Administratoren bzw. Personen, die berechtigte Workflows ändern können,
  können den Schlüssel zur Signierung verwenden. Repository-/Environment-Zugriff
  entsprechend eng halten und GitHub-Konto mit Mehrfaktor-Authentifizierung sichern.

## Einrichtung oder Wiederherstellung

`scripts/provision-signing.ps1` ist eine **explizite administrative Einrichtung**,
kein Build-Schritt. Es benötigt die offizielle GitHub CLI mit passenden Rechten
für Repository-Environments und Secrets. Es verwendet ein vorhandenes passendes
lokales Zertifikat erneut und rotiert die veröffentlichte Identität nicht still.

```powershell
# Überträgt den privaten Schlüssel bewusst in das GitHub-Environment:
.\scripts\provision-signing.ps1
```

Für einen Rechnerwechsel den privaten Schlüssel separat als verschlüsselte PFX
aus dem Windows-Zertifikatsmanager sichern. GitHub lässt Secret-Werte nicht wieder
auslesen. Eine solche Sicherung nicht im Repository oder dessen Releases ablegen.
Bei Schlüsselverlust/-kompromittierung neues Zertifikat, öffentliche CER und
Secrets gemeinsam wechseln und den Wechsel dokumentieren.
