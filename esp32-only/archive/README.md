# 歷史封存與整理對照

2026-09-15 整理。這裡保存歷史，不作為現行規格。現行入口見 [主 README](../README.md)。歷史文件內原本相對路徑保留原樣，搬移後可能無法點擊；操作指令也可能過版。

| 原位置（相對 esp32-only） | 新位置 | 原因 |
|---|---|---|
| README.md | history/README_before_cleanup.md | 舊 UART／錄影說明不能描述目前 WiFi 主線 |
| PROGRESS_AFTER_DSA.md | history/PROGRESS_AFTER_DSA.md | 保留 DSA 後演進紀錄，部分恢復／信任行為已變更 |
| VERSION_PROGRESS_20260911.md | history/VERSION_PROGRESS_20260911.md | 版本間歷史比較 |
| WIFI_CAMERA.md | history/WIFI_CAMERA.md | 早期 WiFi 接影像的操作快照 |
| WIFI_MESSAGES.md | history/WIFI_MESSAGES.md | 早期 encrypted echo 快照 |
| dsa-validation/AUTH_IMPLEMENTATION_STATUS.md | history/AUTH_IMPLEMENTATION_STATUS.md | 首版認證實作狀態，不代表現行完整進度 |
| wifi/ | ../_local_archive/cleanup_20260915/wifi/ | 舊 WiFi-only sketch，整合主線已由 transport.cpp 提供；可能含本機網路設定 |
| encrypted_photo.jpg | ../_local_archive/cleanup_20260915/encrypted_photo.jpg | 產生的照片 |
| camera-tests/host/captured_photo.jpg | ../_local_archive/cleanup_20260915/captured_photo.jpg | 相機基準產物 |
| note.txt | ../_local_archive/cleanup_20260915/note.txt | 個人筆記原樣保存，不作為維護中的文件 |

以上都是搬移，没有直接刪除。`_local_archive` 已由最外層 `.gitignore` 排除，所以它的內容只存在本機；需要另一台電腦保留時，請另外備份，不能依賴 Git clone。

未搬動 `diagnostics/`、`performance-tests/results/`、`dsa-validation/results/`：它們是測試證據，且部分 manifest／重連紀錄引用絕對路徑。未搬動兩把 `.pub`、真實 `wifi_config.h`、外部依賴或 `.venv`。

`performance-tests/TX_EXPERIMENT.md` 與 `TEXT_TX_CHECK.md` 保留在實驗資料夾，描述 v1／v2 的比較理由；目前送出策略以 `TX_DEADLINE.md` 與原始碼為準。
