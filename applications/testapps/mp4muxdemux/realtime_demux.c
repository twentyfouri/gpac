/*
 * Realtime MP4 Demuxer Implementation
 * 
 * 按時間戳順序讀取所有軌道的樣本
 */

#include "realtime_demux.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 軌道類型 */
typedef enum {
    TRACK_UNKNOWN = 0,
    TRACK_VIDEO = 1,
    TRACK_AUDIO = 2,
    TRACK_METADATA = 3
} TrackType;

typedef struct {
    u32 track_id;
    u32 timescale;
    u32 sample_count;
    u64 duration;           /* 軌道總時長 */
    TrackType type;
    
    /* 視頻 */
    u32 width, height;
    
    /* 音頻 */
    u32 sample_rate, channels;
} TrackInfo;

/* ========== 內部函數 ========== */

static TrackType get_track_type(GF_ISOFile *file, u32 track_id)
{
    u32 media_type = gf_isom_get_media_type(file, track_id);
    
    if (media_type == GF_ISOM_MEDIA_VISUAL)
        return TRACK_VIDEO;
    if (media_type == GF_ISOM_MEDIA_AUDIO)
        return TRACK_AUDIO;
    if (media_type == GF_ISOM_MEDIA_META)
        return TRACK_METADATA;
    
    return TRACK_UNKNOWN;
}

static u32 get_timescale(GF_ISOFile *file, u32 track_id)
{
    return gf_isom_get_media_timescale(file, track_id);
}

static u32 get_sample_count(GF_ISOFile *file, u32 track_id)
{
    return gf_isom_get_sample_count(file, track_id);
}

static u64 get_track_duration_ms(GF_ISOFile *file, u32 track_id)
{
    u32 timescale = gf_isom_get_media_timescale(file, track_id);
    u64 duration = gf_isom_get_media_duration(file, track_id);
    
    if (timescale == 0)
        return 0;
    
    return (duration * 1000) / timescale;
}

/* 比較函數：用於排序下一個要讀的樣本 */
typedef struct {
    u32 track_idx;          /* demuxer->track_ids 中的索引 */
    u64 dts;                /* 下一個樣本的 DTS (ms) */
    u32 sample_idx;         /* 樣本編號 */
} NextSample;

static int compare_samples(const void *a, const void *b)
{
    const NextSample *sa = (const NextSample *)a;
    const NextSample *sb = (const NextSample *)b;
    
    if (sa->dts < sb->dts) return -1;
    if (sa->dts > sb->dts) return 1;
    return 0;
}

static u64 get_sample_dts_ms(GF_ISOFile *file, u32 track_id, u32 sample_index, u32 timescale)
{
    GF_ISOSample *sample = gf_isom_get_sample_info(file, track_id, sample_index, NULL, NULL);
    
    if (!sample)
        return UINT64_MAX;
    
    u64 dts_ms = (sample->DTS * 1000) / timescale;
    gf_isom_sample_del(&sample);
    
    return dts_ms;
}

/* ========== 公開 API ========== */

