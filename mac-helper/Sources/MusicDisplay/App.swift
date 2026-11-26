import AppKit
import SwiftUI

@main
struct MusicDisplayApp: App {
  @StateObject private var coordinator = Coordinator()

  var body: some Scene {
    MenuBarExtra {
      Text(coordinator.status)
      Text(coordinator.nowPlaying)
      Divider()
      Toggle("Launch at login", isOn: $coordinator.launchAtLogin)
      Divider()
      Button("Quit Music Display") { NSApplication.shared.terminate(nil) }
        .keyboardShortcut("q")
    } label: {
      Image(systemName: coordinator.status.hasPrefix("Connected") ? "music.note.tv.fill" : "music.note.tv")
    }
    .menuBarExtraStyle(.menu)
  }
}
