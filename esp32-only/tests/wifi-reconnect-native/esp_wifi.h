#pragma once
using esp_err_t = int;
enum wifi_ps_type_t { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1 };
constexpr esp_err_t ESP_OK = 0;
inline wifi_ps_type_t testPowerSave = WIFI_PS_MIN_MODEM;
inline unsigned int powerSaveQueries = 0;
inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t *value) {
  ++powerSaveQueries;
  *value = testPowerSave;
  return ESP_OK;
}
