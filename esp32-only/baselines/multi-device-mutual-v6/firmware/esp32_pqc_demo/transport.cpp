#include "transport.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <string.h>
#include <atomic>
#include <lwip/sockets.h>
#include <errno.h>
#include "tx_options.h"
#if defined(PQC_USE_WIFI)
// Build flags may supply all four settings (also used by native tests).
#elif __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define PQC_USE_WIFI false
#define PQC_WIFI_SSID ""
#define PQC_WIFI_PASSWORD ""
#define PQC_TCP_PORT 9000
#endif

namespace demo_transport {
WiFiServer server(PQC_TCP_PORT);
WiFiClient client;
bool networkStarted = false;
bool serverRunning = false;
std::atomic<uint32_t> disconnectGeneration{0};
std::atomic<unsigned int> disconnectReason{0};
uint32_t handledGeneration = 0;
uint32_t retryAt = 0, retryDelay = 1000, retryCount = 0;
// A vanished Host may never deliver FIN/RST after losing WiFi. Do not let
// its half-open socket own the single-client protocol loop indefinitely.
constexpr uint32_t kClientIdleTimeoutMs = 30000;
uint32_t lastCommandActivity = 0;
bool clientAccepted = false;
char discoveryFingerprint[65] = {};
bool discoveryRunning = false;
uint32_t discoveryRetryAt = 0;

void stopDiscovery() {
  if (discoveryRunning) MDNS.end();
  discoveryRunning = false;
  discoveryRetryAt = 0;
}

void startDiscovery() {
  if (discoveryRunning || !discoveryFingerprint[0] ||
      static_cast<int32_t>(millis() - discoveryRetryAt) < 0) return;
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "pqc-camera-%.12s", discoveryFingerprint);
  discoveryRetryAt = millis() + 10000;
  if (!MDNS.begin(hostname)) {
    Serial.println("[MDNS] start failed; retry in 10 seconds");
    return;
  }
  MDNS.addService("pqc-camera", "tcp", PQC_TCP_PORT);
  // Arduino's mutable-char overload returns bool; const-char overload is void.
  char service[] = "pqc-camera", protocol[] = "tcp";
  char identityKey[] = "fingerprint", versionKey[] = "discovery", version[] = "1";
  if (!MDNS.addServiceTxt(service, protocol, identityKey, discoveryFingerprint) ||
      !MDNS.addServiceTxt(service, protocol, versionKey, version)) {
    MDNS.end();
    Serial.println("[MDNS] service registration failed; retry in 10 seconds");
    return;
  }
  discoveryRunning = true;
  Serial.printf("[MDNS] hostname=%s.local service=_pqc-camera._tcp port=%u\n", hostname, PQC_TCP_PORT);
}

void advertiseIdentity(const char *fingerprint) {
  if (!PQC_USE_WIFI || !fingerprint || strlen(fingerprint) != 64) return;
  for (size_t i = 0; i < 64; ++i) {
    if (!((fingerprint[i] >= '0' && fingerprint[i] <= '9') ||
          (fingerprint[i] >= 'a' && fingerprint[i] <= 'f'))) return;
  }
  stopDiscovery();
  memcpy(discoveryFingerprint, fingerprint, sizeof(discoveryFingerprint));
  // serviceNetwork publishes only after DHCP and the TCP listener are ready.
}

bool networkReady() {
  return WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0;
}

