import Foundation
import Testing
@testable import HelperCore

// MARK: LRC

@Test func lrcParsesStamps() {
  let l = LRC.parse("[00:01.50]A\n[00:03.123]B\n[01:00]C")
  #expect(l.map(\.timeMs) == [1500, 3123, 60000])
  #expect(l.map(\.text) == ["A", "B", "C"])
}

@Test func lrcMultipleStamps() {
  let l = LRC.parse("[00:05.00][00:01.00]X")
  #expect(l == [LyricLine(timeMs: 1000, text: "X"), LyricLine(timeMs: 5000, text: "X")])
}

@Test func lrcOffsetPositiveEarlier() {
  #expect(LRC.parse("[offset:500]\n[00:01.00]A").map(\.timeMs) == [500])
  #expect(LRC.parse("[offset:1500]\n[00:01.00]A").map(\.timeMs) == [0])
  #expect(LRC.parse("[offset:-500]\n[00:01.00]A").map(\.timeMs) == [1500])
}

@Test func lrcIgnoresTagsAndCRLF() {
  #expect(LRC.parse("[ar:Foo]\r\n[00:02.00]A\r\n") == [LyricLine(timeMs: 2000, text: "A")])
}

@Test func lrcKeepsInnerEmptyLine() {
  let l = LRC.parse("[00:00.50]\n[00:01.00]A\n[00:02.00]\n[00:03.00]B\n[00:04.00] ")
  #expect(l.map(\.text) == ["A", "", "B"])
}

@Test func plainZeroTimes() {
  let l = LRC.plain("\n\nOne\n\nTwo\r\n\n")
  #expect(l.map(\.text) == ["One", "", "Two"])
  #expect(l.allSatisfy { $0.timeMs == 0 })
}

// MARK: matching

private func hit(_ artist: String, _ dur: Double, synced: Bool = true, plain: Bool = true,
                 instrumental: Bool = false) -> LrclibTrack {
  LrclibTrack(trackName: "Song", artistName: artist, duration: dur, instrumental: instrumental,
              plainLyrics: plain ? "p" : nil, syncedLyrics: synced ? "[00:01.00]s" : nil)
}

@Test func cleanTitleStripsRemaster() {
  #expect(LyricsMatch.cleanTitle("Hey Jude - Remastered 2015") == "Hey Jude")
  #expect(LyricsMatch.cleanTitle("Let It Be - 2009 Remaster") == "Let It Be")
  #expect(LyricsMatch.cleanTitle("Song - Live at Wembley") == "Song")
  #expect(LyricsMatch.cleanTitle("Song - Radio Edit") == "Song")
  #expect(LyricsMatch.cleanTitle("Song - Single Version") == "Song")
  #expect(LyricsMatch.cleanTitle("Song - Mono") == "Song")
}

@Test func cleanTitleStripsFeat() {
  #expect(LyricsMatch.cleanTitle("Song (feat. X)") == "Song")
  #expect(LyricsMatch.cleanTitle("Song (with Y)") == "Song")
  #expect(LyricsMatch.cleanTitle("Song [feat. Z]") == "Song")
}

@Test func cleanTitleKeepsHyphenatedName() {
  #expect(LyricsMatch.cleanTitle("Re-Wired") == "Re-Wired")
  #expect(LyricsMatch.cleanTitle("Love - Hate") == "Love - Hate")
}

@Test func firstArtistSplits() {
  #expect(LyricsMatch.firstArtist("A, B") == "A")
  #expect(LyricsMatch.firstArtist("A & B") == "A")
  #expect(LyricsMatch.firstArtist("A feat. B") == "A")
  #expect(LyricsMatch.firstArtist("A x B") == "A")
  #expect(LyricsMatch.firstArtist("Solo") == "Solo")
}

@Test func bestPrefersSynced() {
  let plainOnly = hit("Artist", 200, synced: false)
  let synced = hit("Artist", 201.5)
  #expect(LyricsMatch.best([plainOnly, synced], artist: "Artist", durationS: 200) == synced)
}

@Test func bestPrefersCloserDuration() {
  let far = hit("Artist", 202.5)
  let near = hit("Artist", 200.4)
  #expect(LyricsMatch.best([far, near], artist: "Artist", durationS: 200) == near)
}

@Test func bestRejectsOtherArtist() {
  #expect(LyricsMatch.best([hit("Someone Else", 200)], artist: "Artist", durationS: 200) == nil)
}

@Test func bestRejectsDurationOff() {
  #expect(LyricsMatch.best([hit("Artist", 203.5)], artist: "Artist", durationS: 200) == nil)
}

@Test func bestDiacriticInsensitive() {
  #expect(LyricsMatch.best([hit("Beyonce", 200)], artist: "Beyoncé", durationS: 200) != nil)
  #expect(LyricsMatch.best([hit("Beyoncé, JAY-Z", 200)], artist: "beyonce", durationS: 200) != nil)
}

@Test func resultFromJSONFixture() throws {
  let json = #"{"id":1,"trackName":"Song","artistName":"A","albumName":"B","duration":200,"instrumental":false,"plainLyrics":"One\nTwo","syncedLyrics":null}"#
  let t = try JSONDecoder().decode(LrclibTrack.self, from: Data(json.utf8))
  let r = LyricsMatch.result(from: t)
  #expect(r.kind == .plain)
  #expect(r.lines.map(\.text) == ["One", "Two"])
}

