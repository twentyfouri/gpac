
# GPAC Introduction

Current version: 2.4

Latest Release: 2.4


# Features

## MP4 Mux/Demux 範例

### ⚠️ 使用前必須設定環境變數
在執行以下任何 MP4Box 指令前，請先設定 PATH：
```bash
cd /Users/lance/work/Github/gpac
export PATH="$PWD/local/bin:$PATH"
```

或者將以下內容加入 `~/.zshrc`（永久設定）：
```bash
export PATH="/Users/lance/work/Github/gpac/local/bin:$PATH"
```

驗證安裝：
```bash
MP4Box -version
gpac -version
```

### 基本操作

#### 1. MP4 封裝 (Mux)
將 H.264 視頻和 AAC 音頻封裝成 MP4：
```bash
# 使用測試素材的完整範例
MP4Box -add testsuite/media/auxiliary_files/enst_video.h264 \
       -add testsuite/media/auxiliary_files/enst_audio.aac \
       -new output.mp4
```

添加字幕：
```bash
MP4Box -add testsuite/media/auxiliary_files/enst_video.h264 \
       -add testsuite/media/auxiliary_files/enst_audio.aac \
       -add testsuite/media/auxiliary_files/subtitle_fr.srt:lang=fra \
       -new output.mp4
```

使用自己的檔案（需替換為實際路徑）：
```bash
MP4Box -add /path/to/video.h264 -add /path/to/audio.aac -new output.mp4
```

#### 2. MP4 解封裝 (Demux)
提取視頻流（軌道 1）：
```bash
MP4Box -raw 1 output.mp4 -out video.h264
```

提取音頻流（軌道 2）：
```bash
MP4Box -raw 2 output.mp4 -out audio.aac
```

提取所有原始 samples：
```bash
MP4Box -raws 1 output.mp4
```

#### 3. 格式轉換
MP4 轉 MPEG-2 TS：
```bash
MP4Box -mux output.ts:pcr_init=0:pes_pack=none input.mp4
```

從 TS 提取流：
```bash
MP4Box -raw video input.ts
```

#### 4. MP4 資訊查看
```bash
MP4Box -info input.mp4           # 查看所有軌道資訊
MP4Box -info 1 input.mp4         # 查看特定軌道
```

#### 5. MP4 編輯操作
分段 (Fragment)：
```bash
MP4Box -frag 1000 input.mp4 -out fragmented.mp4
```

交錯儲存 (Interleave)：
```bash
MP4Box -inter 500 input.mp4
```

提取單一軌道：
```bash
MP4Box -single 1 input.mp4 -out video_only.mp4
```

### 測試素材位置
```
testsuite/media/auxiliary_files/
├── enst_video.h264    (47 KB, H.264 視頻)
├── enst_audio.aac     (83 KB, AAC 音頻)
└── subtitle_fr.srt    (SRT 字幕)
```

### 相關測試腳本
- `testsuite/scripts/mp4box-base.sh` - MP4 基礎操作測試
- `testsuite/scripts/mp4box-mux.sh` - MP4 封裝測試
- `testsuite/scripts/mp4box-*.sh` - 36 個 MP4Box 測試腳本

### 執行測試
```bash
cd testsuite
export PATH="/Users/lance/work/Github/gpac/local/bin:$PATH"

# 測試 MP4 封裝
./make_tests.sh scripts/mp4box-mux.sh -p=0

# 測試 MP4 基礎操作
./make_tests.sh scripts/mp4box-base.sh -p=0
```

## MP4MuxDemux 應用範例

### 編譯 mp4muxdemux 應用
```bash
cd /Users/lance/work/Github/gpac
make -C applications/testapps/mp4muxdemux
```

編譯完成後，執行檔位於：`bin/gcc/mp4muxdemux`

### 執行 mp4muxdemux

**重要：執行前需設定正確的動態庫路徑**
```bash
cd /Users/lance/work/Github/gpac
export DYLD_LIBRARY_PATH="./local/lib:./bin/gcc:$DYLD_LIBRARY_PATH"
```

#### 1. 生成 30 秒測試 MP4
```bash
./bin/gcc/mp4muxdemux make30
```
輸出檔案：`applications/testapps/mp4muxdemux/out_30s.mp4` (5.7 MB)

