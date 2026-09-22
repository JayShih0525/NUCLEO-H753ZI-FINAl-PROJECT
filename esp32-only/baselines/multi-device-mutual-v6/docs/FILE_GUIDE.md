# 現行檔案閱讀指南

2026-09-16 新增：`host/multi_camera.py` 是 N 台程序啟動／收尾入口；`devices.example.json` 是裝置名稱與公鑰配對範例，`devices.json` 是本機設定；`tests/test_multi_camera.py` 驗證多程序隔離。操作見 [MULTI_CAMERA](../MULTI_CAMERA.md)。主線預設公鑰已改名 `host/camera1.pub`。

路徑均相對 `esp32-only/`。本表是責任與使用關係盤點，不是對演算法或所有錯誤路徑的安全認證。進度與操作見 [README](../README.md)。

## 韌體：firmware/esp32_pqc_demo/

| 檔案 | 實際作用／讀法 |
|---|---|
| esp32_pqc_demo.ino | Arduino 啟動入口，Serial、boot 診斷、transport、camera 初始化，建立 PQC FreeRTOS task |
| crypto_demo.cpp | 命令分派與主要 session 狀態；KEM、GCM、DSA、握手、相機加密、rekey、清理。是後續適合拆分的大檔案，但本次不改功能 |
| crypto_demo.h | 上述初始化與命令迴圈介面 |
| camera_demo.cpp | OV2640 腳位與模式，JPEG frame buffer 取得／歸還；STREAM QVGA、PHOTO SVGA |
| camera_demo.h | 相機模式、frame 資料及呼叫介面 |
| device_identity.cpp | NVS 讀取／建立長期 DSA keypair，自我檢查、指紋與簽章 |
| device_identity.h | 身分模組公開介面 |
| protocol.cpp | 文字行、長度前綴 frame、read/write exact、TX 統計、寫入期限與 boot 診斷 |
| protocol.h | 協定 I/O 與診斷介面 |
| transport.cpp | UART／WiFi 切換；TCP listener/client、WiFi 重試、非阻塞 socket send |
| transport.h | 傳輸介面，讓密碼處理不直接依赖 Serial／WiFiClient |
| frame_writer.h | 合併小型 binary frame 的長度與內容，保持線上格式不變 |
| tx_options.h | TX 實驗開關與 build marker，辨認板上測試版本 |
| wifi_config.example.h | 可提交的網路設定範例 |
| wifi_config.h | 本機真實設定，不提交；不可清掉或把密碼寫入報告 |

啟動次序已包含 RTOS task。相機擷取、加密、傳送仍主要依序進行；「使用 RTOS」不代表已經有並行 pipeline。

## 韌體中的上游 ML-KEM：src/mlkem768/

以下 `.c/.h` 配對的兩個檔案都保留：`.c` 是實作，`.h` 是宣告／參數。這些不是冗餘副本，不因為 Arduino 沒直接 include 每個檔案就刪除。

| 檔案 | 作用 |
|---|---|
| api.h | 對外 ML-KEM-768 API 與尺寸 |
| kem.c、kem.h | KEM keypair、encapsulation、decapsulation |
| indcpa.c、indcpa.h | 底層公鑰加密流程 |
| poly.c、poly.h | 多項式運算、編碼與取樣相關操作 |
| polyvec.c、polyvec.h | 多項式向量操作 |
| ntt.c、ntt.h | Number Theoretic Transform |
| reduce.c、reduce.h | 模數約減 |
| cbd.c、cbd.h | 噪聲分布取樣 |
| symmetric-shake.c、symmetric.h | SHAKE 型對稱原語包裝 |
| fips202.c、fips202.h | Keccak／SHA3／SHAKE |
| randombytes.c、randombytes.h | ESP32 隨機來源介接 |
| verify.c、verify.h | 比較與條件搬移等輔助函式 |
| params.h | ML-KEM-768 固定參數 |
| compat.h | 編譯器／平台相容定義 |
| UPSTREAM.md | PQClean 來源路徑與固定 commit |
| LICENSE | 上游授權，必須保留 |

