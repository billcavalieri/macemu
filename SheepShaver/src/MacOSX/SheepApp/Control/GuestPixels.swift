/*
 *  GuestPixels.swift - Turns a copy of the guest framebuffer into RGBA and PNG for screenshots.
 *
 *  Pure Foundation and ImageIO, no AppKit and no emulator: compiled on its own by tools/vms/tests/unit.sh. The guest
 *  framebuffer is big-endian: 32-bit is xRGB, 16-bit is 5-5-5, and 1/2/4/8-bit are indexed (first pixel in the high bits)
 *  through the 256-entry palette the emulator hands over already gamma-corrected.
 *
 *  (C) 2026 Bill Cavalieri
 *  Part of SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

import Foundation
import ImageIO
import UniformTypeIdentifiers

struct GuestFrame: Equatable {
    var width: Int
    var height: Int
    var rowBytes: Int
    var depth: Int              // bits per pixel: 1, 2, 4, 8, 16 or 32
    var pixels: [UInt8]         // rowBytes * height bytes, as the guest wrote them
    var palette: [UInt8]        // 256 RGB triples (indexed depths only)

    /// Whether the numbers describe a buffer that can be read without going out of bounds.
    var isValid: Bool {
        guard [1, 2, 4, 8, 16, 32].contains(depth), width > 0, height > 0, width <= 16384, height <= 16384 else { return false }
        guard rowBytes >= (width * depth + 7) / 8, pixels.count >= rowBytes * height else { return false }
        return depth > 8 || palette.count >= 256 * 3
    }

    /// RGBA, 8 bits per channel, opaque, `width * height * 4` bytes. Nil when the frame is not valid.
    func rgba() -> [UInt8]? {
        guard isValid else { return nil }
        var out = [UInt8](repeating: 255, count: width * height * 4)
        pixels.withUnsafeBufferPointer { src in
            for y in 0..<height {
                let row = y * rowBytes
                var o = y * width * 4
                for x in 0..<width {
                    let r: UInt8, g: UInt8, b: UInt8
                    switch depth {
                    case 32:
                        let p = row + x * 4
                        r = src[p + 1]; g = src[p + 2]; b = src[p + 3]
                    case 16:
                        let p = row + x * 2
                        let v = (Int(src[p]) << 8) | Int(src[p + 1])
                        r = Self.expand5((v >> 10) & 31); g = Self.expand5((v >> 5) & 31); b = Self.expand5(v & 31)
                    default:
                        let perByte = 8 / depth
                        let byte = Int(src[row + x / perByte])
                        let shift = 8 - depth * (x % perByte + 1)
                        let index = (byte >> shift) & ((1 << depth) - 1)
                        r = palette[index * 3]; g = palette[index * 3 + 1]; b = palette[index * 3 + 2]
                    }
                    out[o] = r; out[o + 1] = g; out[o + 2] = b
                    o += 4
                }
            }
        }
        return out
    }

    private static func expand5(_ v: Int) -> UInt8 { UInt8((v << 3) | (v >> 2)) }

    /// RGBA scaled down (nearest neighbour, so it is exact and repeatable) to at most `maxWidth` pixels wide.
    func scaledRGBA(maxWidth: Int?) -> (width: Int, height: Int, rgba: [UInt8])? {
        guard let full = rgba() else { return nil }
        guard let maxWidth, maxWidth > 0, maxWidth < width else { return (width, height, full) }
        let w = maxWidth
        let h = max(1, height * maxWidth / width)
        var out = [UInt8](repeating: 255, count: w * h * 4)
        for y in 0..<h {
            let sy = min(height - 1, y * height / h)
            for x in 0..<w {
                let sx = min(width - 1, x * width / w)
                let s = (sy * width + sx) * 4, d = (y * w + x) * 4
                out[d] = full[s]; out[d + 1] = full[s + 1]; out[d + 2] = full[s + 2]
            }
        }
        return (w, h, out)
    }

    /// PNG bytes of the picture, optionally scaled to `maxWidth`. Nil when the frame is not valid.
    func pngData(maxWidth: Int? = nil) -> Data? {
        guard let scaled = scaledRGBA(maxWidth: maxWidth) else { return nil }
        return GuestFrame.png(width: scaled.width, height: scaled.height, rgba: scaled.rgba)
    }

    static func png(width: Int, height: Int, rgba: [UInt8]) -> Data? {
        guard rgba.count == width * height * 4,
              let provider = CGDataProvider(data: Data(rgba) as CFData),
              let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
        else { return nil }
        let data = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { return nil }
        return data as Data
    }
}