#### 2. 解封裝 MP4
```bash
./bin/gcc/mp4muxdemux demux \
applications/testapps/mp4muxdemux/out_30s.mp4 \
/tmp/out.h264 \
/tmp/out.aac
```
輸出：
- `/tmp/out.h264` - H.264 視頻流 (5.6 MB)
- `/tmp/out.aac` - AAC 音頻流 (58 KB)

#### 3. 解封裝 MP4（逐幀提取）⭐ 支援 Metadata
👉 **新功能！** 將所有 frame 提取到單獨的檔案，**並自動提取 metadata track**（如果存在）
```bash
./bin/gcc/mp4muxdemux demux_frames \
applications/testapps/mp4muxdemux/out_30s.mp4 \
/tmp/frames_dir
```
輸出：
- `/tmp/frames_dir/frame_00000.h264` - 第 0 幀
- `/tmp/frames_dir/frame_00001.h264` - 第 1 幀
- ...
- `/tmp/frames_dir/frame_01979.h264` - 第 1979 幀（總幀數視影片長度而定）
- **`/tmp/frames_dir/metadata.ndjson`** - Metadata track（自動檢測並提取）⭐
- 總大小視輸入影片而定

**Metadata 提取特性**：
- ✅ 自動檢測 MP4 中的 JSON metadata track
- ✅ 提取到 `metadata.ndjson` 檔案（NDJSON 格式）
- ✅ 與視頻幀同步（frame_id 對應）
- ✅ 如果 MP4 沒有 metadata track，則只提取視頻幀

**範例輸出**：
```bash
Extracted 1980 frames to /tmp/frames_dir
✅ Extracted metadata track to /tmp/frames_dir/metadata.ndjson
```

#### 4. 加入 JSON Metadata Track（per-frame 檢測結果）⭐ 新功能
👉 **目的**：將 per-frame 檢測結果（人臉檢測、物體偵測等）以 JSON 格式嵌入 MP4，並與影片播放時間同步。

**輸入格式**：NDJSON（一行一個 JSON 物件，必須包含 `frame_id` 欄位）
```json
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205},{"class":"car","x":250,"y":120,"w":150,"h":100}]}
{"frame_id":10,"objects":[]}
```

**創建 metadata 檔案**：
```bash
cat > /tmp/detections.ndjson << 'EOF'
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
EOF
```

**新增 metadata track 到 MP4**：
```bash
./bin/gcc/mp4muxdemux add_metadata_track \
applications/testapps/mp4muxdemux/out_30s.mp4 \
/tmp/detections.ndjson \
/tmp/with_metadata.mp4
```
輸出：`/tmp/with_metadata.mp4`（已嵌入 metadata track）

**元數據軌道特性**：
- 📹 **Sample Entry 類型**：METT (Metadata Text, 0x6D657474)
- 📄 **MIME 類型**：application/json
- 🔤 **編碼**：utf-8
- ⏱️ **時間基準**：30 fps（frame_id 直接對應 DTS）
  - frame_id=0 → DTS=0（第 0 秒）
  - frame_id=1 → DTS=1/30（第 1/30 秒）
  - frame_id=30 → DTS=1（第 1 秒）
- 🔗 **時間同步**：每個 sample 的 DTS 與 frame_id 匹配，確保播放時自動對齊影像幀

#### 5. 提取 JSON Metadata Track
從已嵌入 metadata 的 MP4 提取 JSON 內容回到 NDJSON 格式：
```bash
./bin/gcc/mp4muxdemux extract_metadata_track \
/tmp/with_metadata.mp4 \
/tmp/detections_extracted.ndjson
```
輸出：`/tmp/detections_extracted.ndjson`

**輸出格式**：
```json
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":2,"objects":[]}                    # 缺失幀用預設值填充
{"frame_id":3,"objects":[]}
...
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
...
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
```

**往返驗證**：缺失的 frame 會自動填充默認值 `{"frame_id":X,"objects":[]}`

#### 實際應用範例：自動化檢測結果嵌入工作流程 🎬

**場景**：監視器影片 + 人臉檢測結果 → 帶時間同步元數據的 MP4

