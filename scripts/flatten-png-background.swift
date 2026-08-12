import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

guard CommandLine.arguments.count == 3 else {
    FileHandle.standardError.write(Data("Usage: flatten-png-background.swift INPUT OUTPUT\n".utf8))
    exit(2)
}

let inputURL = URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL
let outputURL = URL(fileURLWithPath: CommandLine.arguments[2]) as CFURL

guard let source = CGImageSourceCreateWithURL(inputURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    FileHandle.standardError.write(Data("Unable to read input image.\n".utf8))
    exit(1)
}

let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
let bitmapInfo = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue)
guard let context = CGContext(data: nil,
                              width: image.width,
                              height: image.height,
                              bitsPerComponent: 8,
                              bytesPerRow: 0,
                              space: colorSpace,
                              bitmapInfo: bitmapInfo.rawValue) else {
    FileHandle.standardError.write(Data("Unable to create image context.\n".utf8))
    exit(1)
}

context.setFillColor(red: 1, green: 1, blue: 1, alpha: 1)
context.fill(CGRect(x: 0, y: 0, width: image.width, height: image.height))
context.interpolationQuality = .none
context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))

guard let flattened = context.makeImage(),
      let destination = CGImageDestinationCreateWithURL(
          outputURL, UTType.png.identifier as CFString, 1, nil
      ) else {
    FileHandle.standardError.write(Data("Unable to create output image.\n".utf8))
    exit(1)
}

CGImageDestinationAddImage(destination, flattened, nil)
guard CGImageDestinationFinalize(destination) else {
    FileHandle.standardError.write(Data("Unable to write output image.\n".utf8))
    exit(1)
}
