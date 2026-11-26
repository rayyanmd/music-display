import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

/// Turns any cover image into what the display decodes: 160x160 baseline JPEG <= 32 KB.
public enum ArtProcessor {
  public static let side = 160
  static let qualities: [CGFloat] = [0.75, 0.6, 0.45, 0.3]

  /// Center-crops to square, scales to 160x160, encodes baseline JPEG, lowering quality
  /// until it fits Wire.maxArt. nil if the data isn't a decodable image.
  public static func jpeg160(from imageData: Data) -> Data? {
    guard let src = CGImageSourceCreateWithData(imageData as CFData, nil),
          let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }
    guard let square = scaled(cropSquare(image)) else { return nil }
    for q in qualities {
      if let jpeg = encode(square, quality: q), jpeg.count <= Wire.maxArt { return jpeg }
    }
    return nil
  }

  static func cropSquare(_ img: CGImage) -> CGImage {
    let s = min(img.width, img.height)
    let rect = CGRect(x: (img.width - s) / 2, y: (img.height - s) / 2, width: s, height: s)
    return img.cropping(to: rect) ?? img
  }

  static func scaled(_ img: CGImage) -> CGImage? {
    guard let cs = CGColorSpace(name: CGColorSpace.sRGB),
          let ctx = CGContext(data: nil, width: side, height: side, bitsPerComponent: 8, bytesPerRow: 0,
                              space: cs, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return nil }
    ctx.interpolationQuality = .high
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: side, height: side))
    return ctx.makeImage()
  }

  static func encode(_ img: CGImage, quality: CGFloat) -> Data? {
    let out = NSMutableData()
    guard let dest = CGImageDestinationCreateWithData(out, UTType.jpeg.identifier as CFString, 1, nil) else {
      return nil
    }
    // ImageIO writes baseline JPEG unless progressive is requested; leave it unset.
    CGImageDestinationAddImage(dest, img, [kCGImageDestinationLossyCompressionQuality: quality] as CFDictionary)
    guard CGImageDestinationFinalize(dest) else { return nil }
    return out as Data
  }
}
