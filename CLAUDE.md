# Open_Cart: notes for Claude

ESP32-S3 + ADS1220 load cell scale. PlatformIO + ESP-IDF. Repo: https://github.com/53Aries/Open_Cart (branch `main`).
Hardware and design: `DESIGN.md`. Usage and test status: `README.md`.

## Build
- `pio run -e s3` (Wi-Fi web + scoreboard). `pio run -e s3_isobus` is the opt-in ISOBUS build.
- `pio` is at `~/.platformio/penv/Scripts/pio.exe` (not on PATH in the Bash tool). A build takes about 90 s.
- Flash over USB: `pio run -e s3 -t upload`. Needed whenever the running firmware cannot accept the new one over Wi-Fi.

## Versioning (automatic)
- `version.txt` holds the version (`1.10`). `scripts/version.py` bumps the minor number on EVERY `pio run`
  (including failed builds and uploads, so numbers can skip). `CMakeLists.txt` reads it into `PROJECT_VER`.
- The major number only changes when the user edits `version.txt`. Writing `2.0+` makes the next build exactly 2.0.
- Do not edit `version.txt` otherwise, and do not run extra builds just to check things; each one burns a number.
- The web page shows "Running version: X". The build date/time inside the image is NOT reliable (IDF only
  refreshes it when one small file recompiles), which is why the version number is used instead.
- Each build also writes `.pio/build/<env>/open_cart_v<version>.bin`, the file to upload on the Wi-Fi update page.

## Releasing (user's flow, ask before doing it)
1. `pio run -e s3`, flash and test.
2. Commit code changes (the release script refuses to run with other uncommitted files).
3. `python scripts/release.py`: commits `version.txt`, pushes, creates GitHub Release `v<version>` with the bin attached.
- Binaries are NOT committed to git; they live in Releases only.
- `gh` was installed with winget (user scope). `release.py` finds it even when it is not on PATH. For manual use in
  Bash: `C:/Users/colla/AppData/Local/Microsoft/WinGet/Packages/GitHub.cli_Microsoft.Winget.Source_8wekyb3d8bbwe/bin/gh.exe`.
- The user is logged in as `53Aries`. Never push, release or tag unless asked.

## Wi-Fi firmware update rules (`src/web_server.cpp`, `h_fw_post`)
- Uploaded file must be named `open_cart_v<something>.bin` and be the app image (`firmware.bin`), never `firmware.factory.bin`.
- The running firmware rejects any image whose embedded project name differs from its own (`Open_Cart`, set by
  `project()` in `CMakeLists.txt`). Renaming the project means the next update has to be flashed over USB once.
  (This already happened once: OpenScale -> Open_Cart.)
- The restart task needs a 4 KB stack; at 2 KB the scale accepted the update but never rebooted.

## Repo conventions
- Generated/downloaded stuff is git-ignored: `.pio`, `managed_components/`, `sdkconfig.s3*`, `.vscode/`. Edit
  `sdkconfig.defaults` for config changes (delete the generated `sdkconfig.s3*` if a change does not take effect).
- `src/config.h` has the default AP password `changeme123`; the repo may be public.
- Line endings: Windows checkout, git warns about LF/CRLF. Harmless.
- Git commits end with the `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` line.
