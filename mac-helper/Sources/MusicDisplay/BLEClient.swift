@preconcurrency import CoreBluetooth
import Foundation
import HelperCore

/// CoreBluetooth central that finds the display, keeps it connected and writes protocol messages in order.
@MainActor final class BLEClient: NSObject {
  enum Status: Equatable {
    case bluetoothOff, scanning, connecting, connected(name: String)
  }

  private(set) var status: Status = .bluetoothOff {
    didSet { if status != oldValue { onStatus?(status) } }
  }
  var onStatus: ((Status) -> Void)?
  var onCommand: ((DisplayCommand) -> Void)?
  var onReady: (() -> Void)?

  private static let defaultsKey = "displayId"
  private let serviceUUID = CBUUID(string: NP.service)
  private var central: CBCentralManager!
  private var peripheral: CBPeripheral?
  private var chars: [CBUUID: CBCharacteristic] = [:]
  private var ready = false

  private enum Kind { case meta, artHeader, artData, state }
  private struct Write {
    let kind: Kind
    let data: Data
  }
  private var queue: [Write] = []
  private var awaitingResponse = false
  private var scanStartedAt = Date.distantPast

  /// Art payload bytes per write-without-response for the current link.
  var chunkSize: Int {
    guard let p = peripheral else { return 500 }
    return max(1, min(500, p.maximumWriteValueLength(for: .withoutResponse) - 9))
  }

  func start() {
    central = CBCentralManager(delegate: self, queue: .main)
  }

  func send(_ msgs: [Outgoing]) {
    for m in msgs {
      switch m {
      case .meta(let d):
        queue.removeAll { $0.kind == .artHeader || $0.kind == .artData }  // stale art for an older track
        queue.append(Write(kind: .meta, data: d))
      case .art(let parts):
        guard let header = parts.first else { continue }
        queue.append(Write(kind: .artHeader, data: header))
        queue.append(contentsOf: parts.dropFirst().map { Write(kind: .artData, data: $0) })
      case .state(let d):
        queue.removeAll { $0.kind == .state }  // only the newest state matters
        queue.append(Write(kind: .state, data: d))
      }
    }
    drain()
  }

  private func drain() {
    guard ready, let p = peripheral else { return }
    while !awaitingResponse, let w = queue.first {
      let uuid: String
      let type: CBCharacteristicWriteType
      switch w.kind {
      case .meta: (uuid, type) = (NP.meta, .withResponse)
      case .artHeader: (uuid, type) = (NP.art, .withResponse)
      case .artData: (uuid, type) = (NP.art, .withoutResponse)
      case .state: (uuid, type) = (NP.state, .withoutResponse)
      }
      guard let c = chars[CBUUID(string: uuid)] else { queue.removeFirst(); continue }
      if type == .withoutResponse && !p.canSendWriteWithoutResponse { return }  // resumes in peripheralIsReady
      queue.removeFirst()
      p.writeValue(w.data, for: c, type: type)
      if type == .withResponse { awaitingResponse = true }  // keep header before its data
    }
  }

  private func scan() {
    guard central.state == .poweredOn else { return }
    status = .scanning
    scanStartedAt = Date()
    central.scanForPeripherals(withServices: [serviceUUID], options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
  }

  private func reset() {
    peripheral = nil
    chars = [:]
    ready = false
    awaitingResponse = false
    queue.removeAll()
  }
}

extension BLEClient: @preconcurrency CBCentralManagerDelegate {
  func centralManagerDidUpdateState(_ c: CBCentralManager) {
    if c.state == .poweredOn {
      scan()
    } else {
      reset()
      status = .bluetoothOff
    }
  }

  func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String: Any],
                      rssi: NSNumber) {
    let remembered = UserDefaults.standard.string(forKey: Self.defaultsKey)
    if let remembered, remembered != p.identifier.uuidString {
      // Prefer the display we used before, but accept another after a few seconds of not seeing it.
      if Date().timeIntervalSince(scanStartedAt) < 5 { return }
    }
    c.stopScan()
    peripheral = p
    p.delegate = self
    status = .connecting
    c.connect(p)
  }

  func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
    UserDefaults.standard.set(p.identifier.uuidString, forKey: Self.defaultsKey)
    p.discoverServices([serviceUUID])
  }

  func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
    reset()
    retryScan()
  }

  func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
    reset()
    retryScan()
  }

  private func retryScan() {
    status = .scanning
    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
      MainActor.assumeIsolated { self?.scan() }
    }
  }
}

extension BLEClient: @preconcurrency CBPeripheralDelegate {
  func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
    guard let svc = p.services?.first(where: { $0.uuid == serviceUUID }) else {
      central.cancelPeripheralConnection(p)
      return
    }
    p.discoverCharacteristics(nil, for: svc)
  }

  func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor svc: CBService, error: Error?) {
    for c in svc.characteristics ?? [] { chars[c.uuid] = c }
    guard let cmd = chars[CBUUID(string: NP.command)] else {
      central.cancelPeripheralConnection(p)
      return
    }
    p.setNotifyValue(true, for: cmd)
  }

  func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
    guard c.uuid == CBUUID(string: NP.command), error == nil, !ready else { return }
    ready = true
    status = .connected(name: p.name ?? "display")
    onReady?()
    drain()
  }

  func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
    guard c.uuid == CBUUID(string: NP.command), let b = c.value?.first,
          let cmd = DisplayCommand(rawValue: b) else { return }
    onCommand?(cmd)
  }

  func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
    if let error { NSLog("MusicDisplay: write failed: \(error.localizedDescription)") }
    awaitingResponse = false
    drain()
  }

  func peripheralIsReady(toSendWriteWithoutResponse p: CBPeripheral) {
    drain()
  }
}
