# Fragmented MP4 (fMP4) 实时流写入系统

## 📚 概览

本系统提供高能的实时 MP4 写入能力，支持同时处理视频、音频和元数据流。适用于：

- 🎥 **实时摄像头录制** - 边录边播放
- 📡 **直播流媒体** - HLS/DASH 直播
- 🤖 **检测结果嵌入** - 实时 AI 检测结果
- 💾 **碎片存储** - 容错和恢复

## 🏗️ 架构设计

### 文件结构

```
fmp4_file.mp4
├── ftyp              # 文件类型（可立即识别）
├── moov              # 初始化段（描述所有轨道）
└── moof/mdat 序列    # 可无限追加的媒体片段
    ├── moof #1 → mdat #1  (第 1 秒)
    ├── moof #2 → mdat #2  (第 2 秒)
    ├── moof #3 → mdat #3  (第 3 秒)
    └── ...
```

**关键优势**：
- ✅ 播放器可在录制进行中开始播放（基于片段大小）
- ✅ 每个片段独立，可恢复和续接
- ✅ 符合 HLS/DASH 流媒体标准
- ✅ 灵活的时间戳管理

### 三个独立的轨道

| 轨道 | 频率 | 数据类型 | 用途 |
|------|------|---------|------|
| **视频** | 30 fps | H.264 NAL | 视觉内容 |
| **音频** | 48 kHz | AAC 样本 | 声音内容 |
| **元数据** | 按需 | JSON 文本 | 检测结果、字幕等 |

## 🔧 API 使用

### 1. 初始化

```c
#include "realtime_mp4.h"

// 打开实时 MP4 写入器
RealtimeMP4Writer *writer = realtime_mp4_open(
    "/path/to/output.mp4",
    1280,               // 视频宽度
    720,                // 视频高度
    30,                 // 30 fps
    48000,              // 48 kHz 音频
    2                   // 立体声
);

if (!writer) {
    fprintf(stderr, "Failed to open MP4\n");
    return 1;
}
```

### 2. 实时写入视频

```c
// 当摄像头产生帧时
while (recording) {
    // 从摄像头获取 H.264 NAL
    u8 *h264_nal = camera.capture_h264_frame();  // Annex B 格式
    u32 nal_size = camera.get_frame_size();
    
    // 获取当前时间戳（微秒）
    u64 timestamp_us = realtime_mp4_get_time_us();
    
    // 添加到 MP4
    GF_Err e = realtime_mp4_add_video_frame(writer, h264_nal, nal_size, timestamp_us);
    if (e != GF_OK) {
        fprintf(stderr, "Error adding video: %s\n", gf_error_to_string(e));
    }
    
    free(h264_nal);
}
```

### 3. 实时写入音频

```c
// 当音频缓冲有数据时
while (recording) {
    if (audio_buffer.has_data()) {
        u8 *aac_frame = audio_buffer.read_frame();  // ADTS 格式
        u32 frame_size = audio_buffer.get_size();
        
        u64 timestamp_us = realtime_mp4_get_time_us();
        
        GF_Err e = realtime_mp4_add_audio_samples(writer, aac_frame, frame_size, timestamp_us);
        if (e != GF_OK) {
            fprintf(stderr, "Error adding audio: %s\n", gf_error_to_string(e));
        }
        
        free(aac_frame);
    }
}
```

### 4. 实时写入元数据

```c
// 当有检测结果时
if (detector.has_detection()) {
    // 构建 JSON 元数据
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"timestamp_ms\":%llu,\"frame_id\":%u,\"objects\":["
        "{\"class\":\"person\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"confidence\":%.2f}"
        "]}",
        current_time_ms,
        frame_id,
        detection.x, detection.y, 
        detection.width, detection.height,
        detection.confidence);
    
    u64 timestamp_us = realtime_mp4_get_time_us();
    
    GF_Err e = realtime_mp4_add_metadata(writer, json, timestamp_us);
    if (e != GF_OK) {
        fprintf(stderr, "Error adding metadata: %s\n", gf_error_to_string(e));
    }
}
```

### 5. 创建片段边界

```c
// 推荐每 1 秒调用一次以启用分段流
if (time % 1000 == 0) {  // 每 1000ms
    GF_Err e = realtime_mp4_flush_fragment(writer);
    if (e != GF_OK) {
        fprintf(stderr, "Error flushing fragment: %s\n", gf_error_to_string(e));
    }
}
```

### 6. 完成并关闭

```c
// 当录制完成或需要停止时
GF_Err e = realtime_mp4_close(writer);
if (e != GF_OK) {
    fprintf(stderr, "Error closing MP4: %s\n", gf_error_to_string(e));
    return 1;
}

// 之后 MP4 文件可以完全播放
```

## ⏱️ 时间戳管理

### 统一的微秒基准

```c
// 所有数据使用相同的时间参考
u64 start_time_us = realtime_mp4_get_time_us();

while (recording) {
    u64 current_time_us = realtime_mp4_get_time_us();
    u64 elapsed_us = current_time_us - start_time_us;
    
    // 视频 (30 fps, 90000 Hz timescale)
    // frame_id = 0 → DTS = 0
    // frame_id = 1 → DTS = 3000 (90000/30)
    
    // 音频 (48 kHz)
    // sample_id = 0 → DTS = 0
    // sample_id = 2048 → DTS = 2048
    
    // 元数据 (30 fps, 与视频同步)
    // detection frame 0 → DTS = 0
}
```

