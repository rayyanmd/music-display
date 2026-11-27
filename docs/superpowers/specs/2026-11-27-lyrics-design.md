# Lyrics on the now-playing display — design

Status: draft for approval · 2026-11-27 · extends `2026-11-26-now-playing-display-design.md` and `protocol/PROTOCOL.md` (v1)

## Intent
The user wants to read the lyrics of the song they're playing on the ESP32 display, synced line by line where possible. The display keeps working offline. Lyrics are an extra, fetched by the helper (Mac now, Android later) when it has internet.

## Goals
- A lyrics screen you reach by **tapping the album art**. Tap the lyrics area to go back. *(user's choice: option 1)*
- **Synced lyrics** highlight the current line within about 0.25 s of the singing.
- Lyrics come from **LRCLIB** (`lrclib.net`), which is free and needs no key. *(user's choice)*
- The ⏮ ⏯ ⏭ controls keep working on the lyrics screen.
- Old helpers and old firmware keep working. Lyrics are an optional addition to the protocol.

## Non-goals
- Spotify/Musixmatch lyrics (no public API; the unofficial route needs a login cookie and breaks the terms of service).
- New fonts for full CJK, Arabic or Hebrew. We use the existing efontJA set, and characters it lacks are skipped.
- Scrolling through lyrics by touch, translations, or karaoke word-by-word highlighting.
- The Android helper. It will implement the same protocol in its own phase.

## Anti-goals
- Lyrics running visibly ahead of or behind the song after a seek.
- Lyric lookups slowing down the art, meta or controls.
- A lyrics screen that leaves you stuck without controls.

## Constraints
- Privacy: each new track's title, artist, album and duration are sent to lrclib.net. That is the only new outside service, and the request uses the generic User-Agent `MusicDisplay/1.0`.
- BLE throughput is about 20–40 KB/s in practice, and lyrics for one song are about 1.5–6 KB.
- Display RAM: the lyrics buffer goes in PSRAM, 16 KB maximum per source slot.

## Approaches considered
| Approach | Trade-off | Status |
|---|---|---|
| **Helper fetches LRCLIB and sends timed lines over BLE** | No new display networking; the helper needs internet | **Chosen** |
| Spotify's internal lyrics endpoint via the `sp_dc` cookie | Best coverage, but against the terms of service, breaks often, and needs a secret | Rejected |
| Display fetches lyrics itself over Wi-Fi | Needs Wi-Fi setup on the display, against the original offline/BLE choice | Rejected |
| Lyrics shown in the album line on the main screen (option 2) | Always visible, but long lines marquee and are hard to read | Rejected by user |

## Protocol addition (v1.1, backward compatible)
New characteristic **Lyrics** `6e0a0006-7b5d-4c1e-9f3a-2d8c5b1e4a70`: write without response plus write, max 512 bytes per write. Helpers check whether it exists after service discovery and skip lyrics if it's missing (old firmware). Old helpers never write it.

The transfer is chunked like Art:
- Header (written **with response**): `0x01, u32 track_id, u32 total_len, u8 kind`.
  `kind`: `0` none found, `1` synced, `2` plain (untimed), `3` instrumental. For kinds 0 and 3, `total_len` = 0 and no data follows.
- Data: `0x02, u32 track_id, u32 offset, bytes…`, contiguous from 0, chunk size the same as Art.
- Body (`total_len` ≤ 16384): a sequence of lines, each `u32 time_ms, u8 len, len bytes UTF-8` (len ≤ 200, cut on a UTF-8 boundary). Lines are sorted by time. For plain lyrics every `time_ms` = 0. An empty line (len 0) is an instrumental gap and shows as "♪".
- Ordering: the helper sends Lyrics **after Art** for a track. The display applies the same pending→current promotion rules as Art (lyrics for an upcoming track id are held until that State arrives).
- On reconnect, `resendAll` re-sends the current track's lyrics after Art.

## Mac helper
- `HelperCore/Lyrics.swift`:
  - `LRC.parse(String) -> [LyricLine]` handles `[mm:ss.xx]` / `[mm:ss.xxx]` / `[mm:ss]` stamps, several stamps on one line, `[offset:±ms]` (LRC convention: positive offset shows lyrics earlier, i.e. `time − offset`, clamped at 0), metadata tags (`[ar:]` and the like) ignored, and CRLF. The result is sorted.
  - `Wire.lyricsMessages(trackId:kind:lines:chunk:)` encodes and truncates to 16 KB, dropping whole lines from the end.
- `LyricsFetcher` (app target):
  - It first calls `GET https://lrclib.net/api/get?track_name=&artist_name=&album_name=&duration=<s>`.
  - On a 404 it makes **one** `GET /api/search?track_name=<clean title>&artist_name=<first artist>`. The clean title has Spotify suffixes stripped, such as " - Remastered 2011", " - Live", " (feat. …)" and " - Radio Edit". It picks the best hit with a pure, tested `LyricsMatch.best(hits, artist:, durationS:)`:
    - the hit's artist must contain the first artist (case- and diacritic-insensitive);
    - its duration must be within 3 s;
    - synced beats plain, and a closer duration wins ties. *(my call: Spotify and LRCLIB names often differ, so a strict lookup alone would miss a lot)*
  - The result is `syncedLyrics` if present (kind 1), else `plainLyrics` (kind 2). `instrumental:true` → kind 3. No match → kind 0.
  - **Caching:** only definite answers are cached in memory per track URI (last 50): kinds 1/2/3, and kind 0 after both lookups miss. On a network error or timeout (8 s) it sends kind 0 but does **not** cache it, so the next time that track becomes current it tries again. *(my call: so a Mac waking without Wi-Fi doesn't stick a song on "No lyrics")*
- `SendPlanner` gains `lyricsReady(uri:kind:lines:)`. It stores the result for `resendAll`, like `artJpeg`.
- **Better seek tracking:** the AppleScript poll while playing goes from 5 s to 2 s, and the seek tolerance from 2000 ms to 750 ms, so a seek corrects the display within about 2 s. The periodic State stays at 5 s.

## Firmware
- `proto::LyricsAssembler` (or a generalisation of `ArtAssembler` with a kind byte) and `proto::decodeLyrics(bytes) -> std::vector<LyricLine>`, which rejects malformed bodies. Both are unit-tested natively.
- `NowPlaying`: each slot holds current and pending lyrics as a `shared_ptr<const Lyrics>` with `{kind, lines}`. `Snapshot` exposes `lyrics` plus `lyricsVersion`. `0` means nothing has been received yet for this track.
- `layout`: a new `Rect kLyrics{8, 24, 304, 146}` and a tap on `kArt` toggles the view. On the lyrics screen, a tap in `kLyrics` toggles back. Buttons keep their rects. A pure `lyrics::currentLine(lines, posMs) -> int` is unit-tested (−1 before the first line).
- `TapDetector` already debounces. The view toggle reuses it, and taps that toggle don't send commands.
- The UI view state (Main/Lyrics) lives in `Ui`. It persists across track changes and drops back to Main when nothing is active.

### Lyrics screen (320×240)
```
┌──────────────────────────────────────────┐
│ ● Mac                              ♪ ▮▮▮ │  status row (unchanged)
│                                          │
│   And I said, hey                        │  previous line, dim grey, 12 px
│   What's going on?                       │  CURRENT line, white, 16 px, wraps ≤2 rows
│   And so I cry sometimes                 │  next line, dim, 12 px
│   When I'm lying in bed                  │  next+1, dimmer
│                                          │
│  ━━━━━━━━━━━━━━━━━────────────  1:42 3:55 │  thin progress + times
│  Song title — Artist (12 px, marquee)   ⏮ ⏯ ⏭ │
└──────────────────────────────────────────┘
```
- Synced: the current line is drawn at a fixed row. A line change animates a 150 ms upward slide, computed from the time since the change in the normal frame loop (no blocking delay).
- Plain: lines are shown in order, scrolling automatically in proportion to song position. A small "unsynced" tag appears in the status row. *(my call: it's better than a static first page, and the tag stops it being taken as exact)*
- Kind 0 → "No lyrics for this song". Kind 3 → "♪ Instrumental". Nothing received yet → "Looking for lyrics…" for up to 10 s, then "No lyrics" *(covers helpers without lyrics support)*.
- The last-row buttons are smaller (the same 40 px wide by 36 px tall) to free up height. A 6 px touch slop still applies.

## Testing
- Native (Unity): lyrics body decode (valid, truncated, len overflow, unsorted), assembler (header/data/abort/kinds 0 & 3), NowPlaying pending→current promotion for lyrics, `currentLine` boundaries, and the art/lyrics toggle hit tests.
- Swift: LRC parsing cases, encoder truncation at 16 KB, planner `lyricsReady` and `resendAll` ordering (Meta, Art, Lyrics, State), and JSON → kind mapping on fixture responses, the title-suffix cleaner, and `LyricsMatch.best` (artist mismatch, duration ±3 s, synced preferred).
- On hardware: a popular synced song lines up by ear, and a seek corrects within about 2 s. Untimed, instrumental and not-found songs show the right screen. Controls work on the lyrics screen. Old firmware with the new helper still works.

## Left to the builder
Exact fonts and colours (within the dark theme), animation easing, cache size details, whether to generalise `ArtAssembler` or add a sibling, the exact layout pixel values, and log wording.
