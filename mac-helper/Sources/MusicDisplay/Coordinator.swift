import Foundation
import HelperCore
import ServiceManagement

/// Wires Spotify -> SendPlanner -> BLE, and display commands -> Spotify.
@MainActor final class Coordinator: ObservableObject {
  @Published var status = "Starting…"
  @Published var nowPlaying = "Spotify not playing"
  @Published var launchAtLogin = SMAppService.mainApp.status == .enabled {
    didSet { if launchAtLogin != oldValue { applyLaunchAtLogin() } }
  }

  private let spotify = SpotifySource()
  private let ble = BLEClient()
  private var planner = SendPlanner(chunk: 500)
  private var ticker: Timer?
  private var artTask: Task<Void, Never>?

  private static var nowMs: UInt64 { UInt64(ProcessInfo.processInfo.systemUptime * 1000) }

  init() {
    ble.onStatus = { [weak self] s in self?.status = Self.describe(s) }
    ble.onReady = { [weak self] in
      guard let self else { return }
      self.planner.chunk = self.ble.chunkSize
      self.ble.send(self.planner.resendAll(nowMs: Self.nowMs))
    }
    ble.onCommand = { [weak self] cmd in self?.spotify.send(cmd) }
    spotify.onChange = { [weak self] snap in self?.spotifyChanged(snap) }

    ble.start()
    spotify.start()
    ticker = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
      MainActor.assumeIsolated {
        guard let self else { return }
        self.ble.send(self.planner.tick(nowMs: Self.nowMs))
      }
    }
  }

  private func spotifyChanged(_ snap: SpotifySnapshot?) {
    let out = planner.update(snap, nowMs: Self.nowMs)
    ble.send(out)
    if let snap {
      nowPlaying = "\(snap.playing ? "▶" : "⏸") \(snap.title) — \(snap.artist)"
      if out.contains(where: { if case .meta = $0 { return true } else { return false } }) {
        fetchArt(for: snap)
      }
    } else {
      nowPlaying = "Spotify not playing"
    }
  }

  private func fetchArt(for snap: SpotifySnapshot) {
    artTask?.cancel()
    guard let url = snap.artworkURL else { return }
    let uri = snap.uri
    artTask = Task { [weak self] in
      var req = URLRequest(url: url)
      req.timeoutInterval = 10
      guard let (data, _) = try? await URLSession.shared.data(for: req), !Task.isCancelled else { return }
      let jpeg = await Task.detached(priority: .utility) { ArtProcessor.jpeg160(from: data) }.value
      guard let jpeg, let self, !Task.isCancelled else { return }
      self.ble.send(self.planner.artReady(uri: uri, jpeg: jpeg))
    }
  }

  private func applyLaunchAtLogin() {
    do {
      if launchAtLogin { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
    } catch {
      NSLog("MusicDisplay: launch at login failed: \(error.localizedDescription)")
      launchAtLogin = SMAppService.mainApp.status == .enabled
    }
  }

  private static func describe(_ s: BLEClient.Status) -> String {
    switch s {
    case .bluetoothOff: "Bluetooth is off or not allowed"
    case .scanning: "Searching for display…"
    case .connecting: "Connecting…"
    case .connected(let name): "Connected to \(name)"
    }
  }
}
