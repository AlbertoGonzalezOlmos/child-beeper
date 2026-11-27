# Wi-Fi messenger, clock and alarm

The root application runs on the Waveshare ESP32-S3-LCD-1.47B. It connects using
the existing, gitignored `wifi_settings.txt`, and exchanges messages directly
with the Android app on the same LAN. No account or cloud service is needed.
The ESP32 needs a 2.4 GHz WPA2/WPA3 network. The phone may use either band of the
same LAN; guest/client isolation must be disabled.

## Start

1. Fill in `wifi_settings.txt` using `wifi_settings.example.txt`.
2. Activate ESP-IDF 6.1 and run `./flash.sh`. The credentials are included in
   the local FAT image and flashed with the application.
3. Build/install the Android app as described in [android/README.md](../android/README.md).
4. Open **Child Beeper**, tap **Find on Wi-Fi**, or enter the IP at the bottom of
   the beeper's display. Enter the six-digit **Pair** code shown below the IP,
   then tap **Connect**.

The code is stored in NVS and survives reflashing. It also appears on the local
USB console at startup, for setup without a readable screen. Discovery does not
return it. The app saves the IP and code locally; app backups exclude them.
If DHCP changes the beeper's IP, find it again and reconnect.

This is a basic trusted-LAN application: HTTP and UDP discovery are unencrypted.
The pairing code gates message history, sending, time and alarm changes; it is
not a replacement for network security. Do not forward its ports to the internet.
Build outputs include Wi-Fi credentials in the FAT image; keep them private.

## Messages and BOOT button

- Incoming messages appear on the screen. Long text scrolls. The side RGB light
  smoothly changes blue → yellow → blue twice per heartbeat, until marked read.
- Tap BOOT to mark an unread message read. Otherwise, tap to cycle through
  **I'm OK**, **On my way**, **Please call me**, and **I need help**.
- Double-tap BOOT to send the selected reply. The Android conversation shows it
  on its next poll (normally within one second).
- The Android app can compose messages up to **240 UTF-8 bytes**. The beeper
  retains the latest **16 messages** in RAM, including phone messages, device
  replies and alarm events. History is cleared by a restart.
- Keep the Android app visible for live updates. It resumes polling on return
  and reads the retained messages; there is no background notification service.
- Messages support UTF-8 on the wire. The small LCD's bundled Montserrat font
  has limited glyph coverage; emoji and some scripts may render as missing
  glyphs. Android displays the original text.

## Time and daily alarm

The phone sets UTC time and its current local UTC offset when connecting and
once per minute while the app is open. The ESP32 also uses `pool.ntp.org` to
synchronize UTC when internet access is available. Without internet, the phone
still sets the clock over the LAN. The local offset is saved in NVS; the first
boot defaults to UTC until paired.

The clock continues while the board is powered, including during Wi-Fi outages.
After power loss it needs the phone or NTP again. The offset is fixed between
phone syncs: reopen the app after travel or a daylight-saving change.

In Android, **Set daily alarm** chooses a 24-hour time and enables it.
**Disable alarm / Enable alarm** changes its enabled state. The alarm settings
survive board restarts.

To set an alarm using only the beeper:

1. Hold BOOT for at least 0.8 seconds to edit the hour.
2. Tap to increment the value; double-tap to advance to minutes, then enabled.
3. On the enabled step, tap to choose ON/OFF; double-tap to save.
4. Hold BOOT again at any editing step to cancel.

At the selected local minute, the display and side LED flash for **60 seconds**,
and an `Alarm HH:MM` message is sent to the conversation. Press BOOT or tap
**Stop alarm / mark read** on the phone to dismiss it. The same occurrence cannot
retrigger after dismissal or a clock adjustment backwards. The alarm repeats
daily; missed alarms while powered off are not replayed.

This board has **no speaker/buzzer**. The alarm is visual.

## Local protocol (version 1)

HTTP port **80**. All endpoints except `/api/info` require the header
`X-Beeper-Key: <six-digit pairing code>`. POST bodies are JSON, limited to 1024
bytes. Invalid data returns 400, a missing/wrong code 401, and a persistence
failure 500. Successful writes return the updated state.

| Request | Body / result |
| --- | --- |
| `GET /api/info` | `name`, `protocol`, `port`, `boot_id` (no pairing code) |
| `GET /api/state` | Info plus `epoch`, `utc_offset_minutes`, `unread`, `alarm`, `messages` |
| `POST /api/messages` | `{"text":"Hello"}`; nonblank valid UTF-8, 1–240 bytes |
| `POST /api/clock` | `{"epoch":1795777200,"utc_offset_minutes":60}` |
| `POST /api/alarm` | `{"enabled":true,"hour":7,"minute":30}` |
| `POST /api/ack` | `{}`; stop the current alarm and mark incoming messages read |

`epoch` is UTC seconds (0 when unsynchronized). Clock writes accept integer
epochs from 2024 through 2099 and integer offsets from −720 to +840 minutes.
Each message has `id`, `from` (`phone` or `device`), `text`, and `epoch`.
IDs increase within a boot; use `boot_id` to recognize a reset. The history is
oldest first. Clients should not automatically retry an ambiguous message POST,
since the first request may already have arrived; fetch history first.

Discovery: broadcast the ASCII bytes `CHILD_BEEPER_DISCOVER_V1` to UDP port
**42424**. Each beeper replies to the sender with the same JSON as `/api/info`.
Use the packet source address as the HTTP host. Manual IP entry works when a
router blocks broadcast discovery.

## Verification

`./cb test` exercises message validation/retention, alarm timing/dismissal,
clock adjustments and the heartbeat envelope, alongside the existing BSP tests.
`./cb verify` remains the repository build gate. Android uses
`./gradlew assembleDebug lintDebug`. Measured board/app results and pending
visual/physical checks are recorded in [verification.md](verification.md).
