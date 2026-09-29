#pragma once
// The host never connects. BadgeDisplay, DiagnosticsScreen and WifiScreen read
// this object for status only; WiFiService is replaced by a host class.
#include "Arduino.h"

typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED,
               WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED } wl_status_t;
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP WIFI_MODE_AP
#define WIFI_AP_STA WIFI_MODE_APSTA
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;

class IPAddress {
 public:
  String toString() const { return String("0.0.0.0"); }
  operator uint32_t() const { return 0; }
};

class WiFiClass {
 public:
  wl_status_t status() { return WL_DISCONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  String SSID() { return String(); }
  int32_t RSSI() { return 0; }
  bool mode(wifi_mode_t) { return true; }
  wifi_mode_t getMode() { return WIFI_MODE_NULL; }
  bool disconnect(bool = false, bool = false) { return true; }
  bool setSleep(bool) { return true; }
  bool setSleep(wifi_ps_type_t) { return true; }
  wifi_ps_type_t getSleep() { return WIFI_PS_NONE; }
  bool setHostname(const char*) { return true; }
  bool setAutoReconnect(bool) { return true; }
  bool persistent(bool) { return true; }
};
extern WiFiClass WiFi;
