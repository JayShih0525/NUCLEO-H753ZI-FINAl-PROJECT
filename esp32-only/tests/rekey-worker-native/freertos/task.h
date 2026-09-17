#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
using TaskHandle_t = void *;
inline std::mutex notificationMutex;
inline std::condition_variable notification;
inline bool notified = false, failTask = false;
inline int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *, int, TaskHandle_t *handle) {
  if (failTask) return 0;
  *handle = reinterpret_cast<void *>(1);
  std::thread(fn, nullptr).detach(); return 1;
}
inline void xTaskNotifyGive(TaskHandle_t) {
  std::lock_guard<std::mutex> lock(notificationMutex);
  notified = true; notification.notify_one();
}
inline void ulTaskNotifyTake(int, unsigned) {
  std::unique_lock<std::mutex> lock(notificationMutex);
  notification.wait(lock, [] { return notified; }); notified = false;
}
inline void vTaskDelay(unsigned) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 8192; }
