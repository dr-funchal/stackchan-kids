# Contributing to stackchan-kids

Thanks for helping make StackChan a better friend for kids! Contributions of all sizes are welcome:
new games and stories, avatar animations, translations, bug fixes, docs, and testing on real hardware.

## Ways to help

- **Report bugs**: open an issue with what you did, what happened, and the serial log (see [Debugging](#debugging-crashes)).
- **Suggest ideas**: games, stories, expressions, or anything that would delight a child.
- **Send code**: fork, create a branch, and open a pull request against `main`.
- **Test**: try a PR on your own StackChan and report back.

Issues labelled `good first issue` are a good place to start.

## Project layout

Work happens in [`firmware/`](firmware) (ESP-IDF, C++). `app/` (Flutter), `server/` (Go) and `remote/`
(ESP-NOW remote) come from upstream and are separate projects.

| Path | What lives there |
|---|---|
| `firmware/main/main.cpp` | Starts the HAL, installs launcher apps, runs the main loop, hands off to the AI agent |
| `firmware/main/apps/` | Launcher apps (mooncake + LVGL). New app: copy `app_template/`, register in `apps/apps.h` and `main.cpp` |
| `firmware/main/stackchan/` | The character: `modifiers/` (blink, breath, dance, party…), `avatar/` (eyes, mouth, decorators), `motion/`, LEDs |
| `firmware/main/hal/` | Hardware: servos, touch, IMU, IR, SD card, MCP tools for the agent, display/audio board glue |
| `firmware/main/hal/utils/` | Kids' features: Papa-Letras, Panda Mandou, stories |
| `firmware/patches/xiaozhi-esp32.patch` | Our changes to the [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) AI agent (v2.2.4) |

## Setting up

You need an M5Stack StackChan (CoreS3, ESP32-S3, 16 MB flash, 8 MB PSRAM) to test on hardware,
but you can build and run the host tests without one.

1. Install **ESP-IDF v5.5.4** ([guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/)).
   This is not a PlatformIO project.
2. Activate the environment in each new shell:
   ```bash
   . ~/esp/esp-idf-v5.5.4/export.sh
   ```
   On macOS with the python.org Python, also `export SSL_CERT_FILE=/etc/ssl/cert.pem` first.
3. Fetch dependencies (creates `components/` and `xiaozhi-esp32/` and applies the patch):
   ```bash
   cd firmware
   python3 fetch_repos.py
   ```

## Building and testing

From `firmware/`:

```bash
idf.py build                          # compile
idf.py -p <PORT> app-flash            # flash only the app (fast, ~25 s)
idf.py -p <PORT> flash                # full flash (bootloader, partitions, app, assets)
idf.py -p <PORT> monitor              # serial monitor (exit: Ctrl+])
```

Host tests (motion math, run on your computer):

```bash
cmake -S tests -B build-host-tests && cmake --build build-host-tests && ctest --test-dir build-host-tests --output-on-failure
```

> ⚠️ Before flashing, back up your robot's factory firmware (see the [official docs](https://docs.m5stack.com/en/StackChan)
> or M5Burner). **Never run `idf.py erase-flash`**: it wipes NVS (Wi-Fi, account, calibration).
> Before a full `flash`, check that `build/generated_assets.bin` is **≤ 4 MB**; larger assets overwrite
> the coredump partition and break icons.

## Gotchas

- `firmware/xiaozhi-esp32/` and `firmware/components/` are git-ignored and recreated by `fetch_repos.py`.
  Changes there must go into `patches/xiaozhi-esp32.patch` (or `repos.json`), or they are lost.
- `main/CMakeLists.txt` uses `GLOB_RECURSE`: after **adding** a `.cpp` file, run `idf.py reconfigure`.
- Local settings (server URL, OTA, etc.) go in `sdkconfig.defaults.local`, which is git-ignored.
- Many upstream code comments are in Chinese. New comments may be in English.

## Pitfalls that crashed the robot before

Heap corruption usually shows up as a reboot with a backtrace in an audio task (`AudioInputTask`,
`OpusCodecTask`, `AfeWakeWord`). Those tasks are **victims**; the real culprit wrote out of bounds earlier.
Look at the most recent change.

- **Never animate `lv_image_set_scale` from 0.** Start at ≥ 40 (`LV_SCALE_NONE` = 256 = 100%).
- **Sounds (`PlaySound`) must be Ogg Opus SILK wideband, 60 ms, one frame per packet**, like
  `xiaozhi-esp32/main/assets/common/*.ogg`. Generate them with:
  `opusenc --framesize 60 --bitrate 16 --set-ctl-int 4024=3001 --set-ctl-int 4008=1103 --set-ctl-int 4004=1103`.
- **Do not play a sound when the robot starts listening** (in `SetStatus()` or scheduled). Sounds while
  speaking are fine. Use a visual cue for "you can talk".
- **Keep the Opus encoder `complexity` at 0**; higher values overflow the codec task stack.
- **The SD card and the display share SPI3.** Wrap every SD file operation in `sd_card::BusGuard`.
- **MCP tools run on the main task**: never block there (network, IR, large file reads); spawn a task
  with `xTaskCreatePinnedToCoreWithCaps(..., MALLOC_CAP_SPIRAM)`.
- **Internal RAM is scarce**: put new task stacks and large buffers in PSRAM.
- `ObjectPool` reuses modifier IDs: don't keep the ID of a self-destroying modifier.

## Debugging crashes

- Opening or closing the serial port **resets** the robot; capture a long log rather than short reads.
- Decode a backtrace:
  ```bash
  xtensa-esp32s3-elf-addr2line -pfiaC -e build/stack-chan.elf <addresses>
  ```

## Pull request checklist

- [ ] `idf.py build` passes, and host tests pass if you touched motion code.
- [ ] Tested on a real StackChan (say so in the PR), or clearly marked as untested.
- [ ] Changes to `xiaozhi-esp32/` are in `patches/xiaozhi-esp32.patch`.
- [ ] `python3 scan_secrets.py` (in `firmware/`) shows no real secrets. Never commit Wi-Fi credentials,
      tokens, `sdkconfig.defaults.local` or flash dumps.
- [ ] Content is appropriate for young children.

## License

By contributing, you agree that your contributions are licensed under the [MIT license](LICENSE) of this
fork. Upstream M5Stack code is not relicensed; see the disclaimer in the [README](README.md).
