# MP4 Metadata Track 完整測試指南

本文档为 `mp4muxdemux` 工具中的元数据轨道功能 (`add_metadata_track` 和 `extract_metadata_track`) 提供完整的测试指南。

## 前置準備

### 1. 確認工具已編譯
```bash
cd /Users/lance/work/Github/gpac
# 檢查可執行檔
ls -lh bin/gcc/mp4muxdemux
```

### 2. 設置環境變數
```bash
# 方式 1：臨時設置
export MP4MUX="/Users/lance/work/Github/gpac/bin/gcc/mp4muxdemux"

# 方式 2：永久設置（加入 ~/.zshrc）
echo 'export MP4MUX="/Users/lance/work/Github/gpac/bin/gcc/mp4muxdemux"' >> ~/.zshrc
source ~/.zshrc
```

### 3. 準備測試目錄
```bash
mkdir -p /tmp/metadata_test
cd /tmp/metadata_test
```

---

## 測試用例 1：簡單測試（4 幀）

### 1A. 建立測試 Metadata 檔案

```bash
cat > simple_metadata.ndjson << 'EOF'
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
EOF

echo "✓ 建立簡單測試檔案"
cat simple_metadata.ndjson
```

**預期輸出**：
```json
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
```

### 1B. 執行 add_metadata_track

```bash
TEST_MP4="/Users/lance/work/Github/gpac/testsuite/external_media/noFragsDefault/counter-bifs-10sec-160x102-bipbop_420_avc.mp4"

echo "=== 測試 add_metadata_track (簡單案例) ==="
$MP4MUX add_metadata_track "$TEST_MP4" simple_metadata.ndjson output_simple.mp4

# 驗證輸出
if [ -f output_simple.mp4 ]; then
    echo "✅ 成功：MP4 檔案已生成"
    ls -lh output_simple.mp4
else
    echo "❌ 失敗：檔案未生成"
    exit 1
fi
```

**預期結果**：
```
✅ 成功：MP4 檔案已生成
-rw-r--r--  1 lance  wheel  35K Feb 21 output_simple.mp4
```

### 1C. 執行 extract_metadata_track

```bash
echo ""
echo "=== 測試 extract_metadata_track (簡單案例) ==="
$MP4MUX extract_metadata_track output_simple.mp4 extracted_simple.ndjson

if [ -f extracted_simple.ndjson ]; then
    echo "✅ 成功：NDJSON 檔案已提取"
else
    echo "❌ 失敗：提取檔案不存在"
    exit 1
fi
```

### 1D. 驗證提取內容

```bash
echo ""
echo "=== 驗證提取內容 ==="
echo "提取的完整內容："
cat extracted_simple.ndjson

echo ""
echo "統計："
echo "  原始幀數：$(wc -l < simple_metadata.ndjson)"
echo "  提取幀數：$(wc -l < extracted_simple.ndjson)"
```

**預期輸出**：
```
0 9 10 (frame_id)
11 총 frames (including filled frames 2, 3, 4, 6, 7, 8, 9)

{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":2,"objects":[]}
{"frame_id":3,"objects":[]}
{"frame_id":4,"objects":[]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
{"frame_id":6,"objects":[]}
{"frame_id":7,"objects":[]}
{"frame_id":8,"objects":[]}
{"frame_id":9,"objects":[]}
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
```

### 1E. 驗證往返一致性

```bash
echo ""
echo "=== 驗證 1：內容一致性 ==="

for frame_id in 0 1 5 10; do
    input=$(grep "\"frame_id\":$frame_id" simple_metadata.ndjson)
    output=$(grep "\"frame_id\":$frame_id" extracted_simple.ndjson)
    
    if [ "$input" = "$output" ]; then
        echo "✅ frame_id=$frame_id：一致"
    else
        echo "❌ frame_id=$frame_id：不一致"
        echo "  輸入: $input"
        echo "  輸出: $output"
    fi
done
```

