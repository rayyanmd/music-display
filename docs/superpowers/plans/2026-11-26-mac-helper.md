# Mac Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A macOS menu-bar app that sends the Spotify desktop app's now-playing track (meta, art, state) to the ES3C28P display over BLE, and runs the display's play/pause/next/prev taps against Spotify.

**Architecture:** A SwiftPM package with a pure, unit-tested `HelperCore` library (protocol encoding, art processing, Spotify parsing, and a `SendPlanner` that decides what to send) and a thin `MusicDisplay` executable (CoreBluetooth central, Spotify notification/AppleScript source, SwiftUI `MenuBarExtra`). Scripts wrap the binary into a signed `.app`.

**Tech Stack:** Swift 6.3 (Command Line Tools only, no Xcode), SwiftPM, swift-testing, CoreBluetooth, ImageIO/CoreGraphics, SwiftUI MenuBarExtra, ServiceManagement.

**Spec:** `docs/superpowers/specs/2026-11-26-now-playing-display-design.md` (§2 Mac helper). Wire format: `protocol/PROTOCOL.md` (binding).

## Global Constraints
- macOS 14+ deployment target (the user runs 26.4). Strict Swift 6 concurrency. UI and the coordinator are `@MainActor`.
- Bundle id `com.musicdisplay.helper`. App name `MusicDisplay`. Installed to `~/Applications/MusicDisplay.app`. `LSUIElement = true`.
- Info.plist usage strings: `NSBluetoothAlwaysUsageDescription` = "Sends what Spotify is playing to your Now Playing display." `NSAppleEventsUsageDescription` = "Reads the current Spotify track and controls playback from the display."
- Signing: a self-signed identity named exactly `Music Display Dev`, created once by `scripts/make-cert.sh`. Never ad-hoc, because ad-hoc signing changes with every rebuild and makes macOS re-ask for the Bluetooth and Automation permissions.
- Never launch Spotify: check `NSRunningApplication` for bundle id `com.spotify.client` before any AppleScript.
- Protocol: UUIDs, byte layouts, the 160-byte field cap, the ≤ 512-byte Meta, art ≤ 32768 bytes as baseline JPEG 160×160 at quality ~0.75, ≤ 500-byte art chunks, and State every 5 s while playing. All exactly as in `protocol/PROTOCOL.md`.
- The Mac always sends `source = 1` (mac) and `local = true`. *(Ruling: Spotify's AppleScript/notification API doesn't expose whether it is remote-controlling a Connect device.)*
- Tests run with `mac-helper/scripts/test.sh`, which wraps `swift test` with the CLT Testing.framework paths (verified working on this Mac):
  `F=/Library/Developer/CommandLineTools/Library/Developer/Frameworks; L=/Library/Developer/CommandLineTools/Library/Developer/usr/lib; swift test -Xswiftc -F$F -Xlinker -F$F -Xlinker -rpath -Xlinker $F -Xlinker -rpath -Xlinker $L "$@"`

## Review Focus
1. **Spotify not running, quitting, or "stopped"**: the helper must never launch Spotify, and the display shows paused/idle rather than a stale "playing". Tests: `parseStoppedGivesNotPlaying` (Task 3) and `plannerStoppedSendsPausedState` (Task 3).
2. **Ads, podcasts and local files** (`spotify:ad:…`, `spotify:episode:…`, `spotify:local:…`, empty artwork URL): there's no crash, meta is still sent, and the art falls back to the display placeholder. Test: `parseLocalFileWithoutArtwork` (Task 3).
3. **Display goes out of range and comes back**, or is reset: the full current track (Meta, then Art, then State) is resent on reconnect, with no stale art. Test: `plannerReconnectResendsEverything` (Task 3).
4. **Emoji and very long titles/artists**: Meta stays ≤ 512 bytes and is cut only on UTF-8 boundaries. Test: `metaTruncatesOnUTF8Boundary` (Task 1).
5. **Artwork fetch fails or the image is huge or odd** (non-square, PNG, 3000 px): the state is still sent, art is skipped or re-encoded to fit ≤ 32768 bytes. Tests: `artFromLargeNonSquareImage`, `artFromGarbageReturnsNil` (Task 2).

