<p align="right">
  <a href="README.md">🇷🇺 Перейти к русской версии</a>
</p>

# MandaGram Shabbat Enforcer Plugin (C ABI) v1.0.0

A native **C** plugin for the **MandaGram** client ([mandagram.com](https://mandagram.com)) that enforces Shabbat observance: gracefully closing the messenger during holy hours to ensure total rest and peace of mind.

Built in strict compliance with the **MandaGram C ABI SDK** specification (Java Foreign Function & Memory API bridge).

---

## 🕊️ Key Features

* **Startup Gatekeeper**: If MandaGram is launched during Shabbat, the plugin immediately presents a warning notification and terminates the application before any chats or network connections load.
* **Background Watcher**: A lightweight background watchdog thread checks the local system time every 15 seconds. The moment Shabbat begins, a prompt notification is displayed and the client exits cleanly.
* **Cross-Platform Support**:
  * **Windows**: Native system modal dialog (`MessageBoxA` with `MB_SYSTEMMODAL` priority) and clean termination via `ExitProcess`.
  * **Linux**: Desktop alert via `zenity` (with fallback to `stderr`) and multithreading via POSIX `pthread`.
* **Instant Debug Mode**: Single preprocessor flag to simulate Shabbat on demand for development and testing.

---

## ⏰ Default Shabbat Schedule

| Day of Week | Time | Status |
| :--- | :--- | :--- |
| **Friday** | Starting at **18:00 (6:00 PM)** | Shabbat begins (access blocked, client exits) |
| **Saturday** | All day until **21:30 (9:30 PM)** | Shabbat continues (access blocked) |
| **Saturday** | After **21:30 (9:30 PM)** | Shabbat ends (access restored) |
| **Sunday – Thursday** | 24 / 7 | Normal operation |

Schedule configuration can be adjusted directly at the top of [`src/shabbat_plugin.c`](src/shabbat_plugin.c):
```c
#define SHABBAT_START_WDAY 5   /* Friday */
#define SHABBAT_START_HOUR 18  /* 18:00 */
#define SHABBAT_START_MIN  0

#define SHABBAT_END_WDAY   6   /* Saturday */
#define SHABBAT_END_HOUR   21  /* 21:30 */
#define SHABBAT_END_MIN    30
```

---

## 🧪 Testing & Debug Mode

To verify the plugin behavior immediately without waiting for Friday evening:

1. Open [`src/shabbat_plugin.c`](src/shabbat_plugin.c).
2. Uncomment the debug definition:
   ```c
   #define FORCE_SHABBAT_DEBUG 1
   ```
3. Rebuild the plugin (`make`). When launched in MandaGram, the client will immediately trigger the Shabbat exit prompt (*"Shabbat Shalom! Telegram is strictly forbidden during Shabbat."*).

---

## 📁 Repository Structure

```text
mandagram-shabbat-plugin/
├── Makefile                # Build targets for Linux (.so) and Windows (.dll)
├── README.md               # Russian documentation (default)
├── README_EN.md            # English documentation
├── .gitignore              # Build artifacts exclusion
├── include/
│   └── mandagram_plugin.h  # MandaGram C ABI header definitions
├── src/
│   └── shabbat_plugin.c    # Shabbat Enforcer implementation in C
└── out/
    ├── shabbat_plugin.so   # Compiled Linux x86_64 shared library
    └── shabbat_plugin.dll  # Compiled Windows x64 DLL
```

---

## 🔨 Building

### Prerequisites
* `gcc` (for Linux builds).
* `mingw-w64` (`x86_64-w64-mingw32-gcc`, for Windows cross-compilation).

### Commands

Build shared library for Linux (`out/shabbat_plugin.so`):
```bash
make linux
```

Build dynamic library for Windows (`out/shabbat_plugin.dll`):
```bash
make windows
```

Build both platforms at once:
```bash
make both
```

Clean build artifacts:
```bash
make clean
```

---

## 📦 Installation in MandaGram

1. Build the library targeting your OS (`.so` for Linux or `.dll` for Windows).
2. Copy the resulting binary into the MandaGram plugin directory:
   * **Linux**: `~/.local/share/mandagram/plugins/` (or the `plugins/` directory adjacent to the MandaGram executable)
   * **Windows**: `%APPDATA%\MandaGram\plugins\` (or the `plugins/` directory adjacent to `MandaGram.exe`)
3. Start MandaGram. The client log will confirm initialization:
   ```text
   [Shabbat] Плагин контроля Шаббата инициализирован.
   ```

---

## 📜 License

GNU General Public License v3.0 (GPL-3.0). Shabbat Shalom!
