# Spotify "Now Playing" display: design + build plan

## Context
The user wants a small wireless device that shows what they're playing on Spotify on their Mac or Android phone, with touch controls. The hardware is the LCDwiki ES3C28P (ESP32-S3, 2.8" 320×240 IPS, capacitive touch, LiPo support), already connected at `/dev/cu.usbmodem101` and verified as ESP32-S3 with 8 MB PSRAM and 16 MB flash. The workspace `/Users/rayyan/Projects/music-display` is empty apart from the board skill at `.agents/skills/es3c28p-display/` (pin map, gotchas, LovyanGFX config, a compiling PlatformIO starter).

Decisions the user made:
- **Spotify only** (Premium). Sources are the Mac (macOS 26.4, Spotify desktop app) and an Android phone.
- **Transport is Bluetooth LE, with helper apps.** The user chose this over Spotify's cloud API so it works with no Wi-Fi or internet (car, other rooms).
- **Controls:** touch buttons for previous, play/pause and next, plus song, artist, album, album art and a progress bar.
- **Layout:** landscape, album art on the left (≈160×160), with title, artist, album, progress, times and buttons on the right, and a small source badge ("● Mac" / "● Phone").
- **Placement:** both a desk (USB) and portable (battery), so it needs a battery indicator and power saving.

Goals: instant (<0.5 s) updates, works with no network, Mac and phone usable at the same time, a single small device.
Non-goals for v1: Wi-Fi/Spotify Web API, other music apps, volume control, seek-by-touch, lyrics, multiple displays.
Anti-goals: anything the user must babysit (re-pairing every day, a helper that silently stops on Android), and laggy or janky controls.

## Approaches considered
| Approach | Trade-off | Status |
|---|---|---|
| Spotify Web API from the ESP32 over Wi-Fi | Nothing to install on Mac or phone, but needs Wi-Fi and internet, and lags 1–2 s | Rejected: the user wants it to work away from Wi-Fi |
| **Helpers → display over BLE** | Instant and works offline, but two helper apps to build and keep running, with ~10 m range | **Chosen** |
| Helpers over Wi-Fi | Instant, but the phone must share the display's Wi-Fi | Rejected |
| No-app Bluetooth (AVRCP, as car stereos do) | — | Not possible: the ESP32-S3 has BLE only, with no Bluetooth Classic |

A Wi-Fi/cloud fallback could be phase 2 and is out of scope here.

## Architecture
```
Mac menu-bar helper (Swift) ──BLE central──┐
                                           ├──► ESP32-S3 display = BLE peripheral (NimBLE, ≤3 links)
Android helper (Kotlin)     ──BLE central──┘        touch → Command notify → active helper → Spotify
```
- **The display is the BLE peripheral and the helpers are centrals.** The display advertises one custom GATT service and accepts up to 3 simultaneous connections, so the Mac and phone can be connected together.
- **Arbitration:** the display shows the source that most recently reported `playing=true`. If neither is playing, it keeps the last active source shown as paused. Commands go only to the source currently shown.
- **Spotify Connect duplicates** *(my call)*: when one device remote-controls the other, both helpers can report the same track as playing. State carries a `local` flag. Android sets it from `MediaController.playbackInfo.playbackType` (REMOTE means not local). The Mac sets it to true unless Spotify is showing a Connect device. If both sources report the same `track_id`, the display keeps the current source when it's local, otherwise prefers the local one, and never switches badges for an identical track within 3 s. A command is sent to exactly one helper, and Spotify syncs the other.
- **Security:** BLE bonding (Just Works with LE Secure Connections). The display accepts writes only from bonded centrals. A long-press on the BOOT key opens a pairing window, and bonds are stored in NVS.

