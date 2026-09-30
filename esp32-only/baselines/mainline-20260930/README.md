# 2026-09-30 主線測試基準

這是長期使用功能修改前的主線來源快照，包含 TCP 接收緩衝與 TraceWriter。舊 baseline 保持不變。本版保留有限秒數與完整診斷，不是 9/22 的無參數使用版。

在本資料夾執行，使用 project/.venv 的 Python（相對路徑 ../../../.venv/Scripts/python.exe）：

```powershell
../../../.venv/Scripts/python.exe -B -u -m host.multi_camera --discover --seconds 600 --resolution qvga --rekey-every 10 --rekey-mode pipeline --display
```

host/multi_camera.py 的 ROOT 指向此副本，公鑰、devices.json、Host identity 與韌體均是獨立副本。本機秘密／配置沿用相同身分，已被 Git 忽略；不得公開分享私鑰。其他電腦需自行安全佈建，不保證 clone 後直接連現有板子。

診斷預設寫至此副本 diagnostics（Git 忽略），不在快照驗證範圍。正常開發不要修改來源；只允許本機配置與產生紀錄。若要升級另建版本。

validation-summary.json 保存既有 9/30 測試結果，其原始 command 路徑指向當時主線，不代表在副本重新進行硬體測試。兩台 600 秒平均 36.31 / 40.50 FPS，0 中斷，有最長約 2.5 秒停頓。完整範圍與限制見 docs/CURRENT_STATUS.md。

verify_snapshot.ps1 檢查列入的檔案；不包含秘密、devices.json、.pub、產出資料或外部 Python / Arduino 安裝。未鎖定工具鏈，不宣稱可重現二進位。MAINLINE_README.md 是來源說明存檔，其中 baseline / archive 連結依原主線位置解讀。
