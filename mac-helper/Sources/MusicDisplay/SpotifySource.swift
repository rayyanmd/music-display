import AppKit
import Foundation
import HelperCore

/// Reads Spotify's now-playing state and sends it playback commands. Never launches Spotify.
@MainActor final class SpotifySource {
  nonisolated static let bundleId = "com.spotify.client"
  nonisolated static let notificationName = Notification.Name("com.spotify.client.PlaybackStateChanged")
  static let pollInterval: TimeInterval = 5

  var onChange: ((SpotifySnapshot?) -> Void)?
  private(set) var last: SpotifySnapshot?
  private var pollTimer: Timer?
  private var observers: [NSObjectProtocol] = []

  private static let queryScript = NSAppleScript(source: SpotifyScripts.query)

  var isRunning: Bool {
    !NSRunningApplication.runningApplications(withBundleIdentifier: Self.bundleId).isEmpty
  }

  func start() {
    let dnc = DistributedNotificationCenter.default()
    observers.append(dnc.addObserver(forName: Self.notificationName, object: nil, queue: .main) { [weak self] note in
      let fallback = note.userInfo.flatMap { SpotifyParse.notification($0) }
      MainActor.assumeIsolated { self?.refresh(fallback: fallback) }
    })
    let ws = NSWorkspace.shared.notificationCenter
    observers.append(ws.addObserver(forName: NSWorkspace.didTerminateApplicationNotification, object: nil,
                                    queue: .main) { [weak self] note in
      let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
      guard app?.bundleIdentifier == SpotifySource.bundleId else { return }
      MainActor.assumeIsolated { self?.publish(nil) }
    })
    pollTimer = Timer.scheduledTimer(withTimeInterval: Self.pollInterval, repeats: true) { [weak self] _ in
      MainActor.assumeIsolated {
        guard let self, self.last?.playing == true else { return }
        self.refresh(fallback: nil)
      }
    }
    refresh(fallback: nil)
  }

  /// Query Spotify via AppleScript (gives artwork URL and exact position); fall back to the notification.
  func refresh(fallback: SpotifySnapshot?) {
    guard isRunning else { return publish(nil) }
    var err: NSDictionary?
    if let text = Self.queryScript?.executeAndReturnError(&err).stringValue {
      if text == "stopped" { return publish(nil) }
      if let snap = SpotifyParse.appleScript(text) { return publish(snap) }
    } else if let err {
      NSLog("MusicDisplay: Spotify AppleScript failed: \(err)")
    }
    if let fallback {
      var snap = fallback
      if snap.uri == last?.uri { snap.artworkURL = last?.artworkURL }
      publish(snap)
    }
  }

  func send(_ c: DisplayCommand) {
    guard isRunning else { return }
    var err: NSDictionary?
    NSAppleScript(source: SpotifyScripts.command(c))?.executeAndReturnError(&err)
    if let err { NSLog("MusicDisplay: command failed: \(err)") }
    refresh(fallback: nil)
  }

  private func publish(_ snap: SpotifySnapshot?) {
    last = snap
    onChange?(snap)
  }
}