### GATT protocol (shared spec in `protocol/PROTOCOL.md`)
Service UUID: one custom 128-bit UUID (generated once and fixed in all three codebases).
| Characteristic | Dir | Format |
|---|---|---|
| `State` | helper → display (write w/o response) | compact binary: `ver, track_id(hash32), playing, local, position_ms, duration_ms, timestamp`, sent on change and every 5 s while playing |
| `Meta` | helper → display (write) | UTF-8 `title\x1Fartist\x1Falbum`, with `track_id`. Sent on track change. |
| `Art` | helper → display (write w/o response, chunked) | header `{track_id, total_len}` then 500-byte chunks. **Baseline JPEG, 160×160, ≤ ~12 KB.** |
| `Command` | display → helper (notify) | 1 byte: `0x01` play/pause, `0x02` next, `0x03` prev |
| `Info` | display → helper (read) | protocol version and battery % (lets helpers show a status) |

Both helpers request MTU 517 and 2M PHY where supported. The display interpolates the progress bar locally from `position_ms` and `timestamp`, so state messages stay rare.

## Components

### 1. Firmware: `firmware/` (PlatformIO, Arduino core 3.x)
Starts from `.agents/skills/es3c28p-display/assets/platformio/` (platformio.ini, `LGFX_ES3C28P.hpp`, which is already proven to compile).
- Libraries: LovyanGFX (display, JPEG decode, touch), NimBLE-Arduino 2.x (peripheral with multiple connections and bonding), Adafruit NeoPixel (optional status LED).
- Modules: `ble_service.*` (GATT server, chunk reassembly into a PSRAM buffer, per-connection source state), `now_playing.*` (state model plus arbitration), `ui.*` (layout rendering into an `LGFX_Sprite` for flicker-free text and progress, with a marquee for long titles), `power.*` (battery ADC on GPIO9 ×2, backlight dimming and sleep).
- UI: 320×240 with rotation 1. Art at 160×160 on the left (x=8, y=24), and the right column holds the text, progress bar, times and three 56-px touch buttons. A status row shows the source badge, BLE link count and battery icon. Idle screen: "Nothing playing", with paired sources listed.
- Power *(my call; the board can't sense whether USB is plugged in, so there is one policy for both cases)*: after 1 min paused or idle the backlight dims to 20%, and after 5 min it goes off. Any touch, new track or play event wakes it to full brightness. BLE stays connected. **No light sleep:** the stock Arduino core doesn't support BLE light sleep, so saving power comes from the backlight only (about 79 mA of the ~140 mA total). The battery icon shows the % from the ADC. With no battery fitted it reads about 4.2 V and simply shows "full".
- The speaker amp stays off (drive GPIO1 HIGH at boot).

### 2. Mac helper: `mac-helper/` (Swift 6, SwiftPM, no Xcode needed)
The Command Line Tools and Swift 6.3 are installed. Xcode isn't, so the app is built as a SwiftPM executable plus a `bundle.sh` that wraps it in a `.app` with an Info.plist (`LSUIElement`, fixed `CFBundleIdentifier`, `NSBluetoothAlwaysUsageDescription`, `NSAppleEventsUsageDescription`). No signing identity exists on this Mac. Ad-hoc signing changes with every rebuild and makes macOS re-ask for the Bluetooth and Automation permissions, so *(my call)* a one-time script creates a self-signed "Music Display Dev" code-signing certificate in the login keychain. `bundle.sh` signs with it, so permissions and launch-at-login persist across rebuilds. The app is installed to `~/Applications/MusicDisplay.app`.
- `MenuBarExtra` (SwiftUI) showing the connection status and current track, with a "Launch at login" toggle (`SMAppService`).
- Spotify read: listen for the `com.spotify.client.PlaybackStateChanged` distributed notification (event-driven, carries track ID, name, artist, album, duration, state and position). Fall back to AppleScript (`tell application "Spotify"` for `player position`, `artwork url`, and so on) every 5 s while playing.
- Art: fetch `artwork url` (i.scdn.co), resize to 160×160 with CoreGraphics, encode baseline JPEG with ImageIO (quality ~0.75), and cache by track id.
- Commands: AppleScript `playpause` / `next track` / `previous track`. Needs the one-time Automation permission prompt.
- BLE: a CoreBluetooth central that scans for the service UUID, connects, reconnects automatically, and writes State/Meta/Art.

### 3. Android helper: `android-helper/` (Kotlin, minSdk 31, since the user's phone runs Android 12+)
- A `NotificationListenerService`, needed so `MediaSessionManager.getActiveSessions()` is allowed. It filters to the `com.spotify.music` session and registers a `MediaController.Callback` for metadata and playback state.
- The art bitmap comes from `METADATA_KEY_ALBUM_ART`, falling back to `METADATA_KEY_ART`, then `…_ART_URI`/`…_ALBUM_ART_URI` loaded via ContentResolver. It is scaled to 160×160 and compressed with `Bitmap.compress(JPEG, 75)` (baseline). If no art is available, the display shows a placeholder.
- Keep-alive: `CompanionDeviceManager.associate()` plus `startObservingDevicePresence()` (API 31+) and `REQUEST_COMPANION_RUN_IN_BACKGROUND`. The foreground service uses type `connectedDevice` (required on Android 14+).
- Commands go to `controller.transportControls.play/pause/skipToNext/skipToPrevious`.
- BLE: pairing via **CompanionDeviceManager**, which gives a reliable background presence and permission to run in the background. A foreground service holds the GATT connection, with auto-reconnect.
- Minimal UI: pair the display, grant notification access, and show status and a battery-optimisation exemption hint.
- Toolchain: the Android SDK isn't installed (adb is). The user installs Android Studio, or cmdline-tools plus JDK 17. The project uses the Gradle wrapper, and the APK is sideloaded with `adb install`.

## Build order (each step ends with something working on the real board)
1. **Protocol spec**: `protocol/PROTOCOL.md`, with the UUIDs and byte layouts.
2. **Firmware UI with fake data**: render the layout with a hard-coded track and a bundled test JPEG, and check the touch buttons, marquee and progress.
3. **Firmware BLE**: the GATT server. Test with a generic BLE app (nRF Connect on Android) by writing Meta/State by hand.
4. **Mac helper**: the end-to-end Mac → display path, including art and controls.
5. **Android helper**: the end-to-end phone → display path, then the two-source arbitration.
6. **Power and battery polish**: dimming, sleep, battery icon, and the BOOT long-press pairing window.

The firmware and the Mac helper are buildable and testable entirely on this Mac. Android needs the SDK installed first.

## Verification
- Firmware: `pio run -t upload && pio device monitor` on `/dev/cu.usbmodem101`. Check visually that colors are correct (red/green/blue test), the layout matches the sketch, and taps register on the buttons.
- BLE: nRF Connect shows the service. Hand-written Meta/State bytes appear on screen within 0.5 s.
- Mac: play, pause and skip in Spotify, and the display follows in under 0.5 s with the right art. Tapping ⏯/⏭/⏮ on the display controls Spotify. Quit and relaunch the helper and it reconnects without re-pairing.
- Android: the same checks with the phone, screen off, after 30 min in the background.
- Both: play on the phone while the Mac is paused, and the display switches to "● Phone". Controls now act on the phone.
- Battery: unplug USB, check the battery % looks sane (3.3–4.2 V range), and check the backlight dims and turns off when paused, then wakes on touch or a new track.
- Connect duplicates: control the Mac's Spotify from the phone, and the badge must not flicker. A single tap on ⏭ skips exactly one track.
- Mac rebuild: rebuild and reinstall the helper, and no new permission prompt appears.

## After approval
Save this design to `docs/superpowers/specs/2026-11-26-now-playing-display-design.md` in the workspace (after a `git init`, since the workspace isn't a repo yet). Then write the step-by-step implementation plan (writing-plans) and start with build steps 1–3, which are firmware only and testable today on the connected board.

## Left to the builder
Exact UUID values, fonts and colors (dark theme, readable at arm's length), marquee speed, chunk size tuning, file and module naming inside each component, and the retry/backoff details.
