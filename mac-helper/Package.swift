// swift-tools-version:6.0
import PackageDescription

let package = Package(
  name: "MusicDisplay",
  platforms: [.macOS(.v14)],
  targets: [
    .target(name: "HelperCore"),
    .executableTarget(name: "MusicDisplay", dependencies: ["HelperCore"]),
    .testTarget(name: "HelperCoreTests", dependencies: ["HelperCore"]),
  ]
)
