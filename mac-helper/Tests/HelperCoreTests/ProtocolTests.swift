import Foundation
import Testing
@testable import HelperCore

@Test func trackIdMatchesReference() {
  #expect(Wire.trackId("spotify:track:abc") == 0x0b1b_c65e)
  #expect(Wire.trackId("spotify:track:4uLU6hMCjMI75M1A2tKUQC") == 0xf640_54a7)
  #expect(Wire.trackId("") == 0x811c_9dc5)
}

@Test func stateBytes() {
  let d = Wire.state(trackId: 0x1234_5678, playing: true, local: true, source: 1, positionMs: 1000, durationMs: 60000)
  #expect([UInt8](d) == [1, 3, 1, 0, 0x78, 0x56, 0x34, 0x12, 0xE8, 0x03, 0, 0, 0x60, 0xEA, 0, 0])
}

@Test func metaLayout() {
  let d = Wire.meta(trackId: 0x0102_0304, title: "A", artist: "B", album: "C")
  #expect([UInt8](d) == [4, 3, 2, 1] + Array("A\u{1F}B\u{1F}C".utf8))
}

@Test func metaTruncatesOnUTF8Boundary() {
  let title = String(repeating: "é", count: 200)
  let artist = String(repeating: "😀", count: 100)
  let d = Wire.meta(trackId: 1, title: title, artist: artist, album: "x")
  #expect(d.count <= 512)
  let fields = [UInt8](d.dropFirst(4)).split(separator: 0x1F, omittingEmptySubsequences: false)
  #expect(fields.count == 3)
  for f in fields {
    #expect(f.count <= 160)
    #expect(String(bytes: f, encoding: .utf8) != nil)
  }
  #expect(fields[1].count % 4 == 0)
  #expect(fields[1].count > 0)
}

@Test func artChunking() throws {
  let jpeg = Data((0..<1200).map { UInt8($0 & 0xFF) })
  let msgs = try #require(Wire.artMessages(trackId: 7, jpeg: jpeg, chunk: 500))
  #expect(msgs.count == 4)
  #expect([UInt8](msgs[0]) == [1, 7, 0, 0, 0, 0xB0, 0x04, 0, 0])
  var joined = Data()
  for (i, m) in msgs.dropFirst().enumerated() {
    let b = [UInt8](m)
    #expect(b[0] == 2)
    #expect(Array(b[1..<5]) == [7, 0, 0, 0])
    let off = UInt32(b[5]) | UInt32(b[6]) << 8 | UInt32(b[7]) << 16 | UInt32(b[8]) << 24
    #expect(off == UInt32(i * 500))
    joined.append(contentsOf: b[9...])
  }
  #expect(joined == jpeg)
  #expect(Wire.artMessages(trackId: 7, jpeg: Data(count: 32769), chunk: 500) == nil)
  #expect(Wire.artMessages(trackId: 7, jpeg: Data(), chunk: 500) == nil)
}
