# Third-party components

The project's own code is licensed under LICENSE. Dependencies retain their
original licenses. All notices below are also embedded in the standalone EXE
and available through its About dialog.

## LLVM / libc++ / libc++abi / libunwind / compiler-rt

Built with LLVM-MinGW 20261006, LLVM 23.1.3. The executable statically links
the necessary C++ and compiler support routines. Apache License 2.0 with
LLVM Exceptions; see [licenses/LLVM.txt](licenses/LLVM.txt).

Sources: https://github.com/llvm/llvm-project and
https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006

## MinGW-w64 runtime and headers

The toolchain's original notices are included in `licenses/COPYING*`, especially
`COPYING.MinGW-w64-runtime.txt`. These cover the Zope Public License and applicable
permissive/public-domain portions, as well as winpthreads notices. The compiler
toolchain itself is not distributed with the app.

Source: https://github.com/mingw-w64/mingw-w64

## Windows system libraries

WASAPI, Win32, GDI+, WinHTTP, BCrypt and the Universal C Runtime are used from the
target operating system. No Windows DLLs or separate .NET runtime are bundled.

## VB-CABLE (optional, downloaded separately)

VB-CABLE is software by VB-Audio / Vincent Burel. It is not developed by
Marcus Beckerle and is not licensed under this project's license.

Origin: https://vb-audio.com/Cable/ (https://www.vb-cable.com/)

**VB-CABLE is donationware. All contributions are welcome.** Its own licensing
terms, including terms for professional use, apply independently:
https://vb-audio.com/Services/licensing.htm

The app downloads the unchanged official VBCABLE_Driver_Pack45.zip only when
the user requests installation/removal. It verifies the pinned SHA-256 hash
and the Windows signature of the installer. The original package includes its
readme and license. The driver is not bundled in the repository or release.

Changing the Windows endpoint display name to “Audio Splitter” does not change
the driver's authorship, binary files, or license.
