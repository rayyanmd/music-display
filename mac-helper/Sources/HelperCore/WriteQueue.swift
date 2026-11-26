import Foundation

/// Ordered BLE writes with the pruning rules the display relies on.
public struct WriteQueue {
  public enum Kind: Sendable { case meta, artHeader, artData, state }
  public struct Item: Sendable {
    public let kind: Kind
    public let data: Data
    public let trackId: UInt32
  }

  public private(set) var items: [Item] = []
  public var isEmpty: Bool { items.isEmpty }
  public var count: Int { items.count }

  public init() {}

  /// Append messages. While disconnected everything is dropped: the full current
  /// track is resent on (re)connect, so a backlog would only delay it.
  public mutating func push(_ msgs: [Outgoing], connected: Bool) {
    guard connected else { return }
    for m in msgs {
      switch m {
      case .meta(let d):
        let id = Self.u32(d, at: 0)
        items.removeAll { ($0.kind == .artHeader || $0.kind == .artData) && $0.trackId != id }
        items.append(Item(kind: .meta, data: d, trackId: id))
      case .art(let parts):
        guard let header = parts.first else { continue }
        let id = Self.u32(header, at: 1)
        items.append(Item(kind: .artHeader, data: header, trackId: id))
        items.append(contentsOf: parts.dropFirst().map { Item(kind: .artData, data: $0, trackId: id) })
      case .state(let d):
        items.removeAll { $0.kind == .state }  // only the newest state matters
        items.append(Item(kind: .state, data: d, trackId: Self.u32(d, at: 4)))
      }
    }
  }

  public var first: Item? { items.first }
  public mutating func removeFirst() { items.removeFirst() }
  public mutating func clear() { items.removeAll() }

  static func u32(_ d: Data, at i: Int) -> UInt32 {
    let b = [UInt8](d)
    guard b.count >= i + 4 else { return 0 }
    return UInt32(b[i]) | UInt32(b[i + 1]) << 8 | UInt32(b[i + 2]) << 16 | UInt32(b[i + 3]) << 24
  }
}
