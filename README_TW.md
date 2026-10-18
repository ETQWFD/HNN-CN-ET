<p align="center">
  <img src="docs/assets/icons/app-icon-192.png" width="120" alt="HNN CN ET" />
</p>

<h1 align="center">HNN CN ET</h1>

<p align="center">
  <b>在安卓手機上本地跑大模型聊天</b><br/>
  不用聯網、不用付費、聊天記錄不上傳 · 32 位舊裝置也能用
</p>

<p align="center">
  <a href="https://github.com/ETQWFD/HNN-CN-ET/releases/latest"><img alt="Release" src="https://img.shields.io/badge/下載-最新APK-00B38A" /></a>
  <a href="https://etqwfd.github.io/HNN-CN-ET/"><img alt="Website" src="https://img.shields.io/badge/官網-etqwfd.github.io/HNN--CN--ET-22D3EE" /></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/License-MIT-green" /></a>
</p>

---

## 這是什麼

HNN CN ET 是基於 [longluo/MnnLlmChat](https://github.com/longluo/MnnLlmChat)（MIT，Copyright © 2025 Frank Luo）和 [alibaba/MNN](https://github.com/alibaba/MNN) 二次開發的安卓端側大模型聊天應用。

- 官網：<https://etqwfd.github.io/HNN-CN-ET/>
- 下載統計：<https://etqwfd.github.io/HNN-CN-ET/dan.html>
- Release：<https://github.com/ETQWFD/HNN-CN-ET/releases>
- 聯絡作者：<mailto:2416444244@qq.com> · QQ 群：917902000

## 功能

- **端側離線聊天**：模型在本地執行，聊天記錄只存在手機裡，不上傳任何伺服器。
- **真實 32 位支援**：APK 同時包含 `armeabi-v7a`（32 位）與 `arm64-v8a`（64 位）原生庫，核心庫（libMNN / libmnnllmapp / libmnn_tts）與語音辨識庫（libsherpa-mnn-jni，ASR）雙架構齊備。
- **應用內自動更新**：啟動時檢查 GitHub Releases，支援斷點續傳、失敗重試、進度與速度顯示。
- **日誌頁**：記錄啟動、下載、載入、推論和錯誤，可一鍵複製，方便排查問題。
- **模型下載器**：內建模型下載，顯示進度/速度，支援斷點續傳與檔案校驗。
- **公告系統**：官方公告發布後，App 啟動時彈窗展示一次。
- **AI 模型審核開關**：預設關閉，直接使用所選模型原生對話；開啟後可使用安全審核（Guard）模型。
- **聯絡作者**：設定頁「聯絡作者」一鍵寄信，也可加入 QQ 群。

## 更新日誌

### v1.0.4（2026-10-18）
- 優化模型初始化與載入流程：進入聊天不再被全螢幕遮罩卡住，底部顯示「正在初始化模型」進度提示，首次載入稍慢屬正常。
- 修復聊天歷史記錄排序混亂的問題。
- 官網全面改版為明亮簡潔風格，網站圖示改用應用圖示。

### v1.0.3（2026-10-18）
- 「API 服務」測試頁改為 HNN CN ET 中文青綠版。
- 聊天新增「深度思考」開關：開 = 先深度分析再回答，關 = 直接原生回答。
- 設定新增「MCP 連線設定」：新增/刪除/測試 MCP 伺服器，AI 可透過 MCP 呼叫工具建立檔案等。
- 設定新增「Agent 工作模式」：對話中可執行裝置操作——開啟應用程式、傳訊息、看影片、聽音樂、建立檔案、返回桌面；缺少無障礙權限時自動引導開啟。
- Agent 執行自動申請儲存與懸浮視窗權限；離開聊天介面後懸浮窗常駐，點擊可回到應用。
- AI 生成的程式碼區塊帶「複製程式碼」按鈕，長按訊息可選擇複製程式碼或複製整則訊息。
- 修復聊天記錄最後一則訊息（純思考輸出/重新載入後）消失的問題。
- AI 對話預設原生、無附加審核（「AI 模型審核」開關預設關閉）。

### v1.0.2（2026-10-18）
- 回饋改為直接寄信給營運團隊（2416444244@qq.com），不再跳轉 GitHub Issue。
- Star 彈窗指向 HNN CN ET 倉庫。
- 補齊標籤翻譯：安全審核 / 語音辨識 / 語音合成 / 英語 / 中英雙語。
- 安全審核模型單獨歸類，不再誤當聊天模型。
- 新增「AI 模型審核」開關（預設關閉）。
- 新增公告系統。
- 生成卡住逾時提示，不再一直轉圈。
- 引擎顯示：HNN 1.0.2 · 基於 MNN 3.6.0。
- 介面統一為青綠色品牌色。

### v1.0.1（2026-10-18）
- 補齊 32 位語音辨識原生庫。

### v1.0.0（2026-10-17）
- 首個 ET 版：補齊 32 位 armeabi-v7a 原生庫；新增應用內自動更新、日誌頁、模型下載斷點續傳；品牌統一為 HNN CN ET。

## 建置

- 環境：Android Studio / JDK 17+ / NDK `27.2.12479018` / Gradle 8.9
- `local.properties` 指向 Android SDK 目錄
- 簽名（release）：環境變數 `KEYSTORE_FILE` / `KEYSTORE_PASSWORD` / `KEY_ALIAS` / `KEY_PASSWORD`
- 建置：`./gradlew :app:assembleStandardRelease`

## 授權

- 上游：longluo/MnnLlmChat，MIT，Copyright © 2025 Frank Luo
- 推論引擎：alibaba/MNN，版權歸原作者
- 本次修改：Copyright © 2026 ET，詳見 [NOTICE](NOTICE) 與 [LICENSE](LICENSE)

English [README.md](README.md) · 简体中文 [README_CN.md](README_CN.md)
