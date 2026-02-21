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

/* 生成简单的 H.264 NAL 单元（用于测试） */
u8* generate_h264_frame(u32 frame_id, u32 *size) {
    /* 实际应用中这会来自摄像头/编码器 */
    u8 *nal = (u8 *)malloc(1024);
    
    /* 模拟 NAL 单元：0x00 0x00 0x01 [type] [frame_id_bytes] */
    nal[0] = 0x00;
    nal[1] = 0x00;
    nal[2] = 0x01;
    nal[3] = 0x65;  /* H.264 NAL type 5 (IDR) */
    
    /* 填充一些假数据 */
    for (u32 i = 4; i < 512; i++) {
        nal[i] = (u8)((frame_id + i) & 0xFF);
    }
    
    *size = 512;
    return nal;
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
        320, 240,        /* 视频分辨率 */
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
    printf("       运行时长：~3 秒\n\n");

    u64 start_time = realtime_mp4_get_time_us();
    u32 frame_count = 0;
    u32 audio_frame_count = 0;
    u32 last_fragment_time = 0;

    /* 模拟 ~3 秒的录制 */
    while (frame_count < 100) {
        u64 current_time_us = realtime_mp4_get_time_us() - start_time;
        
        /* 每 33ms 产生一个视频帧 (30 fps) */
        if (current_time_us >= frame_count * 33333) {
            u32 nal_size = 0;
            u8 *h264_frame = generate_h264_frame(frame_count, &nal_size);
            
            realtime_mp4_add_video_frame(writer, h264_frame, nal_size, 
                                        start_time + current_time_us);
            
            /* 每 5 帧产生元数据 */
            if (frame_count % 5 == 0) {
                char *metadata = generate_metadata(frame_count, frame_count % 10 < 3);
                realtime_mp4_add_metadata(writer, metadata, 
                                         start_time + current_time_us);
                free(metadata);
            }
            
            if (frame_count % 20 == 0) {
                printf("[%4u ms] 视频帧 #%u\n", 
                       (u32)(current_time_us / 1000), frame_count);
            }
            
            free(h264_frame);
            frame_count++;
        }
        
        /* 每 43ms 产生一个音频 frame (48 kHz) */
        if (current_time_us >= audio_frame_count * 42667) {
            u32 aac_size = 0;
            u8 *aac_frame = generate_aac_frame(audio_frame_count, 2048, &aac_size);
            
            realtime_mp4_add_audio_samples(writer, aac_frame, aac_size,
                                          start_time + current_time_us);
            
            free(aac_frame);
            audio_frame_count++;
        }
        
        /* 每 1 秒创建一个片段边界 */
        u32 current_seconds = (u32)(current_time_us / 1000000);
        if (current_seconds > last_fragment_time && current_seconds % 1 == 0) {
            realtime_mp4_flush_fragment(writer);
            last_fragment_time = current_seconds;
        }
        
        /* 短暂睡眠以避免 CPU 忙转 */
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