## 電腦端：host/

| 檔案 | 實際作用 |
|---|---|
| __init__.py | Python package，使 `python -m host...` 可使用一致匯入路徑 |
| pqc_camera_demo.py | 相機入口；初始化、驗證影像、顯示、profile、rekey、收尾及 WiFi supervisor 接合 |
| pqc_wifi_demo.py | TCP 加密文字 echo 入口，驗證雙向加解密，適合在相機之前隔離問題 |
| pqc_host_demo.py | UART 測試入口，亦是共用握手、狀態解析、記憶體與 DSA 驗證函式來源；不可當舊檔刪除 |
| crypto_ops.py | Python AES-GCM、ML-DSA 驗證等密碼包裝 |
| device_auth.py | 載入可信公鑰、驗證新鮮 AUTH_KEM 證明及 session confirmation |
| enroll_device.py | TCP／UART 取得公鑰，與人工核對的 fingerprint 比較後保存；不覆蓋既存檔 |
| serial_protocol.py | 文字控制行與 binary frame parser、讀足長度、trace 和錯誤 |
| serial_connection.py | UART 開啟流程，預先設定 DTR／RTS 降低意外重置 |
| tcp_connection.py | socket 轉成協定需要的 read／readline／write 介面，包裝 TCP 錯誤 |
| camera_replay.py | epoch、frame_id 等順序與重播檢查 |
| camera_recovery.py | UART timeout 後的 token 同步及殘留位元組清理；不是 TCP 重連 |
| wifi_recovery.py | TCP 中斷後重試新連線與新 session，記錄各 attempt、維持顯示流程 |
| run_diagnostics.py | 在交易間隙取得 boot／camera／TX 診斷與記憶體觀察所需資訊 |
| trusted_device.pub | 第一台本機可信公鑰；資料，不是程式，保留且不提交 |
| camera2.pub | 第二台本機可信公鑰；透過 `--trust-key` 選擇，保留且不提交 |

## 回歸測試：tests/

| 檔案 | 保護的行為 |
|---|---|
| test_host_crypto.py | 本機密碼封裝的基本正負向行為 |
| test_device_auth.py | 挑戰／身分／session 證明的驗證 |
| test_trust_selection.py | 指定公鑰、拒絕錯誤／缺失公鑰、重連保留同一信任 |
| test_camera_replay.py | 相機順序、epoch 與重播拒絕 |
| test_camera_recovery.py | UART 同步與恢復輔助流程 |
| test_camera_recovery_flow.py | 相機主流程的恢復與收尾 |
| test_serial_connection.py | UART 開啟與控制線設定 |
| test_serial_diagnostics.py | 分段讀取／逾時等協定診斷 |
| test_run_diagnostics.py | 診斷快照與輸出 |
| test_wifi_message.py | WiFi encrypted echo 的協定行為 |
| test_wifi_recovery.py | 重連、期限、不可重試錯誤與中止流程 |
| wifi-reconnect-native/test.cpp | 用假時鐘與假 WiFi 執行實際 transport.cpp，測重連／傳送路徑 |
| wifi-reconnect-native/frame_writer_test.cpp | 合併前後 binary frame 位元組與短寫入處理 |
| wifi-reconnect-native/Arduino.h | Arduino 測試替身 |
| wifi-reconnect-native/WiFi.h | WiFi／client／server 測試替身 |
| wifi-reconnect-native/lwip/sockets.h | 非阻塞 socket 測試替身 |
| text-tx-native/test.cpp | 實際 protocol.cpp 的文字輸出、統計與寫入失敗路徑 |
| text-tx-native/Arduino.h | 此 harness 的 Arduino／Stream 替身 |
| text-tx-native/esp_system.h | boot/reset 測試替身 |
| text-tx-native/esp_timer.h | 計時測試替身 |

兩套 `Arduino.h` 對應不同 harness；不是可以合併刪除的舊版標頭。