**預期結果**：
```
✅ frame_id=0：一致
✅ frame_id=1：一致
✅ frame_id=5：一致
✅ frame_id=10：一致
```

---

## 測試用例 2：複雜測試（多物體 + 多欄位）

### 2A. 建立複雜 Metadata 檔案

```bash
cat > complex_metadata.ndjson << 'EOF'
{"frame_id":0,"timestamp":"2026-02-21T00:00:00.000","objects":[]}
{"frame_id":1,"timestamp":"2026-02-21T00:00:00.033","objects":[{"class":"person","x":100,"y":150,"w":80,"h":200,"confidence":0.95,"id":1}],"scene":"outdoor"}
{"frame_id":2,"timestamp":"2026-02-21T00:00:00.067","objects":[{"class":"person","x":105,"y":155,"w":82,"h":202,"confidence":0.92,"id":1},{"class":"car","x":250,"y":120,"w":150,"h":100,"confidence":0.87,"id":101}],"event":"motion"}
{"frame_id":5,"timestamp":"2026-02-21T00:00:00.167","objects":[{"class":"bicycle","x":50,"y":200,"w":60,"h":100,"confidence":0.78,"id":201}]}
{"frame_id":10,"timestamp":"2026-02-21T00:00:00.333","objects":[{"class":"person","x":200,"y":100,"w":90,"h":210,"confidence":0.99,"id":1}],"alert":true}
EOF

echo "✓ 建立複雜測試檔案"
wc -l complex_metadata.ndjson
head -2 complex_metadata.ndjson
```

### 2B. 執行完整工作流程

```bash
echo ""
echo "=== 測試複雜案例 add_metadata_track ==="
$MP4MUX add_metadata_track "$TEST_MP4" complex_metadata.ndjson output_complex.mp4

if [ -f output_complex.mp4 ]; then
    echo "✅ 成功生成 output_complex.mp4"
    ls -lh output_complex.mp4
else
    echo "❌ 失敗"
    exit 1
fi

echo ""
echo "=== 測試複雜案例 extract_metadata_track ==="
$MP4MUX extract_metadata_track output_complex.mp4 extracted_complex.ndjson

if [ -f extracted_complex.ndjson ]; then
    echo "✅ 成功提取"
    echo "提取幀數：$(wc -l < extracted_complex.ndjson)"
    echo ""
    echo "提取的 frame_id=1 (包含多個欄位)："
    grep '"frame_id":1' extracted_complex.ndjson
else
    echo "❌ 失敗"
    exit 1
fi
```

---

## 測試用例 3：邊界情況

### 3A. 只有一幀的 Metadata

```bash
cat > single_frame.ndjson << 'EOF'
{"frame_id":5,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
EOF

echo "=== 測試：只有一幀 ==="
$MP4MUX add_metadata_track "$TEST_MP4" single_frame.ndjson output_single.mp4
$MP4MUX extract_metadata_track output_single.mp4 extracted_single.ndjson

echo "提取的內容（前 8 行）："
head -8 extracted_single.ndjson
```

### 3B. 連續幀的 Metadata

```bash
cat > consecutive_frames.ndjson << 'EOF'
{"frame_id":0,"objects":[{"type":"A"}]}
{"frame_id":1,"objects":[{"type":"B"}]}
{"frame_id":2,"objects":[{"type":"C"}]}
{"frame_id":3,"objects":[{"type":"D"}]}
{"frame_id":4,"objects":[{"type":"E"}]}
EOF

echo ""
echo "=== 測試：連續幀 ==="
$MP4MUX add_metadata_track "$TEST_MP4" consecutive_frames.ndjson output_consecutive.mp4
$MP4MUX extract_metadata_track output_consecutive.mp4 extracted_consecutive.ndjson

echo "輸入："
cat consecutive_frames.ndjson
echo ""
echo "輸出："
cat extracted_consecutive.ndjson
```

---

## 驗證檢查清單

