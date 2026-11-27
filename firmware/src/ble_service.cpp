#include "ble_service.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>

namespace {
const char* kSvcUuid = "6e0a0000-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kStateUuid = "6e0a0001-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kMetaUuid = "6e0a0002-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kArtUuid = "6e0a0003-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kCmdUuid = "6e0a0004-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kInfoUuid = "6e0a0005-7b5d-4c1e-9f3a-2d8c5b1e4a70";
const char* kLyricsUuid = "6e0a0006-7b5d-4c1e-9f3a-2d8c5b1e4a70";
constexpr uint8_t kProtoVer = 1;

NowPlaying* gModel = nullptr;
std::mutex* gLock = nullptr;
NimBLECharacteristic* gCmd = nullptr;
NimBLECharacteristic* gInfo = nullptr;
uint8_t gBattery = 0xFF;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& ci) override {
    {
      std::lock_guard<std::mutex> g(*gLock);
      gModel->onConnect(ci.getConnHandle());
    }
    log_i("connect %u", ci.getConnHandle());
    NimBLEDevice::startAdvertising();  // keep accepting more centrals
  }
  void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& ci, int reason) override {
    {
      std::lock_guard<std::mutex> g(*gLock);
      gModel->onDisconnect(ci.getConnHandle());
    }
    log_i("disconnect %u reason %d", ci.getConnHandle(), reason);
    NimBLEDevice::startAdvertising();
  }
};

enum class Kind { State, Meta, Art, Lyrics };

class WriteCb : public NimBLECharacteristicCallbacks {
 public:
  explicit WriteCb(Kind k) : kind_(k) {}
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& ci) override {
    NimBLEAttValue v = chr->getValue();
    const uint8_t* d = v.data();
    size_t n = v.size();
    uint16_t conn = ci.getConnHandle();
    std::lock_guard<std::mutex> g(*gLock);
    switch (kind_) {
      case Kind::State: {
        proto::State s;
        if (proto::decodeState(d, n, s)) gModel->onState(conn, s, millis());
        else log_d("bad State len %u", (unsigned)n);
        break;
      }
      case Kind::Meta: {
        proto::Meta m;
        if (proto::decodeMeta(d, n, m)) gModel->onMeta(conn, m);
        else log_d("bad Meta len %u", (unsigned)n);
        break;
      }
      case Kind::Art:
        gModel->onArt(conn, d, n);
        break;
      case Kind::Lyrics:
        gModel->onLyrics(conn, d, n);
        break;
    }
  }

 private:
  Kind kind_;
};

ServerCb gServerCb;
WriteCb gStateCb(Kind::State), gMetaCb(Kind::Meta), gArtCb(Kind::Art), gLyricsCb(Kind::Lyrics);

void updateInfo() {
  if (!gInfo) return;
  uint8_t v[2] = {kProtoVer, gBattery};
  gInfo->setValue(v, sizeof v);
}
}  // namespace

void BleService::begin(NowPlaying& model, std::mutex& lock) {
  gModel = &model;
  gLock = &lock;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  char name[24];
  snprintf(name, sizeof name, "NowPlaying-%02X%02X", mac[4], mac[5]);

  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(&gServerCb, false);

  NimBLEService* svc = srv->createService(kSvcUuid);
  svc->createCharacteristic(kStateUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE, 16)
      ->setCallbacks(&gStateCb);
  svc->createCharacteristic(kMetaUuid, NIMBLE_PROPERTY::WRITE, proto::kMaxMeta)->setCallbacks(&gMetaCb);
  svc->createCharacteristic(kArtUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE, 512)
      ->setCallbacks(&gArtCb);
  svc->createCharacteristic(kLyricsUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE, 512)
      ->setCallbacks(&gLyricsCb);
  gCmd = svc->createCharacteristic(kCmdUuid, NIMBLE_PROPERTY::NOTIFY, 1);
  gInfo = svc->createCharacteristic(kInfoUuid, NIMBLE_PROPERTY::READ, 2);
  updateInfo();
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kSvcUuid);
  adv->enableScanResponse(true);  // device name goes in the scan response (128-bit UUID fills the adv packet)
  adv->start();
  log_i("advertising as %s", name);
}

void BleService::sendCommand(int conn, proto::Cmd c) {
  if (!gCmd || conn < 0) return;
  uint8_t b = static_cast<uint8_t>(c);
  gCmd->notify(&b, 1, static_cast<uint16_t>(conn));
}

void BleService::setBatteryPct(uint8_t pct) {
  gBattery = pct;
  updateInfo();
}