void serviceNetwork() {
  const uint32_t generation = disconnectGeneration.load();
  if (generation != handledGeneration) {
    stopDiscovery();
    client.stop();
    if (serverRunning) server.end();
    serverRunning = false;
    handledGeneration = generation;
    Serial.printf("[WIFI] disconnected events=%lu reason=%u uptime_ms=%lu\n",
        generation, disconnectReason.load(), millis());
    // Keep the existing retry deadline: repeated failure events must not
    // restart the backoff or cause reconnect calls from the event task.
  }
  if (networkReady()) {
    if (!serverRunning) {
      wifi_ps_type_t powerSave = WIFI_PS_NONE;
      const esp_err_t psResult = esp_wifi_get_ps(&powerSave);
      Serial.printf("[WIFI] power_save requested=NONE actual=%d query_error=%d verified=%u\n",
          static_cast<int>(powerSave), static_cast<int>(psResult),
          psResult == ESP_OK && powerSave == WIFI_PS_NONE);
      server.begin();
      serverRunning = true;
      retryDelay = 1000;
      retryCount = 0;
      Serial.printf("[WIFI] online ip=%s port=%u uptime_ms=%lu\n",
          WiFi.localIP().toString().c_str(), PQC_TCP_PORT, millis());
    }
    // First retry after the next loss starts with a one-second pause.
    startDiscovery();
    retryAt = millis() + 1000;
    return;
  }
  if (serverRunning) {
    stopDiscovery();
    client.stop();
    server.end();
    serverRunning = false;
    retryAt = millis() + 1000;
  }
  if (static_cast<int32_t>(millis() - retryAt) >= 0) {
    ++retryCount;
    Serial.printf("[WIFI] retry=%lu wait_ms=%lu status=%d uptime_ms=%lu\n",
        retryCount, retryDelay, WiFi.status(), millis());
    WiFi.reconnect();
    // Allow ten seconds for association/DHCP, then apply the backoff.
    retryAt = millis() + 10000 + retryDelay;
    retryDelay = retryDelay < 15000 ? retryDelay * 2 : 30000;
  }
}
uint32_t connectionId = 0, commandId = 0, commandStarted = 0;
uint32_t writeMs = 0, maxWriteMs = 0, writeCalls = 0, shortWrites = 0;
size_t writtenBytes = 0;
void commandBegin(const char *command) {
  if (!PQC_USE_WIFI) return;
  lastCommandActivity = millis();
  ++commandId;
  commandStarted = millis();
  writeMs = maxWriteMs = writeCalls = shortWrites = 0;
  writtenBytes = 0;
#if PQC_TRACE_COMMANDS
  Serial.printf("[TCP] conn=%lu cmd=%lu name=%s begin_ms=%lu wifi=%d rssi=%d\n",
      connectionId, commandId, command, commandStarted, WiFi.status(), WiFi.RSSI());
#endif
}
void commandEnd() {
  if (!PQC_USE_WIFI) return;
  lastCommandActivity = millis();
#if PQC_TRACE_COMMANDS
  Serial.printf("[TCP] conn=%lu cmd=%lu end_ms=%lu duration_ms=%lu connected=%u tx_calls=%lu tx_bytes=%u tx_ms=%lu tx_max_ms=%lu tx_short=%lu\n",
      connectionId, commandId, millis(), millis()-commandStarted, connected(),
      writeCalls, static_cast<unsigned int>(writtenBytes), writeMs, maxWriteMs, shortWrites);
#endif
}
size_t write(const uint8_t *data, size_t length) {
  const uint32_t started = millis();
  size_t result = 0;
  if (!PQC_USE_WIFI) {
    result = io().write(data, length);
  } else if (connected()) {
    // One nonblocking attempt. protocol::writeExact owns retry/deadline policy.
    const int sent = send(client.fd(), data, length, MSG_DONTWAIT);
    if (sent > 0) result = static_cast<size_t>(sent);
    else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      const int error = errno;
      Serial.printf("[TCP_SEND] fatal errno=%d requested=%u\n", error, static_cast<unsigned int>(length));
      closeConnection();
    }
  }
  if (PQC_USE_WIFI) {
    const uint32_t elapsed = millis() - started;
    ++writeCalls; writtenBytes += result; writeMs += elapsed;
    if (elapsed > maxWriteMs) maxWriteMs = elapsed;
    if (result < length) ++shortWrites;
  }
  return result;
}
bool begin() {
  Serial.printf("[BUILD] rekey-pipeline-v1 coalesce=%u frame_trace=%u command_trace=%u\n",
      PQC_COALESCE_SMALL_FRAMES, PQC_TRACE_FRAME_TX, PQC_TRACE_COMMANDS);
  if (!PQC_USE_WIFI) return true;
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    // No blocking I/O, TCP manipulation or crypto state changes here.
    disconnectReason.store(info.wifi_sta_disconnected.reason);
    disconnectGeneration.fetch_add(1);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);

  // 關閉 WiFi 省電模式
  if (!WiFi.setSleep(false)) {
    Serial.println("[WIFI] disabling sleep failed");
  }

  WiFi.setAutoReconnect(false); // One retry owner: serviceNetwork().
  networkStarted = true; // Never route protocol bytes back to UART on WiFi loss.
  WiFi.begin(PQC_WIFI_SSID, PQC_WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] initial connection pending; background retries enabled");
  }
  retryAt = millis() + 1000;
  serviceNetwork();
  return true;
}
bool wifiMode() { return PQC_USE_WIFI; }
bool connected() {
  if (PQC_USE_WIFI && clientAccepted &&
      millis() - lastCommandActivity >= kClientIdleTimeoutMs) {
    Serial.printf("[TCP] idle timeout conn=%lu idle_ms=%lu; closing stale client\n",
        connectionId, millis() - lastCommandActivity);
    closeConnection();
    return false;
  }
  return !PQC_USE_WIFI || (disconnectGeneration.load() == handledGeneration &&
      networkReady() && serverRunning && client.connected());
}
void closeConnection() {
  if (PQC_USE_WIFI) {
    clientAccepted = false;
    client.stop();
  }
}
bool acceptConnection() {
  if (!PQC_USE_WIFI) return false;
  serviceNetwork();
  if (!serverRunning || !networkReady() ||
      disconnectGeneration.load() != handledGeneration) { delay(10); return false; }
  client.stop();
  client = server.available();
  if (!client) { delay(10); return false; }
  clientAccepted = true;
  lastCommandActivity = millis();
  client.setNoDelay(true);
  client.setTimeout(10000);
  ++connectionId;
  commandId = 0;
  Serial.printf("[TCP] conn=%lu accepted uptime_ms=%lu peer=%s:%u rssi=%d\n", connectionId,
      millis(), client.remoteIP().toString().c_str(), client.remotePort(), WiFi.RSSI());
  return true;
}
Stream &io() { return networkStarted ? static_cast<Stream &>(client) : static_cast<Stream &>(Serial); }
// Do not use WiFiClient.flush(): semantics differ between core versions.
void flush() { if (!PQC_USE_WIFI) Serial.flush(); }
}
