import Foundation

/// What Spotify is playing right now.
public struct SpotifySnapshot: Equatable, Sendable {
  public var uri: String
  public var title, artist, album: String
  public var durationMs: UInt32
  public var positionMs: UInt32
  public var playing: Bool
  public var artworkURL: URL?

  public init(uri: String, title: String, artist: String, album: String, durationMs: UInt32,
              positionMs: UInt32, playing: Bool, artworkURL: URL?) {
    self.uri = uri
    self.title = title
    self.artist = artist
    self.album = album
    self.durationMs = durationMs
    self.positionMs = positionMs
    self.playing = playing
    self.artworkURL = artworkURL
  }
}

public enum SpotifyParse {
  /// Field separator used by the AppleScript query (ASCII 30, record separator).
  public static let separator: Character = "\u{1E}"

  /// userInfo of the "com.spotify.client.PlaybackStateChanged" distributed notification.
  /// nil when stopped or when there is no track.
  public static func notification(_ info: [AnyHashable: Any]) -> SpotifySnapshot? {
    guard let state = info["Player State"] as? String, state != "Stopped",
          let uri = info["Track ID"] as? String, !uri.isEmpty else { return nil }
    let duration = (info["Duration"] as? NSNumber)?.doubleValue ?? 0
    let position = (info["Playback Position"] as? NSNumber)?.doubleValue ?? 0
    return SpotifySnapshot(
      uri: uri,
      title: info["Name"] as? String ?? "",
      artist: info["Artist"] as? String ?? "",
      album: info["Album"] as? String ?? "",
      durationMs: clampMs(duration),
      positionMs: clampMs(position * 1000),
      playing: state == "Playing",
      artworkURL: nil)
  }

  /// "state␞id␞name␞artist␞album␞duration(ms)␞position(s)␞artwork url".
  public static func appleScript(_ s: String) -> SpotifySnapshot? {
    let f = s.split(separator: separator, omittingEmptySubsequences: false).map(String.init)
    guard f.count >= 8, f[0] != "stopped", !f[1].isEmpty else { return nil }
    let position = Double(f[6].replacingOccurrences(of: ",", with: ".")) ?? 0
    let art = f[7].trimmingCharacters(in: .whitespacesAndNewlines)
    return SpotifySnapshot(
      uri: f[1], title: f[2], artist: f[3], album: f[4],
      durationMs: clampMs(Double(f[5]) ?? 0),
      positionMs: clampMs(position * 1000),
      playing: f[0] == "playing",
      artworkURL: art.isEmpty ? nil : URL(string: art))
  }

  static func clampMs(_ v: Double) -> UInt32 {
    guard v.isFinite, v > 0 else { return 0 }
    return v >= Double(UInt32.max) ? UInt32.max : UInt32(v.rounded())
  }
}
