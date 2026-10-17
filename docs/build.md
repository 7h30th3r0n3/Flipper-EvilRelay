# Build and flash: full details

Three independent builds: the Flipper FAP, the ESP32 bridge, and the Android APK.

## 1. Flipper FAP (`flipper-fap/`)

Requirement: a Flipper firmware with the modern NFC stack that exposes
`iso14443_3a_listener`. The FAP is developed against the Momentum (mntm) SDK. It calls
internal NFC-listener APIs, so another firmware fork may need the matching `ufbt` SDK
channel (`ufbt update -b <channel>`) or small API tweaks. It is built with
[`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt), the micro Flipper Build Tool.

`flipper-fap/` already contains `application.fam`, so `ufbt` builds it in place, no copying:

```bash
pip install --user ufbt     # once
ufbt update                 # pulls the SDK for your firmware channel (e.g. -b dev)
cd flipper-fap
ufbt                        # builds -> dist/evil_relay.fap
ufbt launch                 # build, upload and launch on a connected Flipper
```

`application.fam` sets appid `evil_relay`, name `EvilRelay ISO-DEP`, `fap_category="NFC"`.
Read the FAP logs with the Flipper CLI command `log debug`. The FAP appears under NFC.

Serial note. The Flipper USART (GPIO 13/14) is also the console and log port. The FAP
reconfigures the logging path before acquiring the USART and restores the console baud
(`CONSOLE_LOG_BAUD = 230400`) on exit. The relay data baud to the ESP32 is
`RELAY_UART_BAUD = 115200`, and that is the one that must match the sketch.

## 2. ESP32 bridge (`esp32-bridge/`)

Any ESP32. Only the ESP32 Arduino core is needed; `WiFi`, `esp_now` and `esp_wifi` are
bundled. The reference board is an ESP32-C3 SuperMini.

### arduino-cli

```bash
# one-time: install arduino-cli (https://arduino.github.io/arduino-cli), then the ESP32 core
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

arduino-cli compile --fqbn esp32:esp32:esp32c3 esp32-bridge
arduino-cli upload  --fqbn esp32:esp32:esp32c3 -p /dev/ttyACM0 esp32-bridge
```

The port is `/dev/ttyACM0` on Linux, `COMx` on Windows, `/dev/cu.usbmodem*` on macOS
(`arduino-cli board list` finds it).

For an M5Stamp C3 with the M5Stack core, use `--fqbn m5stack:esp32:m5stack_stamp_c3`.

### Arduino IDE

Open `esp32-bridge/EvilRelayESP32.ino`, select your ESP32 board, upload.

Debug serial is 115200 on the C3 USB. It prints the SoftAP IP (`192.168.4.1`) and link
events. Pins are `UART_RX_PIN = GPIO1` and `UART_TX_PIN = GPIO0`; edit them for other
boards.

## 3. Android reader (`android-reader/`)

### Prebuilt APK

Copy `android-reader/EvilRelay-v1.12.apk` to the phone and install it (allow install from
unknown apps for your file manager). `adb` is not required.

### From source

Open `android-reader/src/` in Android Studio and run, or build with Gradle:

```bash
cd android-reader/src
./gradlew assembleDebug          # or: gradle assembleDebug
# output: app/build/outputs/apk/debug/app-debug.apk
```

`versionName 1.8`, `versionCode 9`, `minSdk 21`, `targetSdk 34`. Permissions are NFC plus
Internet and Wi-Fi/network state for the relay socket; no Bluetooth, no location, no
storage. Modes are Reader (Wi-Fi, full ISO-DEP, real ATS) and Emulator (HCE, for
phone-to-phone tests). A "Keep screen on" switch holds `FLAG_KEEP_SCREEN_ON`.

The HCE Emulator mode registers payment AIDs for testing, so it can conflict with wallet
apps on the device and is not distributable on Google Play. Sideload the debug APK only.

## 4. First smoke test

1. Flash the ESP32, power it, confirm its debug serial prints `192.168.4.1`.
2. Flash and open the FAP. It shows "Waiting reader (UART)".
3. If you own a Proxmark3, present the Flipper to it first. `hf 14a info` shows the
   relayed UID and ATS, and `hf emv` walks the APDUs. This validates the chain before you
   touch a real terminal.
4. Only then try an authorised real reader or terminal.