---

## File Structure
```
mac-helper/Package.swift
mac-helper/Sources/HelperCore/Protocol.swift      fnv1a, State/Meta/Art encoding, UUIDs
mac-helper/Sources/HelperCore/ArtProcessor.swift  any image data -> 160x160 baseline JPEG
mac-helper/Sources/HelperCore/Spotify.swift       SpotifySnapshot + parsing (notification, AppleScript)
mac-helper/Sources/HelperCore/SendPlanner.swift   decides outgoing messages
mac-helper/Sources/MusicDisplay/App.swift         @main, MenuBarExtra UI
mac-helper/Sources/MusicDisplay/Coordinator.swift wiring, timers, art fetch
mac-helper/Sources/MusicDisplay/SpotifySource.swift  DistributedNotificationCenter + NSAppleScript
mac-helper/Sources/MusicDisplay/BLEClient.swift   CoreBluetooth central, write queue, reconnect
mac-helper/Tests/HelperCoreTests/*.swift
mac-helper/Resources/Info.plist
mac-helper/scripts/test.sh, bundle.sh, make-cert.sh
```

---

### Task 1: Package scaffold + protocol encoding

**Files:** Create `mac-helper/Package.swift`, `mac-helper/scripts/test.sh`, `Sources/HelperCore/Protocol.swift`, `Tests/HelperCoreTests/ProtocolTests.swift`, and a placeholder `Sources/MusicDisplay/App.swift` (`@main struct` that just prints, replaced in Task 4).

**Interfaces — Produces:**
```swift
public enum NP {   // UUID strings, lowercase, from PROTOCOL.md
  public static let service, state, meta, art, command, info: String
}
public enum Wire {
  public static func trackId(_ uri: String) -> UInt32                       // FNV-1a 32
  public static func state(trackId: UInt32, playing: Bool, local: Bool, source: UInt8,
                           positionMs: UInt32, durationMs: UInt32) -> Data  // 16 bytes
  public static func meta(trackId: UInt32, title: String, artist: String, album: String) -> Data
  public static func artMessages(trackId: UInt32, jpeg: Data, chunk: Int) -> [Data]?   // nil if jpeg.count > 32768 or empty
}
public enum DisplayCommand: UInt8 { case playPause = 1, next = 2, prev = 3 }
```
- [ ] **Step 1:** Write `ProtocolTests.swift` (swift-testing):
  - `trackIdMatchesReference`: `Wire.trackId("spotify:track:abc") == 0x0b1bc65e`, `Wire.trackId("spotify:track:4uLU6hMCjMI75M1A2tKUQC") == 0xf64054a7`, and `Wire.trackId("") == 0x811c9dc5`.
  - `stateBytes`: `state(trackId: 0x12345678, playing: true, local: true, source: 1, positionMs: 1000, durationMs: 60000)` equals `[1, 3, 1, 0, 0x78,0x56,0x34,0x12, 0xE8,0x03,0,0, 0x60,0xEA,0,0]`.
  - `metaLayout`: a meta of ("A","B","C") equals 4 LE id bytes + `"A\u{1F}B\u{1F}C"`.
  - `metaTruncatesOnUTF8Boundary`: a title of 200 × "é", an artist of 100 × "😀", and album "x". The total is ≤ 512, each field ≤ 160 bytes, `String(data: fieldBytes, encoding: .utf8) != nil` for every field, and the emoji field length is a multiple of 4.
  - `artChunking`: a 1200-byte jpeg with chunk 500 gives 4 messages: the header `[1, id LE, 1200 LE]`, then data at offsets 0/500/1000 (`0x02, id, offset`), with payloads concatenating back to the input. A jpeg of 32769 bytes gives `nil`.
- [ ] **Step 2:** Run `cd mac-helper && scripts/test.sh`. Expected: compile errors (`Wire` undefined).
- [ ] **Step 3:** Implement `Protocol.swift`. The UTF-8 cut works on `Array(s.utf8)`, backing up while `(b & 0xC0) == 0x80`.
- [ ] **Step 4:** Run `scripts/test.sh`. Expected: all pass.
- [ ] **Step 5:** Commit with `git commit -m "feat(mac): helper package and protocol encoding"`.

