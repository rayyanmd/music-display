import Foundation
import Testing
@testable import HelperCore

@Test func parseNotificationPlaying() throws {
  let info: [AnyHashable: Any] = [
    "Track ID": "spotify:track:abc", "Name": "Song", "Artist": "A", "Album": "B",
    "Duration": 225000, "Playback Position": 30.5, "Player State": "Playing",
  ]
  let s = try #require(SpotifyParse.notification(info))
  #expect(s.uri == "spotify:track:abc")
  #expect(s.title == "Song")
  #expect(s.artist == "A")
  #expect(s.album == "B")
  #expect(s.durationMs == 225000)
  #expect(s.positionMs == 30500)
  #expect(s.playing)
  #expect(s.artworkURL == nil)
}

@Test func parseStoppedGivesNotPlaying() {
  let info: [AnyHashable: Any] = ["Track ID": "spotify:track:abc", "Player State": "Stopped"]
  #expect(SpotifyParse.notification(info) == nil)
  #expect(SpotifyParse.appleScript("stopped") == nil)
}

@Test func parseAppleScriptCommaDecimal() throws {
  let s = try #require(SpotifyParse.appleScript(
    ["playing", "spotify:track:abc", "Song", "A", "B", "225000", "30,25", "https://i.scdn.co/image/x"]
      .joined(separator: "\u{1E}")))
  #expect(s.positionMs == 30250)
  #expect(s.durationMs == 225000)
  #expect(s.playing)
  #expect(s.artworkURL == URL(string: "https://i.scdn.co/image/x"))
}

@Test func parseLocalFileWithoutArtwork() throws {
  let s = try #require(SpotifyParse.appleScript(
    ["paused", "spotify:local:Artist:Album:Title:180", "Title", "Artist", "Album", "180000", "0", ""]
      .joined(separator: "\u{1E}")))
  #expect(s.artworkURL == nil)
  #expect(!s.playing)
  #expect(s.title == "Title")
}
