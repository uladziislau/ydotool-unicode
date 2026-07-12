# ydotool-unicode

**Fork of [ReimuNotMoe/ydotool](https://github.com/ReimuNotMoe/ydotool)** — the same great Linux command-line automation tool, with one addition: **proper Unicode input via libxkbcommon**.

`ydotool` is a generic Linux command-line automation tool (no X!) — keyboard input, mouse control, and more. It works on Wayland, X11, text console, and anything that accepts evdev input.

**`ydotool` is not limited to Wayland.** You can use it on anything as long as it accepts keyboard/mouse/whatever input. For example, X11, text console, "RetroArch OS", fbdev apps (fbterm/mplayer/SDL1/LittleVGL/Qt Embedded), etc.

## What's different in this fork?

### XKB-based Unicode (Cyrillic, Chinese, etc.)

The original `ydotool type` uses a hardcoded `ascii2keycode_map[128]` — only supports ASCII characters (0–127). Any Unicode character beyond ASCII is silently dropped.

This fork replaces that with a **libxkbcommon**-based lookup:

1. Input is decoded as UTF-8 (multi-byte sequences are properly handled)
2. Each Unicode codepoint is resolved via `xkb_utf32_to_keysym()` → keymap iteration → evdev keycode
3. Shift modifiers are applied automatically based on keyboard layout levels
4. Fallback: if XKB resolution fails, the character is typed via Ctrl+Shift+U hex input

**This means you can now type Russian, Chinese, Arabic, or any Unicode character** using `ydotool type`.

This was developed for integration with [Handy](https://github.com/cjpais/Handy) — a voice-to-text application — to enable Russian ASR output to be typed directly into any application.

### Build dependency

This fork adds one build dependency: `libxkbcommon` (development headers).

On Fedora:
```bash
sudo dnf install libxkbcommon-devel
```

On Debian/Ubuntu:
```bash
sudo apt install libxkbcommon-dev
```

---

## Upstream

This project is a fork of **[ReimuNotMoe/ydotool](https://github.com/ReimuNotMoe/ydotool)** — an excellent piece of work. We have deep respect for the original author and the community behind ydotool.

All changes in this fork are clearly marked in git history and follow the original project's coding style.

**License:** AGPL v3 (inherited from upstream)

### About the original author

The upstream ydotool project is maintained by ReimuNotMoe (and contributors) — independent open source developers from China. They provide this tool to the community for free, and they ask for nothing more than respect and, if you can, support. Please consider [donating to the upstream project](https://www.patreon.com/classicoldsong) if ydotool helps you.

The upstream README includes important context about open source sustainability that we encourage everyone to read.

---

## Usage

Currently implemented command(s):
- `click` - Click on mouse buttons
- `mousemove` - Move mouse pointer to absolute position
- `type` - Type a string (now with Unicode support!)
- `key` - Press keys
- `debug` - Print the socket, number of parameters and parameter values
- `bakers` - Show the honorable bakers
- `stdin` - Sends the key presses as it was a keyboard (i.e from ssh) See [PR #229](https://github.com/ReimuNotMoe/ydotool/pull/229)

### Examples

Type Russian text (works because of XKB Unicode support):
```bash
ydotool type 'Привет, мир!'
```

Type a mix of scripts:
```bash
ydotool type 'Hello, Привет, 你好, мир!'
```

Switch to tty1 (Ctrl+Alt+F1), wait 2 seconds, and type some words:
```bash
ydotool key 29:1 56:1 59:1 59:0 56:0 29:0; sleep 2; ydotool type 'echo Hey guys. This is Austin.'
```

Close a window in graphical environment (Alt+F4):
```bash
ydotool key 56:1 62:1 62:0 56:0
```

## Notes

#### Runtime
`ydotoold` (daemon) program requires access to `/dev/uinput`. **This usually requires root permissions.**

#### Available key names
See `/usr/include/linux/input-event-codes.h`

#### Why a background service is needed
ydotool works differently from xdotool. xdotool sends X events directly to X server, while ydotool uses the uinput framework of Linux kernel to emulate an input device.

When ydotool runs and creates a virtual input device, it will take some time for your graphical environment (X11/Wayland) to recognize and enable the virtual input device. (Usually done by udev)

So, if the delay was too short, the virtual input device may not get recognized & enabled by your graphical environment in time.

In order to solve this problem, a persistent background service, ydotoold, is made to hold a persistent virtual device, and accept input from ydotool.

Since v1.0.0, the use of ydotoold is mandatory.

## Build

**CMake 3.22+ and libxkbcommon are required.**

### Install dependencies

Fedora/RHEL:
```bash
sudo dnf install cmake libxkbcommon-devel scdoc
```

Debian/Ubuntu:
```bash
sudo apt install cmake libxkbcommon-dev scdoc
```

### Compile

```bash
mkdir build
cd build
cmake ..
make -j `nproc`
```

### Build options
- `BUILD_DOCS=ON|OFF` — whether to build the documentation, depends on `scdoc`. Default: ON
- `SYSTEMD_USER_SERVICE=ON|OFF` — whether to use systemd user service file. Default: ON
- `SYSTEMD_SYSTEM_SERVICE=ON|OFF` — whether to use systemd system service file. Default: OFF

## Troubleshooting

### Keyboard layout awareness

This fork tries ALL configured keyboard layout groups when mapping Unicode characters to keycodes. For example, if your system is configured with both US and Russian layouts, a Russian character will be matched against the Russian layout group.

However, **the typed output depends on the currently active keyboard layout** — if you type a Russian character but have US layout active, the keycode produced will be interpreted according to the US layout. This is a fundamental limitation of the evdev keycode approach.

**Workaround:** Use the Shift+Insert (clipboard) method for fully layout-independent input, or ensure the correct layout is active when typing.

### Custom keyboard layouts

See the upstream section in the original [README.md](https://github.com/ReimuNotMoe/ydotool) for guidance on Sway and Hyprland per-device configs.

### /dev/uinput permissions

For non-root usage:
1. Add yourself to the `input` group: `sudo usermod -aG input $USER`
2. Create a udev rule: `KERNEL=="uinput", GROUP="input", MODE="0660"`
3. Run `ydotoold` via `sg input -c ydotoold` or use a systemd user service

---

## Transparency

This fork was created with assistance from [opencode](https://opencode.ai), an AI coding assistant. All code changes have been reviewed and tested by a human maintainer. We believe in transparency about tooling — the git history fully documents what was changed and how.

If you find this fork useful, please star it and consider supporting the upstream project.
