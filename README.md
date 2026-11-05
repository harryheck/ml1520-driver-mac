# Samsung ML-1520 (ML1520) driver for Mac

Print on a Samsung ML-1520 laser printer from a current Mac. Samsung's Mac drivers are
Intel-only and have no ML-1520 profile. This project needs no Rosetta, no PPD driver and no
administrator password.

This is an unofficial project, not affiliated with Samsung, HP or Apple.

**Tested with:** macOS 27.0 on Apple Silicon and one ML-1520 connected over USB. Other setups
are untested.

## Requirements

- A Mac with Apple Silicon. Intel Macs are not supported.
- A Samsung ML-1520 connected over USB. Other printer models will not work.

## Install

1. Download `ml1520-driver-mac-1.0.0-arm64.zip` from the
   [Releases](../../releases) page and unzip it.
2. Open Terminal, type `cd ` with a space at the end, drag the unzipped folder into the
   window and press Return.
3. Run:

       ./install.sh

The printer now appears as **Samsung ML-1520** in every print dialog.

macOS may tell you that a background item was added. Leave it enabled under
**System Settings > General > Login Items**, because the printer stops working without it.

The program is not signed with an Apple Developer ID. `install.sh` removes the download
quarantine flag from its installed copy so that the background service can run it. All source
code is in this repository if you prefer to [build it yourself](#build-from-source).

## Uninstall

    ./uninstall.sh

This removes the printer, the background service and all installed files.

## Limitations

- **Cancelling:** you can only cancel a job that has not reached the printer yet. A running job
  prints to the end, because `ippeveprinter` does not pass the cancellation on.
- **Login:** printing only works while you are logged in.
- **Paper source:** fixed to automatic, with no manual feed setting.
- **Toner level:** not shown.
- **Landscape PDFs from the command line:** `lp` does not rotate them, use `lp -o fit-to-page`.

If the printer is off, a job waits up to 5 minutes for it. If the printer stops accepting data,
for example because of a paper jam, the job is aborted after 30 minutes.

## How it works

    App -> macOS -> ippeveprinter -> ml1520-print -> USB -> ML-1520

PPD drivers and CUPS backends are deprecated. Apple's manual pages name `ippeveprinter` as the
replacement for printers that do not speak IPP, so this project follows that route:

- **`ippeveprinter`** is part of macOS. It runs as a per-user LaunchAgent and presents the
  ML-1520 to macOS as a driverless printer. It only accepts connections from your own Mac,
  on port 18631.
- **`ml1520-print`** turns the raster pages from macOS into the printer's QPDL language and
  sends them directly over USB with libusb. Its output matches SpliX byte for byte.

Installed files: `~/Library/ML1520/`, `~/Library/LaunchAgents/local.ml1520.printer.plist` and
the log `~/Library/Logs/ML1520.log`. Your Samsung drivers are left untouched, if any are
installed.

## Troubleshooting

    launchctl print gui/$(id -u)/local.ml1520.printer | head -20
    tail ~/Library/Logs/ML1520.log
    lpstat -p Samsung_ML_1520

Running `./install.sh` again sets everything up from scratch.

## Build from source

You need the Xcode Command Line Tools and libusb from Homebrew:

    xcode-select --install
    brew install libusb
    ./build.sh
    tests/run.sh
    ./install.sh

`tests/run.sh` needs no printer. It checks the output for a real macOS print job against SpliX.
`tools/decode-qpdl.py` turns printer data back into PNG images, and `tools/make-testpage.py`
creates a test page with 100 mm rulers.

## Credits and license

Licensed under the GNU General Public License version 2, see `LICENSE`.

- [SpliX](https://github.com/OpenPrinting/splix) by Aurélien Croc and contributors did the hard
  work of documenting this printer language. The compressor in `third_party/splix/` is unmodified
  SpliX 2.0.2 code, and `ml1520-print` follows SpliX for everything else (GPL-2.0).
- [libusb](https://libusb.info) is linked statically into release builds (LGPL-2.1-or-later).
  Each release includes the matching source, see `third_party/libusb/`.

This software comes with no warranty.
