import Foundation

/// GATT UUIDs from protocol/PROTOCOL.md.
public enum NP {
  public static let service = "6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let state = "6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let meta = "6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let art = "6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let command = "6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let info = "6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let lyrics = "6e0a0006-7b5d-4c1e-9f3a-2d8c5b1e4a70"  // v1.1, optional
}

public enum DisplayCommand: UInt8, Sendable {
  case playPause = 1, next = 2, prev = 3
}

/// Encoders for the v1 wire format (all integers little-endian).
public enum Wire {
  public static let maxField = 160
  public static let maxArt = 32768
  public static let maxLyrics = 16384
  public static let maxLyricLine = 200
  public static let maxLyricLines = 1000

  /// FNV-1a 32-bit over the URI's UTF-8 bytes.
  public static func trackId(_ uri: String) -> UInt32 {
    var h: UInt32 = 0x811C_9DC5
    for b in uri.utf8 {
      h = (h ^ UInt32(b)) &* 0x0100_0193
    }
    return h
  }

  public static func state(trackId: UInt32, playing: Bool, local: Bool, source: UInt8,
                           positionMs: UInt32, durationMs: UInt32) -> Data {
    var d = Data([1, (playing ? 1 : 0) | (local ? 2 : 0), source, 0])
    d.appendLE(trackId)
    d.appendLE(positionMs)
    d.appendLE(durationMs)
    return d
  }

  public static func meta(trackId: UInt32, title: String, artist: String, album: String) -> Data {
    var d = Data()
    d.appendLE(trackId)
    d.append(contentsOf: truncatedUTF8(title))
    d.append(0x1F)
    d.append(contentsOf: truncatedUTF8(artist))
    d.append(0x1F)
    d.append(contentsOf: truncatedUTF8(album))
    return d
  }

  /// Header followed by data chunks of at most `chunk` payload bytes. nil if empty or too large.
  public static func artMessages(trackId: UInt32, jpeg: Data, chunk: Int) -> [Data]? {
    guard !jpeg.isEmpty, jpeg.count <= maxArt, chunk > 0 else { return nil }
    var header = Data([1])
    header.appendLE(trackId)
    header.appendLE(UInt32(jpeg.count))
    var out = [header]
    let bytes = [UInt8](jpeg)
    var off = 0
    while off < bytes.count {
      let end = min(off + chunk, bytes.count)
      var m = Data([2])
      m.appendLE(trackId)
      m.appendLE(UInt32(off))
      m.append(contentsOf: bytes[off..<end])
      out.append(m)
      off = end
    }
    return out
  }

  /// Lyrics header (`0x01, id, total, kind`) followed by data chunks of at most `chunk` bytes.
  /// At most 1000 lines, each cut to 200 bytes; whole lines are dropped from the end to keep the body <= 16384.
  /// Kinds none/instrumental (or a body that ends up empty) send the header only, as kind none.
  public static func lyricsMessages(trackId: UInt32, result: LyricsResult, chunk: Int) -> [Data] {
    var kind = result.kind
    var body = Data()
    if kind == .synced || kind == .plain {
      for line in result.lines.prefix(maxLyricLines) {
        let text = truncatedUTF8(line.text, max: maxLyricLine)
        guard body.count + 5 + text.count <= maxLyrics else { break }
        body.appendLE(kind == .plain ? 0 : line.timeMs)
        body.append(UInt8(text.count))
        body.append(contentsOf: text)
      }
      if body.isEmpty { kind = .none }
    }
    var header = Data([1])
    header.appendLE(trackId)
    header.appendLE(UInt32(body.count))
    header.append(kind.rawValue)
    var out = [header]
    let bytes = [UInt8](body)
    let step = max(1, chunk)
    var off = 0
    while off < bytes.count {
      let end = min(off + step, bytes.count)
      var m = Data([2])
      m.appendLE(trackId)
      m.appendLE(UInt32(off))
      m.append(contentsOf: bytes[off..<end])
      out.append(m)
      off = end
    }
    return out
  }

  /// At most `max` bytes, never splitting a UTF-8 sequence.
  static func truncatedUTF8(_ s: String, max maxBytes: Int = maxField) -> [UInt8] {
    let b = Array(s.utf8)
    guard b.count > maxBytes else { return b }
    var n = maxBytes
    while n > 0 && (b[n] & 0xC0) == 0x80 { n -= 1 }
    return Array(b[0..<n])
  }
}

extension Data {
  mutating func appendLE(_ v: UInt32) {
    append(contentsOf: [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF), UInt8((v >> 16) & 0xFF), UInt8(v >> 24)])
  }
}
