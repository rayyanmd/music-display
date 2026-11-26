import Foundation

/// GATT UUIDs from protocol/PROTOCOL.md.
public enum NP {
  public static let service = "6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let state = "6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let meta = "6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let art = "6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let command = "6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70"
  public static let info = "6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70"
}

public enum DisplayCommand: UInt8, Sendable {
  case playPause = 1, next = 2, prev = 3
}

/// Encoders for the v1 wire format (all integers little-endian).
public enum Wire {
  public static let maxField = 160
  public static let maxArt = 32768

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

  /// At most `maxField` bytes, never splitting a UTF-8 sequence.
  static func truncatedUTF8(_ s: String) -> [UInt8] {
    let b = Array(s.utf8)
    guard b.count > maxField else { return b }
    var n = maxField
    while n > 0 && (b[n] & 0xC0) == 0x80 { n -= 1 }
    return Array(b[0..<n])
  }
}

extension Data {
  mutating func appendLE(_ v: UInt32) {
    append(contentsOf: [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF), UInt8((v >> 16) & 0xFF), UInt8(v >> 24)])
  }
}