---

### Task 2: Art processing

**Files:** Create `Sources/HelperCore/ArtProcessor.swift` and `Tests/HelperCoreTests/ArtProcessorTests.swift`.

**Interfaces — Produces:**
```swift
public enum ArtProcessor {
  /// Center-crops to square, scales to 160x160, encodes baseline JPEG. Lowers quality
  /// (0.75, 0.6, 0.45, 0.3) until <= 32768 bytes. nil if undecodable.
  public static func jpeg160(from imageData: Data) -> Data?
}
```
- [ ] **Step 1:** Write tests. A test helper renders a CGImage (gradient plus noise so it compresses poorly) and encodes it to PNG via ImageIO.
  - `artFromLargeNonSquareImage`: a 3000×2000 PNG in gives a result that is non-nil, ≤ 32768 bytes, decodes (via `CGImageSourceCreateWithData`) to exactly 160×160, starts with `FF D8`, contains the SOF0 marker `FF C0`, and does **not** contain the progressive SOF2 marker `FF C2`.
  - `artFromGarbageReturnsNil`: `Data("hello".utf8)` gives nil.
- [ ] **Step 2:** Run `scripts/test.sh`. Expected: compile failure.
- [ ] **Step 3:** Implement with `CGImageSource`, `CGImage.cropping`, a `CGContext` 160×160 sRGB draw (interpolation `.high`), and `CGImageDestination` with `kCGImageDestinationLossyCompressionQuality`. ImageIO writes baseline by default, so don't set `kCGImagePropertyJFIFIsProgressive`.
- [ ] **Step 4:** Run `scripts/test.sh`. Expected: all pass.
- [ ] **Step 5:** Commit with `git commit -m "feat(mac): album art to 160px baseline JPEG"`.

---

### Task 3: Spotify parsing + SendPlanner

**Files:** Create `Sources/HelperCore/Spotify.swift`, `Sources/HelperCore/SendPlanner.swift`, `Tests/HelperCoreTests/SpotifyTests.swift` and `Tests/HelperCoreTests/SendPlannerTests.swift`.

**Interfaces — Produces:**
```swift
public struct SpotifySnapshot: Equatable, Sendable {
  public var uri: String; public var title, artist, album: String
  public var durationMs: UInt32; public var positionMs: UInt32
  public var playing: Bool; public var artworkURL: URL?
}
public enum SpotifyParse {
  /// userInfo of distributed notification "com.spotify.client.PlaybackStateChanged".
  /// Keys: "Track ID", "Name", "Artist", "Album", "Duration" (ms, Int), "Playback Position" (s, Double),
  /// "Player State" ("Playing"|"Paused"|"Stopped"). Returns nil when "Stopped" or no Track ID.
  public static func notification(_ info: [AnyHashable: Any]) -> SpotifySnapshot?
  /// Output of the AppleScript in SpotifySource: fields joined by U+001E in this order:
  /// state, id, name, artist, album, duration(ms), position(s, decimal with "." or ","), artwork url.
  /// nil if state is "stopped" or fewer than 8 fields.
  public static func appleScript(_ s: String) -> SpotifySnapshot?
}
public enum Outgoing: Equatable { case meta(Data), art([Data]), state(Data) }
public struct SendPlanner {
  public init(chunk: Int)
  /// Call on every snapshot change (nil = stopped/not running). Returns messages in send order.
  public mutating func update(_ snap: SpotifySnapshot?, nowMs: UInt64) -> [Outgoing]
  /// Art for `uri` became available (already processed JPEG). Ignored if uri != current.
  public mutating func artReady(uri: String, jpeg: Data) -> [Outgoing]
  /// Every second: returns [.state] if playing and >= 5000 ms since the last State.
  public mutating func tick(nowMs: UInt64) -> [Outgoing]
  /// After (re)connect: full resend of the current track (meta, art if known, state).
  public mutating func resendAll(nowMs: UInt64) -> [Outgoing]
}
```
Planner rules:
- A track change (new uri) sends `[meta, state]`, plus art once `artReady` arrives.
- Same track with a playing change, or a position jump of more than 2000 ms from the extrapolated position, sends `[state]`.
- An identical snapshot sends `[]`.
- nil (stopped) after a track sends a single `state` with `playing=false` for the last track, then `[]` for repeated nils.
- `resendAll` before any track sends `[]`.

