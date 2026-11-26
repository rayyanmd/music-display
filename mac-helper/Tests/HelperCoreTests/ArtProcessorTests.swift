import CoreGraphics
import Foundation
import ImageIO
import Testing
import UniformTypeIdentifiers
@testable import HelperCore

/// A noisy gradient PNG (compresses poorly, like real cover art).
private func makePNG(width: Int, height: Int) -> Data {
  let cs = CGColorSpaceCreateDeviceRGB()
  let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                      space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
  var seed: UInt32 = 12345
  for y in stride(from: 0, to: height, by: 8) {
    for x in stride(from: 0, to: width, by: 8) {
      seed = seed &* 1_103_515_245 &+ 12345
      let n = CGFloat(seed >> 24) / 255
      ctx.setFillColor(red: CGFloat(x) / CGFloat(width), green: n, blue: CGFloat(y) / CGFloat(height), alpha: 1)
      ctx.fill(CGRect(x: x, y: y, width: 8, height: 8))
    }
  }
  let img = ctx.makeImage()!
  let out = NSMutableData()
  let dest = CGImageDestinationCreateWithData(out, UTType.png.identifier as CFString, 1, nil)!
  CGImageDestinationAddImage(dest, img, nil)
  CGImageDestinationFinalize(dest)
  return out as Data
}

private func contains(_ d: Data, _ marker: [UInt8]) -> Bool {
  let b = [UInt8](d)
  guard b.count >= marker.count else { return false }
  for i in 0...(b.count - marker.count) where b[i] == marker[0] && b[i + 1] == marker[1] { return true }
  return false
}

@Test func artFromLargeNonSquareImage() throws {
  let jpeg = try #require(ArtProcessor.jpeg160(from: makePNG(width: 3000, height: 2000)))
  #expect(jpeg.count <= 32768)
  #expect(Array(jpeg.prefix(2)) == [0xFF, 0xD8])
  #expect(contains(jpeg, [0xFF, 0xC0]))
  #expect(!contains(jpeg, [0xFF, 0xC2]))
  let src = try #require(CGImageSourceCreateWithData(jpeg as CFData, nil))
  let img = try #require(CGImageSourceCreateImageAtIndex(src, 0, nil))
  #expect(img.width == 160)
  #expect(img.height == 160)
}

@Test func artFromGarbageReturnsNil() {
  #expect(ArtProcessor.jpeg160(from: Data("hello".utf8)) == nil)
}