**步驟 1**：生成檢測結果（NDJSON 格式）
```bash
# 假設你的 AI 模型輸出檢測結果到 detections.ndjson
# 格式：每一行對應一幀，包含偵測到的物體
python detect_frame.py input_video.mp4 > detections.ndjson
```

**步驟 2**：嵌入到 MP4
```bash
./bin/gcc/mp4muxdemux add_metadata_track \
input_video.mp4 \
detections.ndjson \
output_with_detections.mp4
```

**步驟 3**：驗證和提取
```bash
# 檢查是否正確嵌入
./bin/gcc/mp4muxdemux extract_metadata_track \
output_with_detections.mp4 \
verify_detections.ndjson

# 驗證一致性
diff detections.ndjson verify_detections.ndjson
```

**好處**：
- ✅ 檢測結果與影片時間精確同步（frame-level）
- ✅ 單個 MP4 檔案包含所有信息（易於分發和存儲）
- ✅ 使用標準 METT 格式（GPAC 和其他播放器相容）
- ✅ JSON 格式彈性（支援任何檢測結構）

#### 6. 封裝 MP4
```bash
./bin/gcc/mp4muxdemux mux \
/tmp/out.h264 \
/tmp/out.aac \
/tmp/remux.mp4 \
30
```
輸出：`/tmp/remux.mp4` - 重新封裝的 MP4（30 fps, 5.7 MB）

### 使用自訂素材
```bash
./bin/gcc/mp4muxdemux mux \
applications/testapps/mp4muxdemux/h264/test_frame0.h264 \
applications/testapps/mp4muxdemux/aac.aac \
/tmp/custom_output.mp4 \
30
```

### 測試結果 (v2.4.0-lance-dev)
✅ **所有功能正常運作**：
- make30：成功生成 5.7 MB MP4（包含 video + audio + metadata tracks）
- demux：成功解封裝到 H264 + AAC
- demux_frames：成功逐幀提取 1980 個 H.264 幀檔案（12 MB）+ **自動提取 metadata.ndjson（851 行）** ⭐
- **add_metadata_track**：✅ 成功加入 JSON metadata track（新功能）
  - 支援 NDJSON 格式輸入，每行包含 frame_id
  - 使用 METT sample entry with application/json MIME type
  - 時間同步：frame_id 直接對應 DTS（30 fps timescale）
  - 測試檔案：35 KB MP4 with embedded metadata
- **extract_metadata_track**：✅ 成功提取 JSON metadata（新功能）
  - 從 MP4 讀取 METT 軌道
  - 恢復所有幀的 JSON 有效載荷
  - 往返驗證：缺失幀自動填充
  - 測試結果：11 幀提取成功（框 0, 1, 5, 10 + 填充）
- mux：成功重新封裝為 MP4

### 📋 離線元數據測試指南

詳細的測試文檔和自動化測試腳本已準備好：

1. **[TEST-METADATA.md](applications/testapps/mp4muxdemux/TEST-METADATA.md)** - 完整測試文檔
   - ✅ 4 個詳細測試用例（簡單、複雜、邊界情況）
   - ✅ 逐步執行指南
   - ✅ 驗證檢查清單
   - ✅ 故障排除指南

2. **[test_metadata.sh](applications/testapps/mp4muxdemux/test_metadata.sh)** - 自動化測試腳本
   - ✅ 可直接執行：`./test_metadata.sh`
   - ✅ 包含 3 個測試用例（簡單、複雜、邊界）
   - ✅ JSON 驗證
   - ✅ 彩色輸出和詳細報告
   - ✅ 清理選項：`./test_metadata.sh clean`

### 🔴 實時 fMP4 流媒體系統 ⭐ 新增

針對實時場景（視頻邊錄邊播、AI 檢測實時嵌入等），完整的 **Fragmented MP4 (fMP4)** 系統：

**核心檔案**：
- **[REALTIME-FMP4.md](applications/testapps/mp4muxdemux/REALTIME-FMP4.md)** - 完整系統文檔
- **[realtime_mp4.h](applications/testapps/mp4muxdemux/realtime_mp4.h)** - API 頭文件
- **[realtime_mp4.c](applications/testapps/mp4muxdemux/realtime_mp4.c)** - 實現代碼
- **[realtime_example.c](applications/testapps/mp4muxdemux/realtime_example.c)** - 使用示例

