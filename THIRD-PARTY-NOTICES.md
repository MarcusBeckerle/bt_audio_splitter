# Third-party components

BT Audio Splitter's own code uses the license in LICENSE. The following
components retain their original licenses; no ownership of them is claimed.

## NAudio 2.2.1

Audio I/O and format conversion: NAudio.Core, NAudio.Wasapi and their NAudio
dependencies. Copyright Mark Heath and contributors. MIT license; see
[licenses/NAudio.txt](licenses/NAudio.txt).

Source: https://github.com/naudio/NAudio/tree/v2.2.1

## Microsoft .NET 10 and Windows Desktop / WPF

The self-contained executable includes the .NET runtime and WPF. See the
Microsoft license and third-party notices in `licenses/`. All these notices
are also embedded in the executable and accessible through its About dialog.

Sources: https://github.com/dotnet/runtime and https://github.com/dotnet/wpf

## VB-CABLE (optional, downloaded separately)

VB-CABLE is software by VB-Audio / Vincent Burel. It is not developed by
Marcus Beckerle and is not licensed under this project's license.

Origin: https://vb-audio.com/Cable/ (https://www.vb-cable.com/)

**VB-CABLE is donationware. All contributions are welcome.** Its own licensing
terms, including terms for professional use, apply independently:
https://vb-audio.com/Services/licensing.htm

The app downloads the unchanged official VBCABLE_Driver_Pack45.zip package
only when the user requests installation/removal. It verifies the pinned
SHA-256 hash before launching the manufacturer's installer. The manufacturer's
package includes its original readme and license. The driver is not bundled
in the source repository or release archive.

Changing the Windows endpoint display name to "Audio Splitter" does not
change the driver's authorship, binary files, or license.
