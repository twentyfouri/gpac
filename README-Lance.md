
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

#### 1. 生成 30 秒測試 MP4
```bash
cd /Users/lance/work/Github/gpac
DYLD_LIBRARY_PATH=./bin/gcc ./bin/gcc/mp4muxdemux make30
```

輸出檔案：`applications/testapps/mp4muxdemux/out_30s.mp4` (5.7 MB)

#### 2. 解封裝 MP4
```bash
DYLD_LIBRARY_PATH=./bin/gcc ./bin/gcc/mp4muxdemux demux \
applications/testapps/mp4muxdemux/out_30s.mp4 \
/tmp/out.h264 \
/tmp/out.aac
```

輸出：
- `/tmp/out.h264` - H.264 視頻流
- `/tmp/out.aac` - AAC 音頻流

#### 3. 封裝 MP4
```bash
DYLD_LIBRARY_PATH=./bin/gcc ./bin/gcc/mp4muxdemux mux \
/tmp/out.h264 \
/tmp/out.aac \
/tmp/remux.mp4 \
30
```

輸出：`/tmp/remux.mp4` - 重新封裝的 MP4（30 fps）

### 使用自訂素材
```bash
# 用 h264 目錄的逐幀檔案 + aac 檔案
make -C applications/testapps/mp4muxdemux

DYLD_LIBRARY_PATH=./bin/gcc ./bin/gcc/mp4muxdemux make30
``` 


# Getting started
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


# Roadmap
Users are encouraged to use the latest tag or the master branch.

## V2.X
Targets:
- [ ] DASH event support
- [ ] Web GUI
- [ ] QUIC support
- [ ] ROUTE file repair support
- [ ] FLUTE file repair support