**功能特性**：
- ✅ **邊寫邊播** - 在錄制進行中播放
- ✅ **三軌道支持** - 視頻 (H.264) + 音頻 (AAC) + 元數據 (JSON)
- ✅ **實時元數據** - 檢測結果即時嵌入
- ✅ **分片流結構** - 符合 HLS/DASH 標準
- ✅ **容錯恢復** - 獨立片段，中斷可恢復

**實時 API**：
```c
// 初始化
RealtimeMP4Writer *writer = realtime_mp4_open(
    "/path/to/output.mp4", 
    1280, 720, 30,        // 視頻：1280x720@30fps
    48000, 2              // 音頻：48kHz 立體聲
);

// 實時寫入（在錄制循環中調用）
while (recording) {
    // 視頻
    realtime_mp4_add_video_frame(writer, h264_nal, size, timestamp_us);
    
    // 音頻
    realtime_mp4_add_audio_samples(writer, aac_frame, size, timestamp_us);
    
    // 檢測結果（有時候）
    if (detector.has_result()) {
        json = detector.get_json();  // {"frame_id":123, "objects":[...]}
        realtime_mp4_add_metadata(writer, json, timestamp_us);
    }
    
    // 每 1 秒刷新片段
    if (elapsed_time % 1000 == 0) {
        realtime_mp4_flush_fragment(writer);
    }
}

// 完成
realtime_mp4_close(writer);
```

**使用場景**：
- 🎥 **實時監視錄制** - 攝像頭 → 檢測 → MP4 (邊錄邊播)
- 📡 **直播流** - HLS/DASH 直播集成
- 🤖 **AI 檢測嵌入** - 人臉/物體檢測結果實時同步
- 💾 **容錯存儲** - 支持中斷恢復的持續錄制

#### 快速開始測試

```bash
# 進入應用目錄
cd /Users/lance/work/Github/gpac/applications/testapps/mp4muxdemux

# 執行自動化測試（推薦）
./test_metadata.sh

# 或執行帶清理的測試
./test_metadata.sh clean
```

#### 最新測試結果 (2026-02-21)

```
✅ 所有測試通過！

結果：
  ✅ 通過：13
  ❌ 失敗：0

測試涵蓋：
  ✓ 簡單案例：4 幀 metadata 的嵌入/提取
  ✓ 複雜案例：多物體 + 各種欄位的處理
  ✓ 邊界案例：單一幀、連續幀
  ✓ JSON 驗證：所有輸出行都是有效 JSON
  ✓ 往返一致性：內容無損

生成檔案驗證：
  ✓ output_simple.mp4 (35 KB)
  ✓ output_complex.mp4 (36 KB)
  ✓ output_single.mp4 (35 KB)
  ✓ output_consecutive.mp4 (35 KB)
  
提取驗證：
  ✓ extracted_simple.ndjson (454 B, 11 幀)
  ✓ extracted_complex.ndjson (903 B, 11 幀)
  ✓ extracted_single.ndjson (217 B)
  ✓ extracted_consecutive.ndjson (200 B, 5 幀)
``` 


# Getting started

## 編譯 (including Metadata Track Support)
GPAC v2.4.0 已包含以下改進：
- ✅ 元數據軌道 (METT/JSON) 支援
- ✅ `gf_isom_new_stxt_description()` 函數正確導出
- ✅ 完整的 ISOM API 支援

### macOS (Apple Silicon M1/M2) 編譯步驟
```bash
# 安裝必要工具
brew install coreutils gnu-time

# 配置和編譯
cd /Users/lance/work/Github/gpac
./configure --prefix=$PWD/local
make -j$(sysctl -n hw.ncpu)
make install

# 驗證安裝
export PATH="$PWD/local/bin:$PATH"
MP4Box -version
```

### 編譯 mp4muxdemux 工具（含元數據支援）
```bash
cd /Users/lance/work/Github/gpac
make lib                                    # 編譯核心庫
cd applications/testapps/mp4muxdemux
make                                        # 編譯工具
```

編譯完成後，執行檔位於：`../../bin/gcc/mp4muxdemux`