- [ ] **Step 1:** Write `SpotifyTests`:
  - `parseNotificationPlaying`: a dict with Track ID "spotify:track:abc", Name "Song", Artist "A", Album "B", Duration 225000, Playback Position 30.5 and Player State "Playing" gives the snapshot with positionMs 30500 and playing true.
  - `parseStoppedGivesNotPlaying`: Player State "Stopped" gives nil.
  - `parseAppleScriptCommaDecimal`: `"playing␞spotify:track:abc␞Song␞A␞B␞225000␞30,25␞https://i.scdn.co/image/x"` gives position 30250, with the artwork URL set.
  - `parseLocalFileWithoutArtwork`: an id of `spotify:local:Artist:Album:Title:180`, empty artwork and state paused gives a snapshot with artworkURL nil and playing false.
- [ ] **Step 2:** Write `SendPlannerTests` (chunk 500):
  - `plannerNewTrackSendsMetaThenState`: first update gives exactly `[.meta, .state]`, where the state decodes to playing.
  - `plannerSameSnapshotSendsNothing`
  - `plannerPauseSendsState`: playing → paused gives one `.state` with flags bit0 = 0.
  - `plannerSeekSendsState`: position jumps +60000 at the same time gives `.state`. A +1000 drift gives `[]`.
  - `plannerArtOnlyForCurrentTrack`: `artReady` for an old uri gives `[]`. For the current uri it gives `[.art(messages)]`, whose first message starts with `0x01`.
  - `plannerTickEvery5s`: playing, tick at +4000 gives `[]` and at +5000 gives `[.state]`. While paused, ticks give `[]`.
  - `plannerStoppedSendsPausedState`: a track, then nil, gives one `.state` with playing=false and the same track id. Another nil gives `[]`.
  - `plannerReconnectResendsEverything`: after a track plus art, `resendAll` gives `[.meta, .art, .state]` in that order. With no track it gives `[]`.
- [ ] **Step 3:** Run `scripts/test.sh`. Expected: compile failure.
- [ ] **Step 4:** Implement. The planner stores the current snapshot, the extrapolation base `(positionMs, atMs)`, `lastStateMs`, and the processed art for the current uri.
- [ ] **Step 5:** Run `scripts/test.sh`. Expected: all pass.
- [ ] **Step 6:** Commit with `git commit -m "feat(mac): Spotify parsing and send planner"`.

---

### Task 4: The app: Spotify source, BLE client, menu bar, bundling

**Files:** Create `Sources/MusicDisplay/{App,Coordinator,SpotifySource,BLEClient}.swift`, `Resources/Info.plist`, `scripts/bundle.sh` and `scripts/make-cert.sh`. Modify `Package.swift` (the executable links `HelperCore`).

**Interfaces:**
- Consumes: everything in Tasks 1–3.
- Produces (internal to the app):
```swift
@MainActor final class SpotifySource {   // onChange fires on main
  var onChange: ((SpotifySnapshot?) -> Void)?
  func start()        // observe notification + poll AppleScript every 5 s while playing (and once at start)
  func send(_ c: DisplayCommand)   // AppleScript playpause / next track / previous track; no-op if Spotify not running
}
@MainActor final class BLEClient: NSObject {
  enum Status: Equatable { case bluetoothOff, scanning, connecting, connected(name: String) }
  var status: Status { get }; var onStatus: ((Status) -> Void)?
  var onCommand: ((DisplayCommand) -> Void)?; var onReady: (() -> Void)?   // ready = chars discovered + notify on
  var chunkSize: Int { get }   // min(500, maximumWriteValueLength(.withoutResponse) - 9)
  func start(); func send(_ msgs: [Outgoing])
}
@MainActor final class Coordinator: ObservableObject {
  @Published var status: String; @Published var nowPlaying: String
  @Published var launchAtLogin: Bool   // SMAppService.mainApp register/unregister
}
```
Behaviour to pin:
- BLE: `CBCentralManager(delegate:queue: .main)`. Scan with `withServices: [NP.service]`. Connect to the first found, or the identifier remembered in `UserDefaults` key `displayId` if it is seen. On disconnect, rescan after 2 s.
- BLE writes: Meta and the Art header go `.withResponse`. Art data and State go `.withoutResponse`, gated by `canSendWriteWithoutResponse`. The rest is queued and drained in `peripheralIsReady(toSendWriteWithoutResponse:)`. When a new `.meta` is enqueued, drop any queued art data belonging to an older track id.
- Art: on a track change, fetch `artworkURL` with `URLSession` (10 s timeout), run `ArtProcessor.jpeg160` off the main actor, then `planner.artReady`. On failure, skip.
- AppleScript (run only when `NSRunningApplication.runningApplications(withBundleIdentifier: "com.spotify.client")` is non-empty):
  `tell application "Spotify" to return (player state as string) & (ASCII character 30) & (id of current track) & (ASCII character 30) & (name of current track) & … & (artwork url of current track)` in the field order of `SpotifyParse.appleScript`.