RealtimeDemuxer* realtime_demux_open(const char *mp4_path)
{
    if (!mp4_path)
        return NULL;
    
    GF_ISOFile *file = gf_isom_open(mp4_path, GF_ISOM_OPEN_READ, NULL);
    if (!file) {
        fprintf(stderr, "Failed to open MP4 file: %s\n", mp4_path);
        return NULL;
    }
    
    RealtimeDemuxer *demuxer = (RealtimeDemuxer *)calloc(1, sizeof(RealtimeDemuxer));
    if (!demuxer) {
        gf_isom_close(file);
        return NULL;
    }
    
    demuxer->file = file;
    
    /* 收集軌道信息 */
    u32 num_tracks = gf_isom_get_track_count(file);
    
    demuxer->track_ids = (u32 *)calloc(num_tracks, sizeof(u32));
    demuxer->track_timescales = (u32 *)calloc(num_tracks, sizeof(u32));
    demuxer->sample_indices = (u32 *)calloc(num_tracks, sizeof(u32));
    demuxer->total_samples = (u32 *)calloc(num_tracks, sizeof(u32));
    
    if (!demuxer->track_ids || !demuxer->track_timescales || 
        !demuxer->sample_indices || !demuxer->total_samples) {
        realtime_demux_close(demuxer);
        return NULL;
    }
    
    demuxer->num_tracks = num_tracks;
    
    /* 初始化各軌道 */
    for (u32 i = 0; i < num_tracks; i++) {
        u32 track_id = gf_isom_get_track_id(file, i + 1);  /* i+1: 1-based */
        u32 sample_count = gf_isom_get_sample_count(file, track_id);
        u32 timescale = gf_isom_get_media_timescale(file, track_id);
        
        demuxer->track_ids[i] = track_id;
        demuxer->track_timescales[i] = timescale;
        demuxer->total_samples[i] = sample_count;
        demuxer->sample_indices[i] = 0;  /* 從第 0 個樣本開始 */
        
        fprintf(stderr, "[DEMUX] Track ID=%u, Samples=%u, Timescale=%u, Type=%s\n",
                track_id, sample_count, timescale,
                realtime_demux_get_track_type(demuxer, track_id));
    }
    
    return demuxer;
}

