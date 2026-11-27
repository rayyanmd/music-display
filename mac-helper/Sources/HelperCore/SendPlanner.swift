import Foundation

public enum Outgoing: Equatable, Sendable {
  case meta(Data)
  case art([Data])
  case state(Data)
  case lyrics([Data])
}

/// Decides which protocol messages to send as Spotify's state changes.
public struct SendPlanner {
  public static let source: UInt8 = 1  // mac
  public static let stateIntervalMs: UInt64 = 5000
  public static let seekToleranceMs: Int64 = 750

  /// Art payload bytes per BLE write; set from the live connection before resendAll.
  public var chunk: Int
  private var current: SpotifySnapshot?  // last known track; playing=false once stopped
  private var stopped = false
  private var basePosMs: UInt32 = 0
  private var baseAtMs: UInt64 = 0
  private var lastStateMs: UInt64 = 0
  private var artJpeg: Data?  // processed art for current.uri
  private var lyrics: LyricsResult?  // lyrics result for current.uri

  public init(chunk: Int) { self.chunk = chunk }

  public mutating func update(_ snap: SpotifySnapshot?, nowMs: UInt64) -> [Outgoing] {
    guard let snap else {
      guard var cur = current, !stopped else { return [] }
      cur.positionMs = position(at: nowMs)
      cur.playing = false
      current = cur
      stopped = true
      return [stateMessage(nowMs: nowMs)]
    }
    stopped = false
    guard let cur = current, cur.uri == snap.uri else {
      current = snap
      artJpeg = nil
      lyrics = nil
      setBase(snap.positionMs, nowMs)
      return [metaMessage(snap), stateMessage(nowMs: nowMs)]
    }
    let jumped = abs(Int64(snap.positionMs) - Int64(position(at: nowMs))) > Self.seekToleranceMs
    let changed = cur.playing != snap.playing || jumped || cur.durationMs != snap.durationMs
    let metaChanged = cur.title != snap.title || cur.artist != snap.artist || cur.album != snap.album
    current = snap
    if !changed && !metaChanged { return [] }
    setBase(snap.positionMs, nowMs)
    return (metaChanged ? [metaMessage(snap)] : []) + [stateMessage(nowMs: nowMs)]
  }

  public mutating func artReady(uri: String, jpeg: Data) -> [Outgoing] {
    guard let cur = current, cur.uri == uri,
          let msgs = Wire.artMessages(trackId: Wire.trackId(uri), jpeg: jpeg, chunk: chunk) else { return [] }
    artJpeg = jpeg
    return [.art(msgs)]
  }

  /// Lyrics (any kind, including none) for `uri`; ignored unless it is the current track.
  public mutating func lyricsReady(uri: String, result: LyricsResult) -> [Outgoing] {
    guard let cur = current, cur.uri == uri else { return [] }
    lyrics = result
    return [.lyrics(Wire.lyricsMessages(trackId: Wire.trackId(uri), result: result, chunk: chunk))]
  }

  public mutating func tick(nowMs: UInt64) -> [Outgoing] {
    guard let cur = current, cur.playing, nowMs - lastStateMs >= Self.stateIntervalMs else { return [] }
    return [stateMessage(nowMs: nowMs)]
  }

  public mutating func resendAll(nowMs: UInt64) -> [Outgoing] {
    guard let cur = current else { return [] }
    var out: [Outgoing] = [metaMessage(cur)]
    if let artJpeg, let msgs = Wire.artMessages(trackId: Wire.trackId(cur.uri), jpeg: artJpeg, chunk: chunk) {
      out.append(.art(msgs))
    }
    if let lyrics {
      out.append(.lyrics(Wire.lyricsMessages(trackId: Wire.trackId(cur.uri), result: lyrics, chunk: chunk)))
    }
    out.append(stateMessage(nowMs: nowMs))
    return out
  }

  // MARK: - helpers

  private mutating func setBase(_ pos: UInt32, _ nowMs: UInt64) {
    basePosMs = pos
    baseAtMs = nowMs
  }

  private func position(at nowMs: UInt64) -> UInt32 {
    guard let cur = current else { return 0 }
    var p = UInt64(basePosMs)
    if cur.playing && nowMs > baseAtMs { p += nowMs - baseAtMs }
    if cur.durationMs > 0 { p = min(p, UInt64(cur.durationMs)) }
    return UInt32(min(p, UInt64(UInt32.max)))
  }

  private func metaMessage(_ s: SpotifySnapshot) -> Outgoing {
    .meta(Wire.meta(trackId: Wire.trackId(s.uri), title: s.title, artist: s.artist, album: s.album))
  }

  private mutating func stateMessage(nowMs: UInt64) -> Outgoing {
    let cur = current!
    lastStateMs = nowMs
    return .state(Wire.state(trackId: Wire.trackId(cur.uri), playing: cur.playing, local: true,
                             source: Self.source, positionMs: position(at: nowMs), durationMs: cur.durationMs))
  }
}
