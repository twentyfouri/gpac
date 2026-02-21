/*
 * Real-time MP4 Integration Example
 * 
 * 演示如何实时写入视频、音频、元数据到 fMP4 文件
 * 
 * 编译：
 *   gcc -o realtime_example realtime_example.c realtime_mp4.c \
 *       -I/Users/lance/work/Github/gpac/include \
 *       -L/Users/lance/work/Github/gpac/bin/gcc \
 *       -lgpac -lm
 * 
 * 运行：
 *   DYLD_LIBRARY_PATH=./bin/gcc ./realtime_example
 */

#include "realtime_mp4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ========== 模拟数据生成器 ========== */

/* 从 h264/ 目录加载真实 H.264 幀（循环使用 test_frame0.h264 到 test_frame59.h264） */
u8* generate_h264_frame(u32 frame_id, u32 *size) {
    /* 从 h264 目录循环读取真实幀，共 60 个幀 */
    static u8 *cache[60] = {NULL};
    static u32 cache_size[60] = {0};
    static Bool cache_loaded = GF_FALSE;
    
    u32 frame_index = frame_id % 60;  /* 循环使用 */
    
    /* 首次加载所有幀到缓存 */
    if (!cache_loaded) {
        for (u32 i = 0; i < 60; i++) {
            char path[256];
            FILE *f;
            
            snprintf(path, sizeof(path), 
                    "h264/test_frame%u.h264", i);
            
            f = fopen(path, "rb");
            if (!f) {
                fprintf(stderr, "[ERROR] Cannot open %s\n", path);
                continue;
            }
            
            /* 获取文件大小 */
            fseek(f, 0, SEEK_END);
            u32 file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            /* 分配和读取 */
            cache[i] = (u8 *)malloc(file_size);
            if (cache[i]) {
                fread(cache[i], 1, file_size, f);
                cache_size[i] = file_size;
            }
            
            fclose(f);
            fprintf(stderr, "[LOAD] h264/test_frame%u.h264 (%u bytes)\n", i, file_size);
        }
        cache_loaded = GF_TRUE;
    }
    
    /* 返回缓存的幀 */
    if (cache[frame_index]) {
        *size = cache_size[frame_index];
        
        /* 分配新数据以供调用者释放 */
        u8 *frame_copy = (u8 *)malloc(*size);
        memcpy(frame_copy, cache[frame_index], *size);
        
        return frame_copy;
    }
    
    /* 如果加载失败，返回空 */
    fprintf(stderr, "[ERROR] Frame %u not loaded\n", frame_index);
    *size = 0;
    return NULL;
}

/* 生成简单的 AAC 音频（用于测试） */
u8* generate_aac_frame(u32 sample_id, u32 sample_count, u32 *size) {
    /* 实际应用中这会来自麦克风/编码器 */
    u8 *aac = (u8 *)malloc(256);
    
    /* 模拟 ADTS 帧头 */
    aac[0] = 0xFF;  /* ADTS sync word */
    aac[1] = 0xF1;
    
    for (u32 i = 2; i < 256; i++) {
        aac[i] = (u8)((sample_id + i) & 0xFF);
    }
    
    *size = 256;
    return aac;
}

/* 生成 JSON 元数据 */
char* generate_metadata(u32 frame_id, int has_detection) {
    char *json = (char *)malloc(512);
    
    if (has_detection) {
        snprintf(json, 512,
            "{\"timestamp_ms\":%u,\"frame_id\":%u,\"objects\":["
            "{\"class\":\"person\",\"x\":100,\"y\":150,\"w\":80,\"h\":200,\"confidence\":0.95},"
            "{\"class\":\"car\",\"x\":250,\"y\":120,\"w\":150,\"h\":100,\"confidence\":0.87}"
            "]}",
            frame_id * 33,  /* 30 fps = 33ms per frame */
            frame_id);
    } else {
        snprintf(json, 512,
            "{\"timestamp_ms\":%u,\"frame_id\":%u,\"objects\":[]}",
            frame_id * 33,
            frame_id);
    }
    
    return json;
}

/* ========== 模拟录制循环 ========== */