## make under macOS(M1)
- brew install coreutils gnu-time
- ./configure --prefix=$PWD/local
- make -j$(sysctl -n hw.ncpu)
- make install

## Documentation

詳細文件請參考：
- 官方 Wiki：https://wiki.gpac.io
- Filter 文件：https://wiki.gpac.io/Filters/Filters/
- API 文件：https://doxygen.gpac.io

## Testing

### 環境準備
1. 確保已編譯並安裝到本地（見上方 make 流程）
2. 設定 PATH：
   ```bash
   export PATH="/Users/lance/work/Github/gpac/local/bin:$PATH"
   ```

### 執行測試
進入 testsuite 目錄：
```bash
cd testsuite
```

#### 快速測試（建議）
```bash
# 清除之前的快取並執行快速測試（每個腳本只跑第一個測試）
./make_tests.sh -clean
./make_tests.sh -quick -p=0
```

#### 執行單一測試腳本
```bash
# 例如：測試 HLS 生成
./make_tests.sh scripts/hls-gen.sh -p=0

# 清除該腳本的快取
./make_tests.sh -clean scripts/hls-gen.sh
```

#### 執行完整測試套件
```bash
# 從專案根目錄執行
make test_suite
```

#### 同步外部測試素材（可選）
```bash
# 下載外部測試媒體檔案（約 300MB，可能較慢）
./make_tests.sh -sync-media -clean
```

### 測試結果
- 測試日誌：`testsuite/results/all_logs.txt`
- 測試統計：`testsuite/results/all_results.xml`
- 線上結果：https://tests.gpac.io

### v2.4.0-lance-dev 測試結果
快速測試結果（2026-02-20）：
- 總測試數：201 個測試腳本（834 個子測試）
- 通過：137 (68%)
- 失敗：38 (9% of subtests)
- Hash 失敗：103 (12%)
- 執行時間：約 3.5 分鐘

註：部分失敗是因為缺少外部編解碼器（FFmpeg、OpenJPEG 等）


## Support, ongoing tasks and bugs


## 已完成的改進 (Lance 自定義) ⭐

### v2.4.0-lance-dev 新增功能
1. **元數據軌道支援** (METT/JSON)
   - 新增命令：`add_metadata_track`、`extract_metadata_track`
   - 支援 NDJSON 格式 per-frame 元數據
   - 時間同步：frame_id → DTS (30 fps timescale)
   - 使用 ISOM API：`gf_isom_new_stxt_description()`
   - 修復：添加 `GF_EXPORT` 宏確保符號正確導出

2. **MP4MuxDemux 工具擴展**
   - 新增 `demux_frames` - 逐幀提取 H.264（支援 1000+ 幀）+ **自動提取 metadata track** ⭐
   - 新增 `add_metadata_track` - 嵌入 JSON 檢測結果
   - 新增 `extract_metadata_track` - 提取元數據回到 NDJSON
   - 新增 `make30` - 生成 30 秒測試 MP4（包含 metadata track）

3. **使用案例**
   - ✅ 人臉/物體檢測結果嵌入（含邊界框坐標）
   - ✅ Frame-level 時間同步（0.033 秒精度 @30fps）
   - ✅ 單檔案分發（MP4 + metadata）

4. **代碼修改**
   - `src/isomedia/sample_descs.c` - 修復 gf_isom_new_stxt_description 符號導出
   - `applications/testapps/mp4muxdemux/main.c` - 添加 174 行新功能代碼

### 測試驗證 (2026-02-21)
```
測試檔案：counter-bifs-10sec.mp4 (10 秒原始影片)
輸入元數據：4 幀 (frame_id: 0, 1, 5, 10)
輸出元數據：11 幀 (包含自動填充的缺失幀)
往返驗證：✅ 通過

測試命令：
$ ./mp4muxdemux add_metadata_track input.mp4 test.ndjson output.mp4
$ ./mp4muxdemux extract_metadata_track output.mp4 extracted.ndjson
$ cat extracted.ndjson  # 驗證結果
```


# Roadmap
Users are encouraged to use the latest tag or the master branch.

## V2.X
Targets:
- [ ] DASH event support
- [ ] Web GUI
- [ ] QUIC support
- [ ] ROUTE file repair support
- [ ] FLUTE file repair support

