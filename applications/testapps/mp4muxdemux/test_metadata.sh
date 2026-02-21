#!/bin/bash
#
# MP4MuxDemux Metadata Track 自動化測試腳本
# 用途：完整測試 add_metadata_track 和 extract_metadata_track 功能
# 使用：./test_metadata.sh [clean]
#

# set -e  # 移除：會導致在某個命令失敗時直接退出

# ==================== 配置 ====================
MP4MUX="/Users/lance/work/Github/gpac/bin/gcc/mp4muxdemux"
TEST_MP4="/Users/lance/work/Github/gpac/testsuite/external_media/noFragsDefault/counter-bifs-10sec-160x102-bipbop_420_avc.mp4"
TESTDIR="/tmp/metadata_test_$$"
PASSED=0
FAILED=0

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'  # No Color

# ==================== 函數定義 ====================

print_header() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
    ((PASSED++))
}

print_failure() {
    echo -e "${RED}❌ $1${NC}"
    ((FAILED++))
}

print_info() {
    echo -e "${YELLOW}ℹ️  $1${NC}"
}

check_command() {
    if ! command -v "$1" &> /dev/null; then
        print_failure "$1 未安裝"
        return 1
    fi
}

cleanup() {
    if [ "$1" = "clean" ]; then
        print_info "清理測試目錄..."
        rm -rf "$TESTDIR"
    fi
}

# ==================== 初始化 ====================

print_header "MP4MuxDemux Metadata Track 自動化測試"

# 檢查依賴
echo "檢查依賴..."

# 檢查 MP4MUX 是否存在
if [ ! -f "$MP4MUX" ]; then
    print_failure "mp4muxdemux 工具不存在：$MP4MUX"
    print_info "請先執行：cd /Users/lance/work/Github/gpac && make lib && cd applications/testapps/mp4muxdemux && make"
    exit 1
fi

# 設置動態庫路徑
export DYLD_LIBRARY_PATH="/Users/lance/work/Github/gpac/bin/gcc:$DYLD_LIBRARY_PATH"
print_info "已設置 DYLD_LIBRARY_PATH"

# 測試工具是否可執行
if ! "$MP4MUX" 2>&1 | grep -q "Usage"; then
    print_failure "無法執行 mp4muxdemux 工具"
    exit 1
fi
print_success "mp4muxdemux 工具可用"

check_command "jq" || print_info "jq 未安裝，某些測試將被跳過"

# 準備測試目錄
mkdir -p "$TESTDIR"
cd "$TESTDIR"
print_info "測試目錄：$TESTDIR"

# 驗證輸入檔案
if [ ! -f "$TEST_MP4" ]; then
    print_failure "測試 MP4 檔案不存在：$TEST_MP4"
    exit 1
fi
print_success "測試 MP4 檔案存在"

# ==================== 測試 1：簡單案例 ====================

print_header "測試 1：簡單案例（4 幀）"

