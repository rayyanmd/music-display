import Foundation
import Testing
@testable import HelperCore

private func meta(_ id: UInt32) -> Outgoing { .meta(Wire.meta(trackId: id, title: "t", artist: "a", album: "b")) }
private func art(_ id: UInt32) -> Outgoing { .art(Wire.artMessages(trackId: id, jpeg: Data(count: 1000), chunk: 500)!) }
private func state(_ id: UInt32) -> Outgoing {
  .state(Wire.state(trackId: id, playing: true, local: true, source: 1, positionMs: 0, durationMs: 1))
}

@Test func queueDropsEverythingWhileDisconnected() {
  var q = WriteQueue()
  for i in 0..<100 { q.push([meta(UInt32(i)), state(UInt32(i))], connected: false) }
  #expect(q.isEmpty)
}

@Test func queueMetaDropsOnlyOlderTrackArt() {
  var q = WriteQueue()
  q.push([meta(7), art(7)], connected: true)
  #expect(q.count == 4)  // meta + header + 2 chunks
  q.push([meta(7)], connected: true)  // names filled in for the same track
  #expect(q.items.filter { $0.kind == .artData }.count == 2)
  q.push([meta(8)], connected: true)
  #expect(q.items.filter { $0.kind == .artHeader || $0.kind == .artData }.isEmpty)
}

@Test func queueKeepsOnlyNewestStateAtTail() {
  var q = WriteQueue()
  q.push([state(1), meta(2), state(2)], connected: true)
  #expect(q.items.map(\.kind) == [.meta, .state])
  #expect(q.items.last?.trackId == 2)
}

@Test func spotifyScriptsHaveTimeoutAndRunningGuard() {
  for s in [SpotifyScripts.query, SpotifyScripts.command(.playPause), SpotifyScripts.command(.next),
            SpotifyScripts.command(.prev)] {
    #expect(s.contains("with timeout of 2 seconds"))
    #expect(s.contains("if application \"Spotify\" is running then"))
  }
  #expect(SpotifyScripts.command(.next).contains("next track"))
  #expect(SpotifyScripts.command(.prev).contains("previous track"))
  #expect(SpotifyScripts.command(.playPause).contains("playpause"))
}
