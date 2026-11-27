import Foundation

public struct LyricLine: Equatable, Sendable {
  public var timeMs: UInt32
  public var text: String
  public init(timeMs: UInt32, text: String) {
    self.timeMs = timeMs
    self.text = text
  }
}

public enum LyricsKind: UInt8, Sendable {
  case none = 0, synced, plain, instrumental
}

public struct LyricsResult: Equatable, Sendable {
  public var kind: LyricsKind
  public var lines: [LyricLine]
  public init(kind: LyricsKind, lines: [LyricLine]) {
    self.kind = kind
    self.lines = lines
  }
  public static let none = LyricsResult(kind: .none, lines: [])
}

/// LRC ("[mm:ss.xx]text") parsing.
public enum LRC {
  /// Timed lines sorted by time. Several stamps on one line give several lines; `[offset:ms]`
  /// (positive = earlier) is applied; other tags are ignored; leading/trailing empty lines dropped.
  public static func parse(_ s: String) -> [LyricLine] {
    var offset = 0
    var out: [LyricLine] = []
    for raw in splitLines(s) {
      var rest = Substring(raw)
      var stamps: [Int] = []
      while rest.first == "[", let close = rest.firstIndex(of: "]") {
        let tag = rest[rest.index(after: rest.startIndex)..<close]
        if let ms = stampMs(tag) {
          stamps.append(ms)
        } else if tag.lowercased().hasPrefix("offset:"), let v = Int(tag.dropFirst(7).trimmingCharacters(in: .whitespaces)) {
          offset = v
        }
        rest = rest[rest.index(after: close)...]
      }
      let text = rest.trimmingCharacters(in: .whitespaces)
      for t in stamps { out.append(LyricLine(timeMs: UInt32(clamping: t), text: text)) }
    }
    if offset != 0 {
      out = out.map { LyricLine(timeMs: UInt32(clamping: max(0, Int($0.timeMs) - offset)), text: $0.text) }
    }
    out = stableSorted(out)
    return trimEmpty(out)
  }

  /// Untimed lines (every time 0), blank lines at the ends dropped.
  public static func plain(_ s: String) -> [LyricLine] {
    trimEmpty(splitLines(s).map { LyricLine(timeMs: 0, text: $0.trimmingCharacters(in: .whitespaces)) })
  }

  static func splitLines(_ s: String) -> [String] {
    s.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\r", with: "\n")
      .split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
  }

  /// "mm:ss", "mm:ss.x", "mm:ss.xx", "mm:ss.xxx" -> ms; nil for anything else.
  static func stampMs(_ tag: Substring) -> Int? {
    let parts = tag.split(separator: ":", omittingEmptySubsequences: false)
    guard parts.count == 2, let m = Int(parts[0]), m >= 0 else { return nil }
    let secParts = parts[1].split(separator: ".", omittingEmptySubsequences: false)
    guard (1...2).contains(secParts.count), secParts[0].count <= 2, let sec = Int(secParts[0]) else { return nil }
    var ms = 0
    if secParts.count == 2 {
      let f = secParts[1]
      guard (1...3).contains(f.count), f.allSatisfy(\.isASCII), let v = Int(f) else { return nil }
      ms = v * [100, 10, 1][f.count - 1]
    }
    return m * 60000 + sec * 1000 + ms
  }

  static func stableSorted(_ l: [LyricLine]) -> [LyricLine] {
    l.enumerated().sorted { ($0.element.timeMs, $0.offset) < ($1.element.timeMs, $1.offset) }.map(\.element)
  }

  static func trimEmpty(_ l: [LyricLine]) -> [LyricLine] {
    guard let a = l.firstIndex(where: { !$0.text.isEmpty }), let b = l.lastIndex(where: { !$0.text.isEmpty }) else {
      return []
    }
    return Array(l[a...b])
  }
}

/// One LRCLIB track record (from /api/get, or an element of /api/search).
public struct LrclibTrack: Decodable, Equatable, Sendable {
  public var trackName: String
  public var artistName: String
  public var duration: Double
  public var instrumental: Bool
  public var plainLyrics: String?
  public var syncedLyrics: String?