GF_Err realtime_demux_read_sample(RealtimeDemuxer *demuxer, RealtimeDemuxSample *sample)
{
    if (!demuxer || !sample)
        return GF_BAD_PARAM;
    
    /* 查找下一個最早的樣本 */
    NextSample *candidates = (NextSample *)calloc(demuxer->num_tracks, sizeof(NextSample));
    if (!candidates)
        return GF_OUT_OF_MEM;
    
    u32 valid_count = 0;
    
    for (u32 i = 0; i < demuxer->num_tracks; i++) {
        u32 idx = demuxer->sample_indices[i];
        u32 total = demuxer->total_samples[i];
        
        if (idx < total) {
            u32 track_id = demuxer->track_ids[i];
            u32 timescale = demuxer->track_timescales[i];
            
            u64 dts_ms = get_sample_dts_ms(demuxer->file, track_id, idx + 1, timescale);  /* +1: 1-based */
            
            candidates[valid_count].track_idx = i;
            candidates[valid_count].sample_idx = idx;
            candidates[valid_count].dts = dts_ms;
            
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        free(candidates);
        return GF_EOS;  /* 已讀完所有樣本 */
    }
    
    /* 排序找最早的 */
    qsort(candidates, valid_count, sizeof(NextSample), compare_samples);
    NextSample next = candidates[0];
    free(candidates);
    
    u32 track_idx = next.track_idx;
    u32 sample_idx = next.sample_idx;
    u32 track_id = demuxer->track_ids[track_idx];
    u32 timescale = demuxer->track_timescales[track_idx];
    
    /* 讀取樣本 */
    GF_ISOSample *iso_sample = gf_isom_get_sample(demuxer->file, track_id, sample_idx + 1, NULL);  /* +1: 1-based */
    
    if (!iso_sample)
        return GF_IO_ERR;
    
    /* 複製到輸出結構 */
    sample->track_id = track_id;
    sample->data = (u8 *)malloc(iso_sample->dataLength);
    if (!sample->data) {
        gf_isom_sample_del(&iso_sample);
        return GF_OUT_OF_MEM;
    }
    
    memcpy(sample->data, iso_sample->data, iso_sample->dataLength);
    sample->data_size = iso_sample->dataLength;
    sample->dts = (iso_sample->DTS * 1000) / timescale;
    sample->pts = (iso_sample->IsRAP ? iso_sample->DTS : iso_sample->DTS) * 1000 / timescale;  /* 簡單轉換 */
    sample->timescale = timescale;
    sample->is_sync = (iso_sample->IsRAP != 0);  /* 轉換為 Bool */
    sample->sample_index = demuxer->total_samples_read++;
    
    gf_isom_sample_del(&iso_sample);
    
    /* 更新索引 */
    demuxer->sample_indices[track_idx]++;
    
    return GF_OK;
}

u32 realtime_demux_get_track_sample_count(RealtimeDemuxer *demuxer, u32 track_id)
{
    if (!demuxer)
        return 0;
    
    for (u32 i = 0; i < demuxer->num_tracks; i++) {
        if (demuxer->track_ids[i] == track_id)
            return demuxer->total_samples[i];
    }
    
    return 0;
}

const char* realtime_demux_get_track_type(RealtimeDemuxer *demuxer, u32 track_id)
{
    if (!demuxer)
        return "unknown";
    
    TrackType type = get_track_type(demuxer->file, track_id);
    
    switch (type) {
        case TRACK_VIDEO:    return "video";
        case TRACK_AUDIO:    return "audio";
        case TRACK_METADATA: return "metadata";
        default:             return "unknown";
    }
}

GF_Err realtime_demux_get_video_size(RealtimeDemuxer *demuxer, u32 *width, u32 *height)
{
    if (!demuxer || !width || !height)
        return GF_BAD_PARAM;
    
    /* 找視頻軌道 */
    for (u32 i = 0; i < demuxer->num_tracks; i++) {
        u32 track_id = demuxer->track_ids[i];
        if (get_track_type(demuxer->file, track_id) == TRACK_VIDEO) {
            /* 使用 gf_isom_get_visual_info，需要 sample description index (通常為 1) */
            return gf_isom_get_visual_info(demuxer->file, track_id, 1, width, height);
        }
    }
    
    return GF_BAD_PARAM;  /* 沒有視頻軌道 */
}

GF_Err realtime_demux_get_audio_info(RealtimeDemuxer *demuxer, u32 *sample_rate, u32 *channels)
{
    if (!demuxer || !sample_rate || !channels)
        return GF_BAD_PARAM;
    
    /* 找音頻軌道 */
    for (u32 i = 0; i < demuxer->num_tracks; i++) {
        u32 track_id = demuxer->track_ids[i];
        if (get_track_type(demuxer->file, track_id) == TRACK_AUDIO) {
            /* 使用 gf_isom_get_audio_info，需要 sample description index (通常為 1) */
            u32 bits_per_sample;
            return gf_isom_get_audio_info(demuxer->file, track_id, 1, sample_rate, channels, &bits_per_sample);
        }
    }
    
    return GF_BAD_PARAM;  /* 沒有音頻軌道 */
}

GF_Err realtime_demux_close(RealtimeDemuxer *demuxer)
{
    if (!demuxer)
        return GF_OK;
    
    if (demuxer->file)
        gf_isom_close(demuxer->file);
    
    free(demuxer->track_ids);
    free(demuxer->track_timescales);
    free(demuxer->sample_indices);
    free(demuxer->total_samples);
    free(demuxer);
    
    return GF_OK;
}

void realtime_demux_print_stats(RealtimeDemuxer *demuxer)
{
    if (!demuxer)
        return;
    
    printf("\n=== DEMUX Statistics ===\n");
    printf("Total samples read: %u\n", demuxer->total_samples_read);
    printf("Number of tracks: %u\n", demuxer->num_tracks);
    
    for (u32 i = 0; i < demuxer->num_tracks; i++) {
        u32 track_id = demuxer->track_ids[i];
        u32 sample_count = demuxer->total_samples[i];
        u32 timescale = demuxer->track_timescales[i];
        const char *type = realtime_demux_get_track_type(demuxer, track_id);
        
        u64 duration_ms = get_track_duration_ms(demuxer->file, track_id);
        
        printf("  Track %u (%s): %u samples, timescale=%u, duration=%.2f s\n",
               track_id, type, sample_count, timescale, duration_ms / 1000.0);
    }
    printf("========================\n\n");
}