- Menu: the status line (for example "Connected to NowPlaying-98BD" or "Searching for display…"), the current "Title — Artist" or "Spotify not playing", a "Launch at login" toggle, and "Quit".
- `make-cert.sh`: if `security find-identity -p codesigning` already lists "Music Display Dev", exit 0. Otherwise generate a key and a self-signed cert with `openssl req -x509` (EKU codeSigning, CN "Music Display Dev", 10 years), package it as a p12, and `security import … -k ~/Library/Keychains/login.keychain-db -T /usr/bin/codesign`.
- `bundle.sh`: `swift build -c release`, then assemble `build/MusicDisplay.app/Contents/{MacOS,Info.plist}`, run `codesign --force --options runtime --sign "Music Display Dev" --identifier com.musicdisplay.helper`, copy to `~/Applications/`, and `open` it.

- [ ] **Step 1:** Write the four app files and `Info.plist`. Use `swift build`. Expected: build succeeds with no Swift 6 concurrency errors.
- [ ] **Step 2:** Run `scripts/test.sh`. Expected: still all pass.
- [ ] **Step 3:** Run `scripts/make-cert.sh`. Expected: `security find-identity -p codesigning` lists "Music Display Dev". *(One keychain prompt is possible. The user approves it.)*
- [ ] **Step 4:** Run `scripts/bundle.sh`. Expected: `codesign -dv ~/Applications/MusicDisplay.app` shows `Authority=Music Display Dev`, and the menu-bar icon appears. The user allows Bluetooth, then Automation for Spotify when prompted.
- [ ] **Step 5:** End-to-end with Spotify playing (the firmware from the phase 1 plan must be flashed):
  - the display shows the track, art and "● Mac" within 0.5 s of a Spotify track change;
  - pause and play in Spotify are reflected within 0.5 s;
  - tapping ⏯/⏭/⏮ on the display controls Spotify, one action per tap.
- [ ] **Step 6:** Reconnect check. Reset the display (RESET key). The helper reconnects and the full track reappears within about 5 s, with no manual action.
- [ ] **Step 7:** Commit with `git commit -m "feat(mac): menu-bar helper app with BLE and Spotify control"`.

---

### Task 5: Launch at login + permission persistence

**Files:** Modify `Sources/MusicDisplay/Coordinator.swift` and `App.swift` (toggle wiring, if not already complete in Task 4).

- [ ] **Step 1:** Enable "Launch at login" in the menu. Expected: `SMAppService.mainApp.status == .enabled`, and the app is listed in System Settings → General → Login Items.
- [ ] **Step 2:** Make a trivial change (the version string in Info.plist), then run `scripts/bundle.sh` again. Expected: **no** new Bluetooth or Automation prompt, and the display reconnects.
- [ ] **Step 3:** Quit Spotify. Expected: the display shows paused (the last track), and the helper doesn't relaunch Spotify. Then quit the helper, and the display shows "Nothing playing".
- [ ] **Step 4:** Commit with `git commit -m "feat(mac): launch at login; verified stable signing"`.
