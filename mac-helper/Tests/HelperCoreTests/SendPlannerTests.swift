import Foundation
import Testing
@testable import HelperCore

private func snap(_ uri: String = "spotify:track:a", playing: Bool = true, pos: UInt32 = 0) -> SpotifySnapshot {
  SpotifySnapshot(uri: uri, title: "T", artist: "A", album: "B", durationMs: 200_000, positionMs: pos,
                  playing: playing, artworkURL: nil)
}

private func stateOf(_ o: Outgoing) -> (playing: Bool, trackId: UInt32)? {
  guard case .state(let d) = o else { return nil }
  let b = [UInt8](d)
  return (b[1] & 1 == 1, UInt32(b[4]) | UInt32(b[5]) << 8 | UInt32(b[6]) << 16 | UInt32(b[7]) << 24)
}

private func kinds(_ o: [Outgoing]) -> [String] {
  o.map {
    switch $0 {
    case .meta: "meta"
    case .art: "art"
    case .state: "state"
    case .lyrics: "lyrics"
    }
  }
}

@Test func plannerNewTrackSendsMetaThenState() {
  var p = SendPlanner(chunk: 500)
  let out = p.update(snap(), nowMs: 0)
  #expect(kinds(out) == ["meta", "state"])
  #expect(stateOf(out[1])?.playing == true)
}

@Test func plannerSameSnapshotSendsNothing() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  #expect(p.update(snap(pos: 1000), nowMs: 1000).isEmpty)
}

@Test func plannerPauseSendsState() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  let out = p.update(snap(playing: false, pos: 1000), nowMs: 1000)
  #expect(kinds(out) == ["state"])
  #expect(stateOf(out[0])?.playing == false)
}

@Test func plannerSeekSendsState() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  #expect(kinds(p.update(snap(pos: 61000), nowMs: 1000)) == ["state"])
  #expect(p.update(snap(pos: 62500), nowMs: 2000).isEmpty)  // within 750 ms of extrapolated 62000
}

@Test func plannerArtOnlyForCurrentTrack() throws {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap("spotify:track:old"), nowMs: 0)
  _ = p.update(snap("spotify:track:new"), nowMs: 10)
  let jpeg = Data(repeating: 0xAB, count: 1200)
  #expect(p.artReady(uri: "spotify:track:old", jpeg: jpeg).isEmpty)
  let out = p.artReady(uri: "spotify:track:new", jpeg: jpeg)
  #expect(kinds(out) == ["art"])
  guard case .art(let msgs) = out[0] else { Issue.record("not art"); return }
  #expect(msgs.first?.first == 0x01)
  #expect(msgs.count == 4)
}

@Test func plannerTickEvery5s() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  #expect(p.tick(nowMs: 4000).isEmpty)
  #expect(kinds(p.tick(nowMs: 5000)) == ["state"])
  _ = p.update(snap(playing: false, pos: 6000), nowMs: 6000)
  #expect(p.tick(nowMs: 20000).isEmpty)
}

@Test func plannerStoppedSendsPausedState() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  let out = p.update(nil, nowMs: 1000)
  #expect(kinds(out) == ["state"])
  #expect(stateOf(out[0])?.playing == false)
  #expect(stateOf(out[0])?.trackId == Wire.trackId("spotify:track:a"))
  #expect(p.update(nil, nowMs: 2000).isEmpty)
}

@Test func plannerReconnectResendsEverything() {
  var p = SendPlanner(chunk: 500)
  #expect(p.resendAll(nowMs: 0).isEmpty)
  _ = p.update(snap(), nowMs: 0)
  _ = p.artReady(uri: "spotify:track:a", jpeg: Data(repeating: 1, count: 600))
  #expect(kinds(p.resendAll(nowMs: 100)) == ["meta", "art", "state"])
}

private let lyr = LyricsResult(kind: .synced, lines: [LyricLine(timeMs: 1000, text: "hi")])

@Test func plannerLyricsReadyForCurrent() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  let out = p.lyricsReady(uri: "spotify:track:a", result: lyr)
  #expect(kinds(out) == ["lyrics"])
  guard case .lyrics(let parts) = out[0] else { return }
  #expect(parts.count == 2)
  #expect(WriteQueue.u32(parts[0], at: 1) == Wire.trackId("spotify:track:a"))
}

@Test func plannerLyricsIgnoredForOldTrack() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap("spotify:track:a"), nowMs: 0)
  _ = p.update(snap("spotify:track:b"), nowMs: 100)
  #expect(p.lyricsReady(uri: "spotify:track:a", result: lyr).isEmpty)
}

@Test func plannerResendOrderIncludesLyrics() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  _ = p.lyricsReady(uri: "spotify:track:a", result: lyr)
  _ = p.artReady(uri: "spotify:track:a", jpeg: Data(repeating: 1, count: 600))
  #expect(kinds(p.resendAll(nowMs: 100)) == ["meta", "art", "lyrics", "state"])
}

@Test func plannerResendIncludesNoneKind() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(), nowMs: 0)
  _ = p.lyricsReady(uri: "spotify:track:a", result: .none)
  #expect(kinds(p.resendAll(nowMs: 100)) == ["meta", "lyrics", "state"])
}

@Test func plannerTrackChangeClearsLyrics() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap("spotify:track:a"), nowMs: 0)
  _ = p.lyricsReady(uri: "spotify:track:a", result: lyr)
  _ = p.update(snap("spotify:track:b"), nowMs: 100)
  #expect(kinds(p.resendAll(nowMs: 200)) == ["meta", "state"])
}

@Test func plannerSeek800msTriggersState() {
  var p = SendPlanner(chunk: 500)
  _ = p.update(snap(pos: 10000), nowMs: 0)
  #expect(kinds(p.update(snap(pos: 11800), nowMs: 1000)) == ["state"])  // 800 ms ahead of 11000
  #expect(p.update(snap(pos: 12300), nowMs: 2000).isEmpty)               // 500 ms off 11800+1000
}
