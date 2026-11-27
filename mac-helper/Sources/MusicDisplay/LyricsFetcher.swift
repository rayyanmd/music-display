import Foundation
import HelperCore

/// Looks up lyrics on LRCLIB (lrclib.net): an exact /api/get, then one /api/search fallback.
/// nil means a network problem (try again later); a definite "not found" is `.notFound`.
struct LyricsFetcher: Sendable {
  static let userAgent = "MusicDisplay/1.0"
  static let timeout: TimeInterval = 8

  private enum Response {
    case ok(Data)
    case notFound
    case failed
  }

  func fetch(_ s: SpotifySnapshot) async -> LyricsResult? {
    guard s.uri.hasPrefix("spotify:track:") || s.uri.hasPrefix("spotify:local:") else { return LyricsResult.notFound }  // ads, episodes
    let durationS = (Double(s.durationMs) / 1000).rounded()
    var get = URLComponents(string: "https://lrclib.net/api/get")!
    get.queryItems = [
      URLQueryItem(name: "track_name", value: s.title),
      URLQueryItem(name: "artist_name", value: s.artist),
      URLQueryItem(name: "album_name", value: s.album),
      URLQueryItem(name: "duration", value: String(Int(durationS))),
    ]
    switch await request(get.url!) {
    case .ok(let data):
      guard let t = try? JSONDecoder().decode(LrclibTrack.self, from: data) else { return nil }
      return LyricsMatch.result(from: t)
    case .failed:
      return nil
    case .notFound:
      break
    }
    var search = URLComponents(string: "https://lrclib.net/api/search")!
    search.queryItems = [
      URLQueryItem(name: "track_name", value: LyricsMatch.cleanTitle(s.title)),
      URLQueryItem(name: "artist_name", value: LyricsMatch.firstArtist(s.artist)),
    ]
    switch await request(search.url!) {
    case .ok(let data):
      guard let hits = try? JSONDecoder().decode([LrclibTrack].self, from: data) else { return nil }
      return LyricsMatch.result(from: LyricsMatch.best(hits, artist: s.artist, durationS: durationS))
    case .notFound:
      return LyricsResult.notFound
    case .failed:
      return nil
    }
  }

  private func request(_ url: URL) async -> Response {
    var req = URLRequest(url: url)
    req.timeoutInterval = Self.timeout
    req.setValue(Self.userAgent, forHTTPHeaderField: "User-Agent")
    guard let (data, resp) = try? await URLSession.shared.data(for: req),
          let http = resp as? HTTPURLResponse else { return .failed }
    switch http.statusCode {
    case 200: return .ok(data)
    case 404: return .notFound
    default: return .failed
    }
  }
}
