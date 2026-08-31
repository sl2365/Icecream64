# IceCream64

[![Release](https://img.shields.io/github/v/release/sl2365/Icecream64?style=for-the-badge-square&logo=github&logoColor=white&color=violet)](https://github.com/sl2365/Icecream64/releases/latest/download/Icecream64.rar)
[![Release Date](https://img.shields.io/github/release-date/sl2365/Icecream64?style=for-the-badge-square&logo=github&logoColor=white&color=yellow)](https://github.com/sl2365/Icecream64/releases)

[![Latest Asset Downloads](https://img.shields.io/github/downloads/sl2365/Icecream64/latest/Icecream64.rar?style=for-the-badge-square&logo=github&logoColor=white&label=downloads-latest&displayAssetName=false&color=blue)](https://github.com/sl2365/Icecream64/releases/latest)
[![Total Downloads](https://img.shields.io/github/downloads/sl2365/Icecream64/total?style=for-the-badge-square&logo=github&logoColor=white&label=downloads-total&color=blue)](https://github.com/sl2365/Icecream64/releases)

[![Commits Since Release](https://img.shields.io/github/commits-since/sl2365/Icecream64/latest?style=for-the-badge-square&logo=github&logoColor=white&color=green)](https://github.com/sl2365/Icecream64/activity)
[![Last Commit](https://img.shields.io/github/last-commit/sl2365/Icecream64?style=for-the-badge-square&logo=github&logoColor=white&color=green)](https://github.com/sl2365/Icecream64/activity)

IceCream64 is a modern 64-bit VST3 synthesizer for Windows, inspired by the original **IceCream** plug-in by **Cosmic Boy**.

This project preserves the playful character and hands-on workflow of the original while adding a resizable interface, light and dark themes, portable settings, and a number of refinements. It is a homage rather than an exact replica.

## Features

- Two oscillators with independent level and octave controls
- Multimode filter with cutoff, resonance, and keyboard tracking
- Amp and filter envelopes
- 16-step pitch and filter sequencer
- Independent attack and release smoothing for sequencer steps
- Built-in sequencer patterns plus user-loadable and user-saveable templates
- XY pad for expressive control
- Eight-band equalizer with reset control
- Reverb and delay effects
- Bitcrusher with 32-, 24-, 16-, and 8-bit character options
- Polyphonic and monophonic playing modes, glide, Harmonix, and Character controls
- On-screen keyboard with momentary and latched notes
- 32 embedded factory presets plus user preset support
- Classic Light and Modern Dark themes
- Resizable interface from 75% to 200%
- Portable presets, sequencer templates, and interface settings

## Installation

1. Download or build `IceCream.vst3`.
2. Place it in a writable VST3 folder scanned by your plug-in host.
3. Rescan VST3 plug-ins in the host, then load **ICECREAM** as an instrument.

A common system VST3 folder is:

```text
C:\Program Files\Common Files\VST3
```

IceCream stores user data in a `Data` folder beside the plug-in. If the chosen VST3 folder is protected by Windows, saving presets, sequencer templates, or settings may require suitable write permission. You can instead use a writable custom VST3 folder configured in your host.

## Using IceCream

### Presets

Use the arrow buttons in the **Patch** section, or scroll over the patch display, to move through presets one at a time. The **Menu** provides access to factory and user presets, Save, Save As, interface size, theme selection, and the About window.

The 32 factory presets are embedded in the plug-in. User presets are stored as `.ini` files in `Data\Presets`.

### Step sequencer

Draw directly in the 16-step display to create a pattern. The sequencer can modulate pitch, filter, or both:

- **FREE** switches between free-running and synchronized operation.
- **PITCH** enables pitch sequencing.
- **FILTER** enables filter sequencing.
- The rate knob selects the sequencer division.
- **A - SMOOTH - R** independently softens the attack and release transitions between steps, including the transition from the final step back to the first.
- **LOAD** opens a saved sequencer template.
- **SAVE** stores the current sequencer setup; press Enter to confirm the name.

Sequencer templates are stored in `Data\Seq`.

### On-screen keyboard

- Left-click a key to play it momentarily.
- Right-click a key to latch it.
- Click a latched key again to release it.

In Mono mode, releasing the newest note returns playback to an earlier key that is still being held.

### Themes and interface size

Open **Patch > Menu** to select **Classic Light** or **Modern Dark**, or to choose a size from 75% to 200%. The interface can also be resized with its drag handle.

The selected theme and zoom size are restored from `Data\Settings.ini` the next time the plug-in opens.

## Portable data

IceCream keeps its writable files beside the plug-in:

```text
IceCream.vst3
Data\
  Settings.ini
  Presets\
  Seq\
```

Keep the `Data` folder with the plug-in when moving an existing installation if you want to preserve user presets, sequencer templates, and interface settings. Missing folders are created when required.

## Building from source

### Requirements

The included build script currently targets **Windows x64** and expects:

- Windows 10 or later
- Visual Studio Community 2026 with the **Desktop development with C++** workload
- CMake 4.4.2
- JUCE 8.0.15
- Windows PowerShell

The versions and locations of CMake and JUCE are fixed in `- Build.bat`. Arrange the folders like this:

```text
Project folder\
  IceCream64\
    - Build.bat
    source\
  _Tools\
    cmake\
      _4.4.2\
        bin\
          cmake.exe
    JUCE\
      _8.0.15\
        CMakeLists.txt
```

### Build steps

1. Install Visual Studio Community 2026 and its C++ desktop workload.
2. Put CMake 4.4.2 and JUCE 8.0.15 in the locations shown above.
3. Double-click `- Build.bat` in the project root.
4. Wait for all five checks to report `PASS`.
5. Find the finished plug-in at `dist\IceCream.vst3`.

The script configures an x64 Release build, compiles the VST3, embeds all 32 factory presets, validates the resulting Windows x64 binary, and writes the full build output to `Results.log`. Existing user files in `dist\Data` are preserved, and the script does not install the plug-in elsewhere.

> **Note:** The build script closes `PolyHostInterface.exe` if it is running so the existing plug-in file is not locked during the build. I added this specifically because thats what I loaded Icecream.vst3 in for testing purposes. If you don't use [PolyHostInterface](https://github.com/sl2365/PolyHostInterface), it will just be ignored and compile as normal.

## Credits

- Original IceCream plug-in and concept: [Cosmic Boy](https://www.cosmicbren.com/audio-tools)
- IceCream64 v2 64-bit VST3: **sl23**
- Project source and releases on [github](https://github.com/sl2365/Icecream64)

IceCream64 is not an exact replica of the original plug-in. It is an independent homage created in appreciation of Cosmic Boy's original work. I loved the interface so much, I tried to create a modern version while keeping the originals aesthetics.
