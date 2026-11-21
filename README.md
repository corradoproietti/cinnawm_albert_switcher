# CinnaWM Switcher for Albert Launcher

Native C++23 / Qt6 plugin (`cinnawm_switcher.so`, Plugin Interface `35.1`) connecting **Albert Launcher** directly to **[CinnaWM](https://github.com/corradoproietti/window_manager)** on Cinnamon over `QtDBus`.

---

## Features

- **Fast Window & Tab Switching**: Searches and switches windows across all workspaces, monitors, **Manual Tab Groups**, **Overflow Stacks**, **Scratchpad**, **Picture-in-Picture**, and minimized states via `org.cinnawm.Service`.
- **Direct `Super + Tab` Launch & Cycling**: Exposes session D-Bus service `org.cinnawm.AlbertSwitcher` so `Super + Tab` in CinnaWM opens Albert directly with `w `; repeated `Super + Tab` (or `Tab` / `Shift + Tab`) steps cleanly from window to window and switches when `Super` is released or `Enter` is pressed.
- **Zero Python / Zero Subprocesses**: Pure C++23 with `QtDBus`, `QIcon::hasThemeIcon` + `.desktop` `StartupWMClass` resolution, and X11 `XQueryKeymap` modifier detection.
- **Configurable in Albert Preferences**: Toggle minimized windows, prioritize the active workspace, customize trigger (`w ` by default), and enable fuzzy matching.

---

## Build & Installation

Requires:
- **Albert `35.1+`** development headers & CMake package (`albert-config.cmake`)
- **Qt6** (`Core`, `DBus`, `Gui`, `Widgets`, `Test`)
- **CMake `>= 3.19`**, **C++23 compiler**, and **libX11**

```bash
git clone https://github.com/corradoproietti/cinnawm_albert_switcher.git
cd cinnawm_albert_switcher
bash install.sh
```

After installation:
1. Restart Albert so it loads `cinnawm_switcher.so`.
2. Open **Albert Settings → Plugins** and enable **CinnaWM Switcher**.

Uninstall:

```bash
bash install.sh --uninstall
```

---

## Running Unit Tests

```bash
cmake -S . -B build -DBUILD_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

*(Pass `-DBUILD_PLUGIN=ON -DCMAKE_PREFIX_PATH="$HOME/.local"` to build both the plugin and tests.)*

---

## Environment Configuration

All D-Bus and trigger identifiers can be customized via environment variables:

- `CINNAWM_DBUS_NAME` (default: `org.cinnawm.Service`)
- `CINNAWM_DBUS_PATH` (default: `/org/cinnawm/Service`)
- `CINNAWM_DBUS_INTERFACE` (default: `org.cinnawm.Service`)
- `CINNAWM_ALBERT_DBUS_NAME` (default: `org.cinnawm.AlbertSwitcher`)
- `CINNAWM_ALBERT_DBUS_PATH` (default: `/org/cinnawm/AlbertSwitcher`)
- `CINNAWM_ALBERT_TRIGGER` (default: `w `)
- `CINNAWM_DBUS_TIMEOUT_MS` (default: `1500`)
- `CINNAWM_ALBERT_PREFIX` / `CINNAWM_ALBERT_PLUGINS_DIR` (installer overrides)

---

## License

Licensed under the [GNU General Public License v3.0](LICENSE).