### 时间戳转换

```c
// 将微秒转换为指定 timescale 的 DTS
u32 dts = realtime_mp4_timestamp_to_dts(timestamp_us, timescale);

// 例如：
// timestamp_us = 1000000 (1 秒)
// timescale = 90000 (视频)
// dts = 90000
```

## 🧪 运行示例程序

### 编译

```bash
cd /Users/lance/work/Github/gpac/applications/testapps/mp4muxdemux

# 编译实时库和示例
gcc -o realtime_example realtime_example.c realtime_mp4.c \
    -I/Users/lance/work/Github/gpac/include \
    -L/Users/lance/work/Github/gpac/bin/gcc \
    -lgpac -lm

# 或使用更完整的编译选项
gcc -Wall -Wextra -O2 \
    -I/Users/lance/work/Github/gpac/include \
    -L/Users/lance/work/Github/gpac/bin/gcc \
    -o realtime_example realtime_example.c realtime_mp4.c -lgpac -lm
```

### 运行

```bash
# 设置库路径
export DYLD_LIBRARY_PATH=/Users/lance/work/Github/gpac/bin/gcc:$DYLD_LIBRARY_PATH

# 执行示例
./realtime_example

# 输出
# ========================================
#   Fragmented MP4 Real-time Example
# ========================================
# 
# [RealtimeMP4] Opened fMP4 file: /tmp/realtime_output.mp4
# [RealtimeMP4] Video: 320x240@30fps, Audio: 48000Hz/2-ch, Metadata: JSON
# 
# [Main] 开始模拟实时数据流...
#        视频：30 fps (每 33ms 一帧)
#        音频：48 kHz (每 43ms 一个 frame)
#        元数据：每 5 帧检测一次对象
#        运行时长：~3 秒
# 
# [100 ms] 视频帧 #0
# ...
```

### 验证输出

```bash
# 检查文件信息
file /tmp/realtime_output.mp4

# 查看详细信息（如果有 ffprobe）
ffprobe /tmp/realtime_output.mp4

# 播放（如果有 ffplay）
ffplay /tmp/realtime_output.mp4
```

## 📊 数据流示例

```
时间轴（毫秒）
0ms      33ms     66ms     99ms     132ms    165ms    ...
│        │        │        │        │        │
├─────────────────────────────────────────────
│   视频 #0    │   视频 #1    │   视频 #2    │
│              │              │              │
└─────────────────────────────────────────────
│   音频 Frame1 (48000 Hz)    │  音频 Frame2  │
│              (2048 samples) │              │
└─────────────────────────────────────────────
     元数据(frame)           元数据(frame)
     {"frame_id":0,...}     {"frame_id":5,...}
└─────────────────────────────────────────────

片段边界 ← 每 1000ms flush fragment
```

## 🔄 实时循环伪代码

```c
// 初始化
RealtimeMP4Writer *writer = realtime_mp4_open(...)
u64 start_time_us = realtime_mp4_get_time_us()

while (recording) {
    u64 current_time_us = realtime_mp4_get_time_us()
    
    // 1. 处理视频
    if (video_frame_available()) {
        frame = camera.read()
        realtime_mp4_add_video_frame(writer, frame.data, frame.size, current_time_us)
        
        // 2. 检查是否有检测结果
        if (detector.has_result(frame.id)) {
            metadata = detector.get_json()
            realtime_mp4_add_metadata(writer, metadata, current_time_us)
        }
    }
    
    // 3. 处理音频
    if (audio_available()) {
        samples = audio_device.read()
        realtime_mp4_add_audio_samples(writer, samples, count, current_time_us)
    }
    
    // 4. 定期刷新片段（每 1 秒）
    if ((current_time_us - start_time_us) % 1000000 == 0) {
        realtime_mp4_flush_fragment(writer)
    }
}

// 完成
realtime_mp4_close(writer)
```

## 📈 性能特性

| 功能 | 性能 | 备注 |
|------|------|------|
| 视频添加 | < 1ms | 取决于 NAL 大小 |
| 音频添加 | < 1ms | 通常很快 |
| 元数据添加 | < 1ms | JSON 通常小 |
| 片段刷新 | ~10-50ms | I/O 操作 |
| 内存使用 | ~10-50MB | 取决于缓冲大小 |

## ⚠️ 常见问题

### Q: 如何确保音视频同步？
**A**: 使用相同的时间戳源（系统时钟或校准的外部时钟）。系统会根据各轨道的 timescale 自动转换。

### Q: 如果中途崩溃怎么办？
**A**: 未刷新的片段会丢失。可使用最后一个完整的片段（已刷新的）进行恢复。

### Q: 元数据的 timestamp_ms 字段必须吗？
**A**: 推荐包含，便于播放器和分析工具追踪时间。JSON 格式灵活，可包含任意字段。

### Q: 最大录制时长？
**A**: 理论上无限。fMP4 格式允许无限个片段，受存储空间限制。

### Q: 能否改变视频分辨率或帧率？
**A**: 不能。moov 头在开始时确定，所有数据必须遵循初始参数。

## 📖 相关文档

- [realtime_mp4.h](realtime_mp4.h) - API 定义
- [realtime_mp4.c](realtime_mp4.c) - 实现
- [realtime_example.c](realtime_example.c) - 使用示例
- [TEST-METADATA.md](TEST-METADATA.md) - 离线元数据测试
- [README.md](README.md) - 基础使用指南