### ✅ add_metadata_track 檢查清單
- [ ] MP4 檔案成功生成
- [ ] 檔案大小 > 原始檔案大小
- [ ] 無錯誤訊息或警告（除了模塊提示）
- [ ] 執行退出碼 = 0

### ✅ extract_metadata_track 檢查清單
- [ ] NDJSON 檔案成功生成
- [ ] 檔案非空
- [ ] 包含有效的 JSON 行
- [ ] 所有行都包含 "frame_id" 欄位
- [ ] 執行退出碼 = 0

### ✅ 往返一致性檢查清單
- [ ] 輸入的 frame_id 在輸出中都能找到
- [ ] 輸入的 frame 內容與輸出完全相同
- [ ] 中間 frame 被正確填充為 `{"frame_id":X,"objects":[]}`
- [ ] 沒有資料損失或損壞
- [ ] JSON 格式始終有效

---

## 效能指標

基於 v2.4.0-lance-dev 測試結果：

| 操作 | 檔案大小 | 耗時 | 狀態 |
|------|---------|------|------|
| add_metadata_track (10s MP4) | 35 KB → 35 KB | <1s | ✅ |
| extract_metadata_track | 11 frames/output | <1s | ✅ |
| 往返驗證 | 100% 匹配 | <1s | ✅ |

---

## 常見問題

### Q1：為什麼提取的幀數比輸入多？
**A**：系統會自動填充缺失的幀。例如，輸入 frame_id=0,1,5,10，輸出會包含 frame_id=0-10（中間幀用預設值填充）。

### Q2：是否支援其他 JSON 結構？
**A**：是的。任何有效的 JSON 都被支援，只要包含 "frame_id" 欄位。

### Q3：時間同步如何工作？
**A**：frame_id 直接對應 DTS（在 30 fps timescale 下）。例如：
- frame_id=0 → DTS=0 → 第 0 秒
- frame_id=1 → DTS=1 → 第 1/30 秒
- frame_id=30 → DTS=30 → 第 1 秒

### Q4：支援動態幀速率嗎？
**A**：目前固定為 30 fps。未來版本可能支援可配置的 timescale。

---

## 故障排除

### 問題：add_metadata_track 失敗
```bash
# 檢查輸入 MP4
file input.mp4

# 檢查 NDJSON 格式
head -5 metadata.ndjson
jq . < metadata.ndjson  # 如果有 jq 工具

# 檢查幀 ID 順序
grep -o '"frame_id":[0-9]*' metadata.ndjson | sort
```

### 問題：extract_metadata_track 輸出為空
```bash
# 檢查 metadata track 是否真的被嵌入
file output.mp4
ls -lh output.mp4

# 嘗試用其他工具檢查（如果可用）
# MP4Box -info output.mp4
```

### 問題：JSON 格式錯誤
```bash
# 驗證 NDJSON 格式
while IFS= read -r line; do
    echo "$line" | jq . > /dev/null && echo "✓ $line" || echo "✗ $line"
done < metadata.ndjson
```

---

## 快速參考

### 基本命令
```bash
# 嵌入 metadata
mp4muxdemux add_metadata_track input.mp4 metadata.ndjson output.mp4

# 提取 metadata
mp4muxdemux extract_metadata_track output.mp4 extracted.ndjson

# 驗證 JSON
jq . < metadata.ndjson
```

### 建立測試檔案範本
```bash
# 空物體列表
{"frame_id":0,"objects":[]}

# 單個物體
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}

# 多個物體
{"frame_id":2,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200},{"class":"car","x":250,"y":120,"w":150,"h":100}]}

# 帶進階屬性
{"frame_id":3,"objects":[{"class":"person","confidence":0.95,"id":1}],"timestamp":"2026-02-21T00:00:00","metadata":"custom"}
```

---

## 相關文件

- [README.md](README.md) - 基礎使用指南
- [main.c](main.c) - 源代碼實現
- [test_metadata.sh](test_metadata.sh) - 自動化測試腳本