@Test func resultSyncedPreferred() {
  #expect(LyricsMatch.result(from: hit("A", 1)).kind == .synced)
}

@Test func resultInstrumental() {
  #expect(LyricsMatch.result(from: hit("A", 1, synced: false, plain: false, instrumental: true)).kind == .instrumental)
}

@Test func resultNoneForNilOrEmpty() {
  #expect(LyricsMatch.result(from: nil).kind == .none)
  #expect(LyricsMatch.result(from: hit("A", 1, synced: false, plain: false)).kind == .none)
}

// MARK: encoding

@Test func lyricsMessagesHeaderLayout() {
  let r = LyricsResult(kind: .synced, lines: [LyricLine(timeMs: 1000, text: "Hi")])
  let m = Wire.lyricsMessages(trackId: 0x0102_0304, result: r, chunk: 500)
  #expect(m.count == 2)
  #expect([UInt8](m[0]) == [1, 4, 3, 2, 1, 7, 0, 0, 0, 1])
  #expect([UInt8](m[1]) == [2, 4, 3, 2, 1, 0, 0, 0, 0, 0xE8, 0x03, 0, 0, 2] + Array("Hi".utf8))
}

@Test func lyricsMessagesNoneHeaderOnly() {
  let m = Wire.lyricsMessages(trackId: 5, result: LyricsResult(kind: .none, lines: []), chunk: 500)
  #expect(m.count == 1)
  #expect([UInt8](m[0]) == [1, 5, 0, 0, 0, 0, 0, 0, 0, 0])
  let i = Wire.lyricsMessages(trackId: 5, result: LyricsResult(kind: .instrumental, lines: [LyricLine(timeMs: 0, text: "x")]), chunk: 500)
  #expect(i.count == 1)
  #expect([UInt8](i[0]).last == 3)
}

@Test func lyricsMessagesChunked() {
  let lines = (0..<50).map { LyricLine(timeMs: UInt32($0) * 1000, text: String(repeating: "a", count: 40)) }
  let m = Wire.lyricsMessages(trackId: 1, result: LyricsResult(kind: .synced, lines: lines), chunk: 100)
  let body = m.dropFirst().flatMap { [UInt8]($0.dropFirst(9)) }
  #expect(body.count == 50 * 45)
  #expect(m.dropFirst().allSatisfy { $0.count <= 109 })
}

@Test func lyricsEncoderTruncatesTo16K() {
  let lines = (0..<400).map { LyricLine(timeMs: UInt32($0) * 100, text: String(repeating: "b", count: 100)) }
  let m = Wire.lyricsMessages(trackId: 1, result: LyricsResult(kind: .synced, lines: lines), chunk: 500)
  let total = WriteQueue.u32(m[0], at: 5)
  #expect(total <= 16384)
  #expect(total % 105 == 0)  // whole lines only
  #expect(Int(total) == m.dropFirst().reduce(0) { $0 + $1.count - 9 })
}

@Test func lyricsLineCutOnUTF8Boundary() {
  let r = LyricsResult(kind: .plain, lines: [LyricLine(timeMs: 0, text: String(repeating: "é", count: 120))])
  let m = Wire.lyricsMessages(trackId: 1, result: r, chunk: 500)
  let body = [UInt8](m[1].dropFirst(9))
  #expect(body[4] == 200)
  #expect(String(bytes: body[5...], encoding: .utf8) != nil)
}

// MARK: lookup gate

private func fetch(_ g: inout LyricsGate, uri: String, title: String, artist: String, nowMs: UInt64) -> Bool {
  g.shouldFetch(uri: uri, title: title, artist: artist, nowMs: nowMs)
}

@Test func gateSkipsBlankNames() {
  var g = LyricsGate()
  #expect(fetch(&g, uri: "u", title: "", artist: "A", nowMs: 0) == false)
  #expect(fetch(&g, uri: "u", title: "T", artist: "", nowMs: 0) == false)
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 0) == true)  // names filled in later
}

@Test func gateFetchesOncePerTrack() {
  var g = LyricsGate()
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 0) == true)
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 100) == false)
  g.finished(uri: "u", success: true, nowMs: 200)
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 999_999) == false)
}

@Test func gateRetriesFailureAfterBackoff() {
  var g = LyricsGate()
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 0) == true)
  g.finished(uri: "u", success: false, nowMs: 1000)
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 1000 + LyricsGate.retryAfterMs - 1) == false)
  #expect(fetch(&g, uri: "u", title: "T", artist: "A", nowMs: 1000 + LyricsGate.retryAfterMs) == true)
}

@Test func gateNewTrackAlwaysFetches() {
  var g = LyricsGate()
  #expect(fetch(&g, uri: "a", title: "T", artist: "A", nowMs: 0) == true)
  g.finished(uri: "a", success: false, nowMs: 10)
  #expect(fetch(&g, uri: "b", title: "T", artist: "A", nowMs: 20) == true)
  g.finished(uri: "a", success: false, nowMs: 30)  // late result for an old track changes nothing
  #expect(fetch(&g, uri: "b", title: "T", artist: "A", nowMs: 40) == false)
}

@Test func lyricsEncoderCapsLineCount() {
  let lines = (0..<1200).map { LyricLine(timeMs: UInt32($0), text: "") }
  let m = Wire.lyricsMessages(trackId: 1, result: LyricsResult(kind: .synced, lines: lines), chunk: 500)
  #expect(WriteQueue.u32(m[0], at: 5) == UInt32(Wire.maxLyricLines * 5))
}