  public init(trackName: String, artistName: String, duration: Double, instrumental: Bool,
              plainLyrics: String?, syncedLyrics: String?) {
    self.trackName = trackName
    self.artistName = artistName
    self.duration = duration
    self.instrumental = instrumental
    self.plainLyrics = plainLyrics
    self.syncedLyrics = syncedLyrics
  }

  enum CodingKeys: String, CodingKey { case trackName, artistName, duration, instrumental, plainLyrics, syncedLyrics }

  public init(from decoder: Decoder) throws {
    let c = try decoder.container(keyedBy: CodingKeys.self)
    trackName = try c.decodeIfPresent(String.self, forKey: .trackName) ?? ""
    artistName = try c.decodeIfPresent(String.self, forKey: .artistName) ?? ""
    duration = try c.decodeIfPresent(Double.self, forKey: .duration) ?? 0
    instrumental = try c.decodeIfPresent(Bool.self, forKey: .instrumental) ?? false
    plainLyrics = try c.decodeIfPresent(String.self, forKey: .plainLyrics)
    syncedLyrics = try c.decodeIfPresent(String.self, forKey: .syncedLyrics)
  }
}

/// Picking the right LRCLIB record for a Spotify track.
public enum LyricsMatch {
  public static let durationToleranceS = 3.0

  private static let suffixPatterns = [
    #"\s+-\s+.*\bremaster(ed)?\b.*$"#,
    #"\s+-\s+live\b.*$"#,
    #"\s+-\s+radio edit$"#,
    #"\s+-\s+single version$"#,
    #"\s+-\s+(mono|stereo)\b.*$"#,
    #"\s*[\(\[](feat\.?|ft\.|with)\s[^\)\]]*[\)\]]"#,
  ]

  /// Strips Spotify-style suffixes (" - Remastered 2011", " (feat. X)", ...) that LRCLIB titles lack.
  public static func cleanTitle(_ t: String) -> String {
    var s = t
    var changed = true
    while changed {
      changed = false
      for p in suffixPatterns {
        let r = s.replacingOccurrences(of: p, with: "", options: [.regularExpression, .caseInsensitive])
        if r != s {
          s = r
          changed = true
        }
      }
    }
    let trimmed = s.trimmingCharacters(in: .whitespaces)
    return trimmed.isEmpty ? t : trimmed
  }

  /// The first name in a Spotify artist string ("A, B", "A & B", "A feat. B", "A x B").
  public static func firstArtist(_ a: String) -> String {
    var cut = a.endIndex
    for sep in [", ", " & ", " feat. ", " x "] {
      if let r = a.range(of: sep, options: .caseInsensitive), r.lowerBound < cut { cut = r.lowerBound }
    }
    return String(a[..<cut]).trimmingCharacters(in: .whitespaces)
  }

  /// Artist must contain the first artist (case/diacritic-insensitive) and duration be within 3 s;
  /// synced beats plain, then the closer duration.
  public static func best(_ hits: [LrclibTrack], artist: String, durationS: Double) -> LrclibTrack? {
    let want = fold(firstArtist(artist))
    guard !want.isEmpty else { return nil }
    let ok = hits.filter { fold($0.artistName).contains(want) && abs($0.duration - durationS) <= durationToleranceS }
    return ok.min { a, b in
      let sa = hasSynced(a), sb = hasSynced(b)
      if sa != sb { return sa }
      return abs(a.duration - durationS) < abs(b.duration - durationS)
    }
  }

  public static func result(from t: LrclibTrack?) -> LyricsResult {
    guard let t else { return .none }
    if t.instrumental { return LyricsResult(kind: .instrumental, lines: []) }
    if let s = t.syncedLyrics {
      let l = LRC.parse(s)
      if !l.isEmpty { return LyricsResult(kind: .synced, lines: l) }
    }
    if let p = t.plainLyrics {
      let l = LRC.plain(p)
      if !l.isEmpty { return LyricsResult(kind: .plain, lines: l) }
    }
    return .none
  }

  static func hasSynced(_ t: LrclibTrack) -> Bool { !(t.syncedLyrics ?? "").trimmingCharacters(in: .whitespacesAndNewlines).isEmpty }

  static func fold(_ s: String) -> String {
    s.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: nil).trimmingCharacters(in: .whitespaces)
  }
}
