<div align="center">
  <a>
    <img src="docs/flex-launcher.svg" alt="Logo" width="150" height="150">
  </a>


# Reflex Launcher
</div>

> **Reflex Launcher is a fork of [Flex Launcher](https://github.com/complexlogic/flex-launcher) by complexlogic.** Where upstream is in maintenance mode, Reflex Launcher is under active development and adds new features on top of it, starting with macOS support. The executable, package, and config paths still use the original `flex-launcher` name.

<details open>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about">About</a>
    </li>
    <li>
      <a href="#screenshots">Screenshots</a>
    </li>
    <li>
      <a href="#installation">Installation</a>
      <ul>
        <li><a href="#windows">Windows</a></li>
        <li><a href="#linux">Linux</a></li>
        <li><a href="#macos">macOS</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
      <ul>
        <li><a href="#controls">Controls</a></li>
        <li><a href="#debugging">Debugging</a></li>
      </ul>
    <li><a href="#development-status">Development Status</a></li>
    <li><a href="#documentation">Documentation</a></li>
    <li><a href="#credits">Credits</a></li>
  </ol>
</details>

## About
Reflex Launcher is a customizable application launcher and front end designed with a TV-friendly [10 foot user interface](https://en.wikipedia.org/wiki/10-foot_user_interface), intending to mimic the look and feel of a streaming box or game console. Reflex Launcher allows you to launch applications on your HTPC or couch gaming PC entirely by use of a TV remote or a gamepad. No keyboard or mouse required!

Reflex Launcher is compatible with Windows, Linux (including Raspberry Pi devices), and macOS.

## Screenshots
![Screenshot 1](docs/assets/screenshots/screenshot1.png "Screenshot 1")

https://user-images.githubusercontent.com/95071366/208355237-11f00cbb-9cc3-436b-98f7-8de350e584a7.mp4

## Installation
Prebuilt packages are available for Windows 64 bit, Debian/Ubuntu x86-64, and Raspberry Pi on the [latest release](https://github.com/davetait/reflex-launcher/releases/latest). macOS and other Linux distributions must compile from source using the [compilation guide](docs/compilation.md).

### Windows
Download the win64 .zip file from the [latest release](https://github.com/davetait/reflex-launcher/releases/latest) and extract the contents to a directory of your choice. The launcher should be run on an up-to-date Windows 10 system, or Windows 11.

### Linux
#### APT-based x86-64 Distributions (Debian, Ubuntu, etc.)
This package is compatible with Debian Bullseye and later, Ubuntu 21.04 and later.
```bash
wget https://github.com/davetait/reflex-launcher/releases/download/v2.2-reflex1/flex-launcher_2.2_amd64.deb
sudo apt install ./flex-launcher_2.2_amd64.deb
```
#### Raspberry Pi
This package is compatible with Raspberry Pi OS Bullseye and later, 64 bit only.
```bash
wget https://github.com/davetait/reflex-launcher/releases/download/v2.2-reflex1/flex-launcher_2.2_arm64.deb
sudo apt install ./flex-launcher_2.2_arm64.deb
```
#### Other Distributions (Arch, Fedora, etc.)
No package is published. Install the dependencies listed in the [Linux compilation guide](docs/compilation.md#linux), then build:
```bash
git clone https://github.com/davetait/reflex-launcher.git
cd reflex-launcher
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```
#### Copying Assets to Home Directory
The packages install a default config file and assets to `/usr/share/flex-launcher` (`/usr/local/share/flex-launcher` when built from source). It is strongly recommended to *not* edit this config file directly, as it will be overwritten if you upgrade to a later version. Instead, copy these files to your home directory and edit it there. For a source install, replace `/usr/share` with `/usr/local/share` below.
```bash
cp -r /usr/share/flex-launcher ~/.config
sed -i "s|/usr/share/flex-launcher|$HOME/.config/flex-launcher|g" ~/.config/flex-launcher/config.ini
```

### macOS
Install [Homebrew](https://brew.sh) and the Xcode Command Line Tools (`xcode-select --install`), then install the dependencies:
```bash
brew install cmake pkgconf sdl2 sdl2_image sdl2_ttf inih
```
Clone the repo and build:
```bash
git clone https://github.com/davetait/reflex-launcher.git
cd reflex-launcher
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/flex-launcher
```
Optionally, install the program, default config file, and assets under `/usr/local`, then copy them to your home directory for editing (same reasoning as on Linux):
```bash
sudo cmake --install build
cp -r /usr/local/share/flex-launcher ~/.config
sed -i '' "s|/usr/local/share/flex-launcher|$HOME/.config/flex-launcher|g" ~/.config/flex-launcher/config.ini
```
#### macOS Notes
- Linux `.desktop` files are not supported. Launch applications with `open`, e.g. `Entry1=Kodi;/path/to/kodi.png;open -a Kodi`.
- The `:shutdown` and `:restart` commands use AppleScript. The first time you use one, macOS asks for permission to control "System Events". `:sleep` uses `pmset sleepnow`.
- See the [compilation guide](docs/compilation.md#macos) for more detail.

## Usage
Reflex Launcher uses an INI file to configure the menus and settings. Upon  startup, the program will search for a file named `config.ini` in the following locations in order:
1. The current working directory
2. The directory containing the `flex-launcher` executable
3. Linux and macOS only: `~/.config/flex-launcher`
4. Linux and macOS only: `/usr/share/flex-launcher` for the packages, `/usr/local/share/flex-launcher` when built from source

If your config file is in one of the above locations, the launcher can be started simply by double clicking the executable file or adding it to autostart. If your config file is in a non-standard location, you must specify the path via command line argument:
```bash
flex-launcher -c /path/to/config.ini
```
Reflex Launcher ships with a default config file which is intended strictly for demonstration purposes. If you try to start one of the applications, it is possible that nothing will happen because the install path is different on your system, or you don't have the application installed at all. See the [configuration file documentation](docs/configuration.md#configuring-flex-launcher) for instuctions on how to change the menus and settings.

### Controls
The keyboard arrow keys move the highlight cursor left and right. Enter selects the current entry, backspace goes back to the previous menu (if applicable), and Esc quits the program. 

#### TV Remotes
Reflex Launcher does not feature built-in decoding of IR or CEC signals. If you plan to use a TV remote to control the device, it is assumed that these signals are decoded by the OS or another program and mapped to keyboard presses, which can then be received by Reflex Launcher.

#### Gamepads
Gamepad controls are built-in to the program, but are disabled by default. To enable them, open your configuration file and, under the "Gamepad" section, change the "Enabled" setting from false to true. After that, the gamepad controls should "Just Work" for most users. If your gamepad is not recognized automatically, or you want to change the default controls, see the [gamepad controls documentation](docs/configuration.md#gamepad-controls).

### Settings
The default menu has a Settings entry that opens an in-app screen for changing every scalar setting in `config.ini` with a remote, gamepad or keyboard. Saving applies the changes immediately. Menus and hotkeys are still edited in the file. See the [configuration documentation](docs/configuration.md#settings-screen) for the controls.

### Debugging
Reflex Launcher has a debug mode which may be enabled as follows:
```bash
flex-launcher -d
```
This will output a logfile named `flex-launcher.log` in the same directory as `flex-launcher.exe` on Windows, and in `~/.local/share/flex-launcher` on Linux and macOS. 

## Development Status
Upstream Flex Launcher has reached a mature state and is maintained for bugfixes and dependency updates only; its author is focusing on a [new HTPC launcher project](https://github.com/complexlogic/big-launcher). Reflex Launcher continues active development on top of upstream, adding new features and platform support (starting with macOS).

## Documentation
Here is a list of available documentation:
- [Configuration](docs/configuration.md#configuring-flex-launcher)
- [General Setup Guide](docs/setup.md#setup-guide)
  - [Windows-specific Setup Guide](docs/setup_windows.md#windows-setup-guide)
  - [Linux-specific Setup Guide](docs/setup_linux.md#linux-setup-guide)
- [Compilation Guide](docs/compilation.md#compilation-guide)

## Credits
Reflex Launcher is a fork of [Flex Launcher](https://github.com/complexlogic/flex-launcher) by complexlogic, and is made possible by the following projects:
- [SDL](https://github.com/libsdl-org/SDL), including the subprojects:
  - [SDL_image](https://github.com/libsdl-org/SDL_image)
  - [SDL_ttf](https://github.com/libsdl-org/SDL_ttf)
- [Nanosvg](https://github.com/memononen/nanosvg)
- [inih](https://github.com/benhoyt/inih)
- [Numix icons](https://github.com/numixproject)

The design of Flex Launcher, and therefore Reflex Launcher, was strongly influenced by the excellent desktop application launcher [xlunch](https://github.com/Tomas-M/xlunch).
