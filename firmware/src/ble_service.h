// NimBLE GATT server for the now-playing protocol (protocol/PROTOCOL.md).
#pragma once
#include <cstdint>
#include <mutex>
#include "now_playing.h"
#include "protocol.h"

class BleService {
 public:
  // Init NimBLE, create the service, start advertising. Writes are decoded and applied to `model` under `lock`.
  void begin(NowPlaying& model, std::mutex& lock);
  // Notify Command to exactly one connection.
  void sendCommand(int conn, proto::Cmd c);
  // Backs the Info read characteristic (0xFF = unknown).
  void setBatteryPct(uint8_t pct);
};
