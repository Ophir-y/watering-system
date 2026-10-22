// HTTP server: REST API under /api (see docs/api.md), live updates over
// Server-Sent Events at /api/stream, firmware upload, and the embedded dashboard.
#pragma once

#include <ESPAsyncWebServer.h>

#include <atomic>
#include <string>

class Auth;
class Hub;
class LogStore;
class Notifier;
class TimeKeeper;
class WifiService;

namespace plantlink {
struct SystemInfo;
}

class WebApi {
 public:
  WebApi(Hub& hub, Auth& auth, WifiService& wifi, LogStore& logs, TimeKeeper& clock, Notifier& notifier)
      : server_(80), events_("/api/stream"), hub_(hub), auth_(auth), wifi_(wifi), logs_(logs), clock_(clock),
        notifier_(notifier) {}

  void begin();
  void pushStatus();  // network task; no-op without connected clients
  void pushEvent(const char* type, const std::string& json);
  bool rebootRequested() const { return rebootAtMs_ != 0 && static_cast<int32_t>(millis() - rebootAtMs_) >= 0; }

  void dispatch(AsyncWebServerRequest* r);
  void serveStatic(AsyncWebServerRequest* r);

 private:
  plantlink::SystemInfo systemInfo();
  bool authorized(AsyncWebServerRequest* r);
  void scheduleReboot(uint32_t delayMs) { rebootAtMs_ = millis() + delayMs; }
  void handleOtaUpload(AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len,
                       bool final);
  void handleExport(AsyncWebServerRequest* r);

  AsyncWebServer server_;
  AsyncEventSource events_;
  Hub& hub_;
  Auth& auth_;
  WifiService& wifi_;
  LogStore& logs_;
  TimeKeeper& clock_;
  Notifier& notifier_;
  std::atomic<uint32_t> rebootAtMs_{0};
  bool otaAuthorized_ = false;
  std::string otaError_;
};