# 1A. 建立測試檔案
echo "步驟 1A：建立 metadata 檔案..."
cat > simple_metadata.ndjson << 'EOF'
{"frame_id":0,"objects":[]}
{"frame_id":1,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
{"frame_id":5,"objects":[{"class":"person","x":110,"y":160,"w":85,"h":205}]}
{"frame_id":10,"objects":[{"class":"car","x":200,"y":100,"w":150,"h":120}]}
EOF
print_success "建立 simple_metadata.ndjson"

# 1B. 測試 add_metadata_track
echo ""
echo "步驟 1B：執行 add_metadata_track..."
if "$MP4MUX" add_metadata_track "$TEST_MP4" simple_metadata.ndjson output_simple.mp4 2>/dev/null; then
    if [ -f output_simple.mp4 ]; then
        SIZE=$(du -h output_simple.mp4 | cut -f1)
        print_success "add_metadata_track 成功，檔案大小：$SIZE"
    else
        print_failure "add_metadata_track 執行但無輸出檔案"
    fi
else
    print_failure "add_metadata_track 執行失敗"
fi

# 1C. 測試 extract_metadata_track
echo ""
echo "步驟 1C：執行 extract_metadata_track..."
if "$MP4MUX" extract_metadata_track output_simple.mp4 extracted_simple.ndjson 2>/dev/null; then
    if [ -f extracted_simple.ndjson ]; then
        LINE_COUNT=$(wc -l < extracted_simple.ndjson)
        print_success "extract_metadata_track 成功，提取 $LINE_COUNT 幀"
    else
        print_failure "extract_metadata_track 執行但無輸出檔案"
    fi
else
    print_failure "extract_metadata_track 執行失敗"
fi

# 1D. 驗證內容一致性
echo ""
echo "步驟 1D：驗證內容一致性..."
CONSISTENCY_OK=true
for frame_id in 0 1 5 10; do
    input=$(grep "\"frame_id\":$frame_id" simple_metadata.ndjson)
    output=$(grep "\"frame_id\":$frame_id" extracted_simple.ndjson)
    
    if [ "$input" = "$output" ]; then
        echo -e "  ${GREEN}✓${NC} frame_id=$frame_id 一致"
    else
        echo -e "  ${RED}✗${NC} frame_id=$frame_id 不一致"
        CONSISTENCY_OK=false
    fi
done

if [ "$CONSISTENCY_OK" = true ]; then
    print_success "所有框架內容一致"
else
    print_failure "部分框架內容不一致"
fi

# 1E. 驗證中間幀填充
echo ""
echo "步驟 1E：驗證中間幀填充..."
FILL_OK=true
for frame_id in 2 3 4 6 7 8 9; do
    if grep -q "\"frame_id\":$frame_id" extracted_simple.ndjson; then
        echo -e "  ${GREEN}✓${NC} frame_id=$frame_id 已填充"
    else
        echo -e "  ${RED}✗${NC} frame_id=$frame_id 缺失"
        FILL_OK=false
    fi
done

if [ "$FILL_OK" = true ]; then
    print_success "中間幀正確填充"
else
    print_failure "某些中間幀未填充"
fi

# ==================== 測試 2：複雜案例 ====================

print_header "測試 2：複雜案例（多物體 + 各種欄位）"

echo "步驟 2A：建立複雜 metadata 檔案..."
cat > complex_metadata.ndjson << 'EOF'
{"frame_id":0,"timestamp":"2026-02-21T00:00:00.000","objects":[]}
{"frame_id":1,"timestamp":"2026-02-21T00:00:00.033","objects":[{"class":"person","x":100,"y":150,"w":80,"h":200,"confidence":0.95,"id":1}],"scene":"outdoor"}
{"frame_id":2,"timestamp":"2026-02-21T00:00:00.067","objects":[{"class":"person","x":105,"y":155,"w":82,"h":202,"confidence":0.92,"id":1},{"class":"car","x":250,"y":120,"w":150,"h":100,"confidence":0.87,"id":101}]}
{"frame_id":5,"timestamp":"2026-02-21T00:00:00.167","objects":[{"class":"bicycle","x":50,"y":200,"w":60,"h":100,"confidence":0.78,"id":201}]}
{"frame_id":10,"timestamp":"2026-02-21T00:00:00.333","objects":[{"class":"person","x":200,"y":100,"w":90,"h":210,"confidence":0.99,"id":1}],"alert":true}
EOF
print_success "建立 complex_metadata.ndjson（5 幀）"

echo ""
echo "步驟 2B：執行 add_metadata_track..."
if "$MP4MUX" add_metadata_track "$TEST_MP4" complex_metadata.ndjson output_complex.mp4 2>/dev/null; then
    print_success "複雜案例 add_metadata_track 成功"
else
    print_failure "複雜案例 add_metadata_track 失敗"
fi

echo ""
echo "步驟 2C：執行 extract_metadata_track..."
if "$MP4MUX" extract_metadata_track output_complex.mp4 extracted_complex.ndjson 2>/dev/null; then
    COMPLEX_LINES=$(wc -l < extracted_complex.ndjson)
    print_success "複雜案例 extract_metadata_track 成功，提取 $COMPLEX_LINES 幀"
else
    print_failure "複雜案例 extract_metadata_track 失敗"
fi

# ==================== 測試 3：邊界案例 ====================

print_header "測試 3：邊界案例"

# 3A. 單一幀
echo "步驟 3A：測試單一幀..."
cat > single_frame.ndjson << 'EOF'
{"frame_id":5,"objects":[{"class":"person","x":100,"y":150,"w":80,"h":200}]}
EOF

if "$MP4MUX" add_metadata_track "$TEST_MP4" single_frame.ndjson output_single.mp4 2>/dev/null; then
    if "$MP4MUX" extract_metadata_track output_single.mp4 extracted_single.ndjson 2>/dev/null; then
        print_success "單一幀測試通過"
    else
        print_failure "單一幀提取失敗"
    fi
else
    print_failure "單一幀嵌入失敗"
fi

# 3B. 連續幀
echo ""
echo "步驟 3B：測試連續幀（0-4）..."
cat > consecutive_frames.ndjson << 'EOF'
{"frame_id":0,"objects":[{"type":"A"}]}
{"frame_id":1,"objects":[{"type":"B"}]}
{"frame_id":2,"objects":[{"type":"C"}]}
{"frame_id":3,"objects":[{"type":"D"}]}
{"frame_id":4,"objects":[{"type":"E"}]}
EOF

if "$MP4MUX" add_metadata_track "$TEST_MP4" consecutive_frames.ndjson output_consecutive.mp4 2>/dev/null; then
    if "$MP4MUX" extract_metadata_track output_consecutive.mp4 extracted_consecutive.ndjson 2>/dev/null; then
        CONS_COUNT=$(wc -l < extracted_consecutive.ndjson)
        if [ "$CONS_COUNT" -eq 5 ]; then
            print_success "連續幀測試通過（提取 5 幀）"
        else
            print_failure "連續幀提取幀數不符（預期 5，得 $CONS_COUNT）"
        fi
    else
        print_failure "連續幀提取失敗"
    fi
else
    print_failure "連續幀嵌入失敗"
fi

# ==================== 測試 4：JSON 驗證 ====================

print_header "測試 4：JSON 驗證"

echo "驗證輸出 JSON 格式..."
JSON_VALID=true
if command -v jq &> /dev/null; then
    while IFS= read -r line; do
        if ! echo "$line" | jq . > /dev/null 2>&1; then
            echo -e "  ${RED}✗${NC} 無效 JSON：$line"
            JSON_VALID=false
        fi
    done < extracted_simple.ndjson
    
    if [ "$JSON_VALID" = true ]; then
        print_success "所有 JSON 行都有效"
    else
        print_failure "部分 JSON 行無效"
    fi
else
    print_info "jq 未安裝，跳過 JSON 驗證"
fi

# ==================== 摘要 ====================

print_header "測試摘要"

echo ""
echo "結果："
echo -e "  ${GREEN}通過：$PASSED${NC}"
echo -e "  ${RED}失敗：$FAILED${NC}"

echo ""
echo "生成的檔案："
ls -lh output*.mp4 extracted*.ndjson 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

echo ""
echo "測試目錄（供檢查）："
echo "  $TESTDIR"

# ==================== 清理 ====================

if [ "$1" = "clean" ]; then
    cleanup clean
    echo ""
    echo "已清理測試檔案"
else
    echo ""
    print_info "若要清理測試目錄，請執行：./test_metadata.sh clean"
fi

# ==================== 最終結果 ====================

echo ""
if [ $FAILED -eq 0 ]; then
    print_header "✅ 所有測試通過！"
    exit 0
else
    print_header "❌ 某些測試失敗"
    exit 1
fi
