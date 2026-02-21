/*
 * Fragmented MP4 (fMP4) Real-time Writer
 * 
 * 用途：实时写入视频、音频、元数据到 fMP4 文件
 * 特性：
 *   - 边写边播（支持 HLS/DASH 流）
 *   - 独立片段（每个 moof/mdat 是独立单元）
 *   - 灵活的时间戳管理
 *   - 元数据轨道支持（JSON 格式）
 */

#ifndef REALTIME_MP4_H
#define REALTIME_MP4_H

#include <gpac/isomedia.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 数据结构定义 ========== */

typedef struct {
    GF_ISOFile *file;
    
    /* 轨道信息 */
    u32 video_track;
    u32 audio_track;
    u32 metadata_track;
    GF_ISOTrackID video_track_id;
    GF_ISOTrackID audio_track_id;
    GF_ISOTrackID metadata_track_id;

    u32 video_desc_idx;
    u32 audio_desc_idx;
    u32 metadata_desc_idx;
    
    /* 配置参数 */
    u32 video_fps;
    u32 video_timescale;        /* 通常 90000 */
    u32 audio_sample_rate;
    u32 audio_timescale;        /* = audio_sample_rate */
    u32 metadata_timescale;     /* 通常 30 */
    
    /* 时间管理 */
    u64 start_time_us;          /* 录制开始时间 (微秒) */
    u64 last_video_timestamp;   /* 最后一个视频样本的时间戳 */
    u64 last_audio_timestamp;   /* 最后一个音频样本的时间戳 */
    u64 last_metadata_timestamp;/* 最后一个元数据样本的时间戳 */
    
    /* 片段管理 */
    u32 fragment_counter;
    u64 fragment_duration_us;   /* 片段时长 1 秒 */
    u64 next_fragment_time_us;  /* 下一个片段的时间 */
    
    /* 统计信息 */
    u32 video_sample_count;
    u32 audio_sample_count;
    u32 metadata_sample_count;
    
    /* 缓冲 */
    char *output_path;
    
} RealtimeMP4Writer;

/* ========== 核心 API ========== */

/**
 * 打开实时 MP4 写入器
 * 
 * @param output_file       输出文件路径
 * @param video_width       视频宽度 (像素)
 * @param video_height      视频高度 (像素)
 * @param video_fps         视频帧率 (fps)
 * @param audio_sample_rate 音频采样率 (Hz)
 * @param audio_channels    音频通道数
 * @return 写入器上下文，失败返回 NULL
 */
RealtimeMP4Writer* realtime_mp4_open(
    const char *output_file,
    u32 video_width, u32 video_height, u32 video_fps,
    u32 audio_sample_rate, u32 audio_channels);

/**
 * 添加视频帧
 * 
 * @param ctx               写入器上下文
 * @param h264_nal          H.264 NAL 单元数据 (Annex B 格式)
 * @param nal_size          NAL 数据大小
 * @param timestamp_us      时间戳 (微秒)
 * @return 成功返回 GF_OK
 */
GF_Err realtime_mp4_add_video_frame(
    RealtimeMP4Writer *ctx,
    const u8 *h264_nal, u32 nal_size,
    u64 timestamp_us);

/**
 * 添加音频样本颗粒
 * 
 * @param ctx               写入器上下文
 * @param aac_data          AAC 音频数据 (ADTS 格式)
 * @param data_size         数据大小
 * @param timestamp_us      时间戳 (微秒)
 * @return 成功返回 GF_OK
 */
GF_Err realtime_mp4_add_audio_samples(
    RealtimeMP4Writer *ctx,
    const u8 *aac_data, u32 data_size,
    u64 timestamp_us);

/**
 * 添加元数据样本
 * 
 * @param ctx               写入器上下文
 * @param json_payload      JSON 元数据 (必须包含 "timestamp_ms" 字段)
 * @param timestamp_us      时间戳 (微秒)
 * @return 成功返回 GF_OK
 * 
 * 示例 JSON:
 * {"timestamp_ms":0,"frame_id":0,"objects":[]}
 * {"timestamp_ms":33,"frame_id":1,"objects":[{"class":"person","x":100,"y":150}]}
 */
GF_Err realtime_mp4_add_metadata(
    RealtimeMP4Writer *ctx,
    const char *json_payload,
    u64 timestamp_us);

/**
 * 创建新的片段边界
 * 
 * 调用此函数会：
 * 1. 完成当前 moof/mdat 片段
 * 2. 开始新的 moof/mdat 片段
 * 3. 允许播放器播放完整的片段
 * 
 * 推荐每 1 秒调用一次以实现分段流式传输
 * 
 * @param ctx               写入器上下文
 * @return 成功返回 GF_OK
 */
GF_Err realtime_mp4_flush_fragment(RealtimeMP4Writer *ctx);

/**
 * 获取当前录制时长
 * 
 * @param ctx               写入器上下文
 * @return 总录制时长 (毫秒)
 */
u64 realtime_mp4_get_duration_ms(RealtimeMP4Writer *ctx);

/**
 * 获取统计信息
 * 
 * @param ctx               写入器上下文
 * @param video_samples     视频样本数计数
 * @param audio_samples     音频样本数计数
 * @param metadata_samples  元数据样本数计数
 */
void realtime_mp4_get_stats(
    RealtimeMP4Writer *ctx,
    u32 *video_samples,
    u32 *audio_samples,
    u32 *metadata_samples);

/**
 * 关闭实时 MP4 写入器
 * 
 * 调用此函数会：
 * 1. 冲 flush 待处理的数据
 * 2. 更新 moov 头中的总时长
 * 3. 关闭文件
 * 4. 释放所有资源
 * 
 * 之后 MP4 文件可以被完全播放
 * 
 * @param ctx               写入器上下文
 * @return 成功返回 GF_OK
 */
GF_Err realtime_mp4_close(RealtimeMP4Writer *ctx);

/* ========== 工具函数 ========== */

/**
 * 获取当前系统时间 (微秒)
 */
u64 realtime_mp4_get_time_us(void);

/**
 * 将微秒时间戳转换为指定 timescale 的 DTS
 */
u32 realtime_mp4_timestamp_to_dts(u64 timestamp_us, u32 timescale);

#ifdef __cplusplus
}
#endif

#endif /* REALTIME_MP4_H */