## 效能工具：performance-tests/

| 檔案／目錄 | 作用 |
|---|---|
| run_benchmark.py | 啟動共用相機 Host；保存命令、Git 狀態、terminal、trace、各重連 segment 摘要 |
| analyze_trace.py | JSONL 分析，平均／P95／區間 FPS 等統計 |
| test_analyze.py | 分析器回歸測試 |
| README.md | 量測操作與比較方式 |
| TX_EXPERIMENT.md | v1 小 frame 合併實驗背景；保留比較依據 |
| TEXT_TX_CHECK.md | v2 文字 TX 納入統計與期限的歷史 |
| TX_DEADLINE.md | 現行 v3 非阻塞 send 與 exact-write 期限說明 |
| reports/BASELINE.md | 初期量測基準解讀，不代表最新版表現 |
| reports/baseline_20260913.json | 上述基準數據 |
| results/ | 各輪本機結果、Monitor 與 pcap；忽略提交，保留證據 |

manifest 記錄 commit 與 dirty 檔案清單，但沒有保存所有 dirty 原始碼內容。因此相同 commit 不一定代表兩輪是相同程式。

## 隔離相機與 DSA 的基準工具

| 路徑 | 作用 |
|---|---|
| camera-tests/photo_camera_test/photo_camera_test.ino | 不含 PQC 的單張相機 sketch |
| camera-tests/photo_camera_test/camera_board.h | 該 sketch 的板型腳位 |
| camera-tests/stream_camera_test/stream_camera_test.ino | 不含 PQC 的連續相機 sketch |
| camera-tests/stream_camera_test/camera_board.h | 該 sketch 的板型腳位；獨立 Arduino sketch 需要自己的檔案 |
| camera-tests/host/camera_serial.py | 獨立相機 UART 協定 |
| camera-tests/host/capture_photo.py | 接收並保存基準照片 |
| camera-tests/host/record_stream.py | 舊基準錄影程式；只有刻意執行它才錄影，非現行整合入口 |
| camera-tests/README.md | 獨立基準操作，參數不可套用整合版 |
| camera-tests/requirements-camera.txt | 基準 Host 依賴 |
| dsa-validation/verify_dsa.py | local／UART device 的 20 項 DSA 正負向驗證，保存 TXT／JSON |
| dsa-validation/README.md | 各項測試預期及限制 |
| dsa-validation/requirements.txt | DSA 工具依賴 |
| dsa-validation/results/ | 實機與本機歷史驗證結果，忽略提交 |
| dsa-validation/.test-deps/ | 本機測試依賴，非待燒錄／部署檔案 |

## 根目錄文件與本機資料

| 檔案／目錄 | 作用 |
|---|---|
| README.md | 現行總入口、歷程、操作與下一步 |
| AUTHENTICATION.md | 持久身分與握手設計；選擇多裝置公鑰以 MULTI_DEVICE_TRUST 為準 |
| MULTI_DEVICE_TRUST.md | 每程序指定並固定可信公鑰 |
| REPLAY_PROTECTION.md | 相機序號與重播檢查 |
| CAMERA_RECOVERY.md | UART 收包中斷的恢復設計 |
| SERIAL_DIAGNOSTICS.md | UART 診斷、boot 與 TX 觀察 |
| requirements.txt | 整合 Python 依賴與版本 |
| .gitignore | 子目錄快取排除；主要本機資料／秘密排除在專案最外層 .gitignore |
| diagnostics/ | 本機 trace／log，不當成程式或測試案例刪除 |
| docs/REVIEW_STATUS.md | 本次檢視範圍、問題與驗證結果 |
| archive/ | 過版文件與搬移對照 |
| _local_archive/ | 本機封存，不提交 Git |

若要個人重構，先沿同一個取圖命令讀懂兩端，再把 `crypto_demo.cpp` 的「協定命令、session 狀態、密碼操作」逐步拆分。保持 wire format 與回歸測試不變，避免同時重構與改協定而失去比較基準。
