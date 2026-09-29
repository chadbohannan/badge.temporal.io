// Host WiFiService: never connected. status() reports disconnected, and a
// connect attempt walks to the Failed phase so WifiScreen shows its real
// failure popup. Same header as api/WiFiService.cpp.

#include "api/WiFiService.h"

WiFiService wifiService;

void WiFiService::begin() {}

bool WiFiService::connect() {
  noteConnectionFailed();
  return false;
}

void WiFiService::disconnect() {}

bool WiFiService::isConnected() const { return false; }

bool WiFiService::connectToSlotAsync(uint8_t) {
  if (asyncConnectInFlight_) return false;
  setPhaseStatus("No WiFi on the host");
  setPhase(Phase::kFailed);
  noteConnectionFailed();
  return true;
}

bool WiFiService::connectSavedNetworksAsync() { return connectToSlotAsync(0); }

void WiFiService::runSlotConnect(uint8_t) {}
void WiFiService::runSavedNetworksConnect() {}

void WiFiService::dismissPhase() {
  if (phase_ == Phase::kConnected || phase_ == Phase::kFailed) {
    phase_ = Phase::kIdle;
    phaseStatusText_[0] = '\0';
    phaseChangedMs_ = millis();
  }
}

bool WiFiService::clockReady() const { return false; }
bool WiFiService::currentTime(time_t*) const { return false; }
int WiFiService::rssi() const { return 0; }
uint8_t WiFiService::signalLevel() const { return 0; }

void WiFiService::noteConnectionOk() {}
void WiFiService::noteConnectionFailed() { lastNetworkFailMs_ = millis(); }
void WiFiService::noteRequestOk() {}
void WiFiService::noteRequestFailed() {}

void WiFiService::refreshClockState() const {}

void WiFiService::setPhase(Phase p) {
  phase_ = p;
  phaseChangedMs_ = millis();
}

void WiFiService::setPhaseStatus(const char* s) {
  snprintf(phaseStatusText_, sizeof phaseStatusText_, "%s", s ? s : "");
}

bool WiFiService::pollStaAssociation(uint32_t, const char*, const char*) { return false; }
