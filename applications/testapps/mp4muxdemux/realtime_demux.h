/*
 * Realtime MP4 Demuxer Header
 * 
 * 用途：从 fMP4 中逐 sample 读取视频、音频、元数据
 * 特性：
 *   - 支持 fragmented 和 regular 模式
 *   - 离线读取（不需要边写边播）
 *   - 返回时间戳、轨道 ID、数据
 */

#ifndef REALTIME_DEMUX_H
#define REALTIME_DEMUX_H

#include <gpac/isomedia.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sample 数据结构 */
typedef struct {
    u32 track_id;           /* 轨道 ID (1=video, 2=audio, 3=metadata) */
    u8 *data;               /* 样本数据 */
    u32 data_size;          /* 数据大小 */
    u64 dts;                /* 解码时间戳 (毫秒) */
    u64 pts;                /* 显示时间戳 (毫秒) */
    u32 timescale;          /* 该轨道的 timescale */
    Bool is_sync;           /* 是否关键帧/同步样本 */
    u32 sample_index;       /* 全局样本索引号 */
} RealtimeDemuxSample;

/* Demuxer 上下文 */
typedef struct {
    GF_ISOFile *file;
    
    /* 轨道信息 */
    u32 num_tracks;
    u32 *track_ids;         /* 轨道 ID 数组 */
    u32 *track_timescales;  /* 对应的 timescale */
    
    /* 迭代状态 */
    u32 *sample_indices;    /* 每个轨道当前的样本索引 */
    u32 *total_samples;     /* 每个轨道的总样本数 */
    
    /* 统计 */
    u32 total_samples_read;
} RealtimeDemuxer;

/* ========== 核心 API ========== */

/**
 * 打开文件用于 demux
 * 
 * @param mp4_path      MP4 文件路径
 * @return 返回 demuxer 上下文，失败返回 NULL
 */
RealtimeDemuxer* realtime_demux_open(const char *mp4_path);

/**
 * 读取下一个样本（按时间戳顺序）
 * 
 * @param demuxer       demux 上下文
 * @param sample        样本结构（输出）
 * @return GF_OK 成功，GF_EOS 已读完所有样本，其他错误
 * 
 * 调用者需要自行释放 sample->data
 */
GF_Err realtime_demux_read_sample(RealtimeDemuxer *demuxer, RealtimeDemuxSample *sample);

/**
 * 获取轨道信息
 * 
 * @param demuxer       demux 上下文
 * @param track_id      轨道 ID (1/2/3)
 * @return 总样本数，失败返回 0
 */
u32 realtime_demux_get_track_sample_count(RealtimeDemuxer *demuxer, u32 track_id);

/**
 * 获取轨道媒体类型
 * 
 * @param demuxer       demux 上下文
 * @param track_id      轨道 ID
 * @return 媒体类型字符串 ("video"/"audio"/"metadata")
 */
const char* realtime_demux_get_track_type(RealtimeDemuxer *demuxer, u32 track_id);

/**
 * 获取视频分辨率
 * 
 * @param demuxer       demux 上下文
 * @param width         宽度（输出）
 * @param height        高度（输出）
 * @return GF_OK 成功，GF_BAD_PARAM 不是视频轨道
 */
GF_Err realtime_demux_get_video_size(RealtimeDemuxer *demuxer, u32 *width, u32 *height);

/**
 * 获取音频信息
 * 
 * @param demuxer       demux 上下文
 * @param sample_rate   采样率（输出）
 * @param channels      通道数（输出）
 * @return GF_OK 成功，GF_BAD_PARAM 不是音频轨道
 */
GF_Err realtime_demux_get_audio_info(RealtimeDemuxer *demuxer, u32 *sample_rate, u32 *channels);

/**
 * 关闭 demuxer 
 * 
 * @param demuxer       demux 上下文
 * @return GF_OK
 */
GF_Err realtime_demux_close(RealtimeDemuxer *demuxer);

/* ========== 工具函数 ========== */

/**
 * 打印 demux 统计信息
 */
void realtime_demux_print_stats(RealtimeDemuxer *demuxer);

#ifdef __cplusplus
}
#endif

#endif /* REALTIME_DEMUX_H */
