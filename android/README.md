# Child Beeper for Android

A small native Java app for Android 8.0+ (API 26). It uses platform UI/networking
APIs, without third-party runtime dependencies.

## Build and install

Install JDK 17 and Android SDK platform 35. Set `ANDROID_HOME` to your local SDK
directory (or set `sdk.dir` in an untracked `local.properties`).

From this directory:

```sh
./gradlew assembleDebug lintDebug
adb devices
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.childbeeper/.MainActivity
```

If more than one device is attached, add `-s <serial>` to each adb command.
Enable Developer options → USB debugging on the phone and accept the computer's
authorization prompt. Some Xiaomi builds also require **Install via USB**.
The Gradle 8.13 wrapper is included and its distribution checksum is pinned.

## Use

Connect the phone and beeper to the same Wi-Fi LAN. Open **Child Beeper**, use
**Find on Wi-Fi** (or enter the IP displayed on the beeper), then enter its
six-digit pairing code and tap **Connect**.

Send text in **Messages**. The beeper's button replies appear in the same
conversation. **Set daily alarm** saves and enables an alarm on the board;
the next button toggles it off/on. **Stop alarm / mark read** acknowledges the
beeper's alert.

Time and the local UTC offset synchronize automatically. Keep the app visible
to receive live updates; returning to it retrieves the latest 16 messages still
held by the board. Wi-Fi requests run off the UI thread and bind to the Wi-Fi
network, so cellular fallback does not redirect them.

See [the firmware usage and protocol guide](../docs/messaging.md) for button
controls, time behavior, network requirements and limits.