int main(int argc, char **argv) {
    printf("========================================\n");
    printf("  Fragmented MP4 Real-time Example\n");
    printf("========================================\n\n");

    /* 初始化 GPAC 系统 */
    gf_sys_init(GF_MemTrackerNone, NULL);

    /* 创建实时 MP4 写入器 */
    printf("[Main] 创建实时 MP4 文件...\n");
    RealtimeMP4Writer *writer = realtime_mp4_open(
        "/tmp/realtime_output.mp4",
        1280, 720,       /* 视频分辨率 (H.264 test frames: 1280x720) */
        30,              /* 30 fps */
        48000,           /* 48 kHz 音频 */
        2                /* 立体声 */
    );

    if (!writer) {
        fprintf(stderr, "Failed to open MP4 writer\n");
        gf_sys_close();
        return 1;
    }

    /* ========== 模拟实时数据流 ========== */

    printf("\n[Main] 开始模拟实时数据流...\n");
    printf("       视频：30 fps (每 33ms 一帧)\n");
    printf("       音频：48 kHz (每 43ms 一个 frame)\n");
    printf("       元数据：每 5 帧检测一次对象\n");
    printf("       运行时长：~15 秒 (450 video frames)\n\n");

    u64 start_time = realtime_mp4_get_time_us();
    u32 frame_count = 0;
    u32 audio_frame_count = 0;
    u32 last_fragment_frame = 0;

    /* 模拟 ~15 秒的录制 (450 video frames @ 30 fps) */
    while (frame_count < 450) {
        /* 使用幀計數器計算理論時間戳（保證嚴格遞增） */
        u64 video_timestamp_us = start_time + (u64)frame_count * 33333ULL;  /* 30 fps = 33.333ms per frame */
        u64 audio_timestamp_us = start_time + (u64)audio_frame_count * 21333ULL;  /* 48kHz, 1024 samples per frame = 21.333ms */
        
        /* 产生视频帧 */
        u32 nal_size = 0;
        u8 *h264_frame = generate_h264_frame(frame_count, &nal_size);
        
        realtime_mp4_add_video_frame(writer, h264_frame, nal_size, video_timestamp_us);
        
        /* 每 50 帧产生元数据 */
        if (frame_count % 50 == 0) {
            char *metadata = generate_metadata(frame_count, frame_count % 100 < 30);
            realtime_mp4_add_metadata(writer, metadata, video_timestamp_us);
            free(metadata);
        }
        
        if (frame_count % 100 == 0) {
            printf("[%4llu ms] 视频帧 #%u\n", 
                   (video_timestamp_us - start_time) / 1000, frame_count);
        }
        
        free(h264_frame);
        frame_count++;
        
        /* 产生相应的音频帧（保持 A/V 同步） */
        /* 音頻：48kHz 採樣率，每幀 1024 樣本 = 21.333ms/幀 */
        u32 expected_audio_frames = (frame_count * 33333ULL) / 21333ULL;
        while (audio_frame_count < expected_audio_frames) {
            audio_timestamp_us = start_time + (u64)audio_frame_count * 21333ULL;
            
            u32 aac_size = 0;
            u8 *aac_frame = generate_aac_frame(audio_frame_count, 1024, &aac_size);
            
            realtime_mp4_add_audio_samples(writer, aac_frame, aac_size, audio_timestamp_us);
            
            free(aac_frame);
            audio_frame_count++;
        }
        
        /* 每 150 幀（5 秒）創建一個片段邊界 */
        if (frame_count > 0 && frame_count % 150 == 0 && frame_count != last_fragment_frame) {
            realtime_mp4_flush_fragment(writer);
            last_fragment_frame = frame_count;
            printf("  [FRAGMENT] Created at frame %u (%.2f seconds)\n", 
                   frame_count, frame_count / 30.0);
        }
        
        /* 短暂睡眠以模拟实时录制 */
        usleep(1000);  /* 1ms */
    }

    /* ========== 完成并关闭 ========== */

    printf("\n[Main] 完成录制，关闭文件...\n");
    realtime_mp4_close(writer);

    /* 验证输出文件 */
    printf("\n[Main] 输出文件统计：\n");
    system("ls -lh /tmp/realtime_output.mp4");

    printf("\n========================================\n");
    printf("✅ 测试完成！\n");
    printf("生成的文件：/tmp/realtime_output.mp4\n");
    printf("可使用以下命令查看或播放：\n");
    printf("  file /tmp/realtime_output.mp4\n");
    printf("  ffprobe /tmp/realtime_output.mp4\n");
    printf("  ffplay /tmp/realtime_output.mp4\n");
    printf("========================================\n");

    gf_sys_close();
    return 0;
}
