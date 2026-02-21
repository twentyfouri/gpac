#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "realtime_mp4.h"
#include <gpac/isomedia.h>

GF_Err gf_isom_close_fragments(GF_ISOFile *movie);

u64 realtime_mp4_get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (u64)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static u64 timestamp_to_dts(u64 timestamp_us, u32 timescale)
{
    return (timestamp_us * (u64)timescale) / 1000000ULL;
}

u32 realtime_mp4_timestamp_to_dts(u64 timestamp_us, u32 timescale)
{
    return (u32)timestamp_to_dts(timestamp_us, timescale);
}

static void realtime_mp4_cleanup(RealtimeMP4Writer *writer)
{
    if (!writer) {
        return;
    }
    if (writer->file) {
        gf_isom_close(writer->file);
    }
    if (writer->output_path) {
        free(writer->output_path);
    }
    free(writer);
}

RealtimeMP4Writer *realtime_mp4_open(const char *mp4_path, u32 width, u32 height, u32 fps,
                                     u32 audio_sample_rate, u32 audio_channels)
{
    if (!mp4_path || !width || !height || !fps || !audio_sample_rate || !audio_channels) {
        return NULL;
    }

    RealtimeMP4Writer *writer = (RealtimeMP4Writer *)malloc(sizeof(RealtimeMP4Writer));
    if (!writer) {
        return NULL;
    }
    memset(writer, 0, sizeof(RealtimeMP4Writer));

    writer->output_path = strdup(mp4_path);
    if (!writer->output_path) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->file = gf_isom_open(mp4_path, GF_ISOM_OPEN_WRITE, NULL);
    if (!writer->file) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    GF_Err e = gf_isom_set_brand_info(writer->file, GF_ISOM_BRAND_MP42, 0);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->video_fps = fps;
    writer->video_timescale = 90000;
    writer->audio_sample_rate = audio_sample_rate;
    writer->audio_timescale = audio_sample_rate;
    writer->metadata_timescale = fps;

    writer->video_track = gf_isom_new_track(writer->file, 0, GF_ISOM_MEDIA_VISUAL, writer->video_timescale);
    if (!writer->video_track) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->audio_track = gf_isom_new_track(writer->file, 0, GF_ISOM_MEDIA_AUDIO, writer->audio_timescale);
    if (!writer->audio_track) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->metadata_track = gf_isom_new_track(writer->file, 0, GF_ISOM_MEDIA_META, writer->metadata_timescale);
    if (!writer->metadata_track) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->video_track_id = gf_isom_get_track_id(writer->file, writer->video_track);
    writer->audio_track_id = gf_isom_get_track_id(writer->file, writer->audio_track);
    writer->metadata_track_id = gf_isom_get_track_id(writer->file, writer->metadata_track);

    GF_GenericSampleDescription video_desc;
    memset(&video_desc, 0, sizeof(video_desc));
    video_desc.codec_tag = GF_ISOM_SUBTYPE_AVC_H264;
    video_desc.width = (u16)width;
    video_desc.height = (u16)height;

    e = gf_isom_new_generic_sample_description(writer->file, writer->video_track, NULL, NULL,
                                               &video_desc, &writer->video_desc_idx);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    GF_GenericSampleDescription audio_desc;
    memset(&audio_desc, 0, sizeof(audio_desc));
    audio_desc.codec_tag = GF_ISOM_SUBTYPE_MP4A;
    audio_desc.samplerate = audio_sample_rate;
    audio_desc.nb_channels = (u16)audio_channels;
    audio_desc.bits_per_sample = 16;

    e = gf_isom_new_generic_sample_description(writer->file, writer->audio_track, NULL, NULL,
                                               &audio_desc, &writer->audio_desc_idx);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_set_audio_info(writer->file, writer->audio_track, writer->audio_desc_idx,
                               audio_sample_rate, audio_channels, 16, GF_IMPORT_AUDIO_SAMPLE_ENTRY_v0_BS);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_new_stxt_description(writer->file, writer->metadata_track,
                                     GF_ISOM_SUBTYPE_METT, "application/json", "utf-8", NULL,
                                     &writer->metadata_desc_idx);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    u32 video_duration = writer->video_timescale / writer->video_fps;
    u32 audio_duration = 1024;
    u32 metadata_duration = 1;

    e = gf_isom_setup_track_fragment(writer->file, writer->video_track_id, writer->video_desc_idx,
                                     video_duration, 0, 0, 0, 0, GF_FALSE);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_setup_track_fragment(writer->file, writer->audio_track_id, writer->audio_desc_idx,
                                     audio_duration, 0, GF_ISOM_FRAG_DEF_IS_SYNC, 0, 0, GF_FALSE);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_setup_track_fragment(writer->file, writer->metadata_track_id, writer->metadata_desc_idx,
                                     metadata_duration, 0, GF_ISOM_FRAG_DEF_IS_SYNC, 0, 0, GF_FALSE);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_finalize_for_fragment(writer->file, 0, GF_TRUE);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_start_segment(writer->file, NULL, GF_FALSE);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    e = gf_isom_start_fragment(writer->file, GF_ISOM_FRAG_MOOF_FIRST);
    if (e < GF_OK) {
        realtime_mp4_cleanup(writer);
        return NULL;
    }

    writer->start_time_us = realtime_mp4_get_time_us();
    writer->last_video_timestamp = writer->start_time_us;
    writer->last_audio_timestamp = writer->start_time_us;
    writer->last_metadata_timestamp = writer->start_time_us;
    writer->fragment_counter = 0;
    writer->fragment_duration_us = 1000000;
    writer->next_fragment_time_us = writer->start_time_us + writer->fragment_duration_us;
    
    /* 初始化 DTS 追蹤 */
    writer->next_video_dts = 0;
    writer->next_audio_dts = 0;
    writer->next_metadata_dts = 0;

    return writer;
}

GF_Err realtime_mp4_add_video_frame(RealtimeMP4Writer *writer, const u8 *h264_data, u32 data_size, u64 timestamp_us)
{
    if (!writer || !writer->file || !h264_data || !data_size) {
        return GF_BAD_PARAM;
    }

    GF_ISOSample *sample = gf_isom_sample_new();
    if (!sample) {
        return GF_OUT_OF_MEM;
    }

    sample->data = (u8 *)malloc(data_size);
    if (!sample->data) {
        gf_isom_sample_del(&sample);
        return GF_OUT_OF_MEM;
    }

    memcpy(sample->data, h264_data, data_size);
    sample->dataLength = data_size;
    sample->DTS = timestamp_to_dts(timestamp_us - writer->start_time_us, writer->video_timescale);
    sample->CTS_Offset = 0;
    sample->IsRAP = (writer->video_sample_count == 0) ? RAP : 0;

    u32 duration = writer->video_timescale / writer->video_fps;
    u64 sample_dts = sample->DTS;  /* 保存 DTS，因為 sample 會被釋放 */
    
    GF_Err e = gf_isom_fragment_add_sample(writer->file, writer->video_track_id, sample,
                                           writer->video_desc_idx, duration, 0, 0, GF_FALSE);

    sample->dataLength = 0;
    gf_isom_sample_del(&sample);

    if (e >= GF_OK) {
        writer->video_sample_count++;
        writer->last_video_timestamp = timestamp_us;
        /* 更新下一個 DTS（用於設置下一個片段的 tfdt） */
        writer->next_video_dts = sample_dts + duration;
    }

    return e;
}

GF_Err realtime_mp4_add_audio_samples(RealtimeMP4Writer *writer, const u8 *aac_data, u32 data_size, u64 timestamp_us)
{
    if (!writer || !writer->file || !aac_data || !data_size) {
        return GF_BAD_PARAM;
    }

    GF_ISOSample *sample = gf_isom_sample_new();
    if (!sample) {
        return GF_OUT_OF_MEM;
    }

    sample->data = (u8 *)malloc(data_size);
    if (!sample->data) {
        gf_isom_sample_del(&sample);
        return GF_OUT_OF_MEM;
    }

    memcpy(sample->data, aac_data, data_size);
    sample->dataLength = data_size;
    sample->DTS = timestamp_to_dts(timestamp_us - writer->start_time_us, writer->audio_timescale);
    sample->CTS_Offset = 0;
    sample->IsRAP = RAP;

    u32 duration = 1024;
    u64 sample_dts = sample->DTS;  /* 保存 DTS */
    
    GF_Err e = gf_isom_fragment_add_sample(writer->file, writer->audio_track_id, sample,
                                           writer->audio_desc_idx, duration, 0, 0, GF_FALSE);

    sample->dataLength = 0;
    gf_isom_sample_del(&sample);

    if (e >= GF_OK) {
        writer->audio_sample_count++;
        writer->last_audio_timestamp = timestamp_us;
        /* 更新下一個 DTS */
        writer->next_audio_dts = sample_dts + duration;
    }

    return e;
}

GF_Err realtime_mp4_add_metadata(RealtimeMP4Writer *writer, const char *json_payload, u64 timestamp_us)
{
    if (!writer || !writer->file || !json_payload) {
        return GF_BAD_PARAM;
    }

    GF_ISOSample *sample = gf_isom_sample_new();
    if (!sample) {
        return GF_OUT_OF_MEM;
    }

    u32 json_len = (u32)strlen(json_payload);
    sample->data = (u8 *)malloc(json_len);
    if (!sample->data) {
        gf_isom_sample_del(&sample);
        return GF_OUT_OF_MEM;
    }

    memcpy(sample->data, json_payload, json_len);
    sample->dataLength = json_len;
    sample->DTS = timestamp_to_dts(timestamp_us - writer->start_time_us, writer->metadata_timescale);
    sample->CTS_Offset = 0;
    sample->IsRAP = RAP;

    u32 duration = 1;
    u64 sample_dts = sample->DTS;  /* 保存 DTS */
    
    GF_Err e = gf_isom_fragment_add_sample(writer->file, writer->metadata_track_id, sample,
                                           writer->metadata_desc_idx, duration, 0, 0, GF_FALSE);

    sample->dataLength = 0;
    gf_isom_sample_del(&sample);

    if (e >= GF_OK) {
        writer->metadata_sample_count++;
        writer->last_metadata_timestamp = timestamp_us;
        /* 更新下一個 DTS */
        writer->next_metadata_dts = sample_dts + duration;
    }

    return e;
}

GF_Err realtime_mp4_flush_fragment(RealtimeMP4Writer *writer)
{
    if (!writer || !writer->file) {
        return GF_BAD_PARAM;
    }

    /* 開始新片段 */
    GF_Err e = gf_isom_start_fragment(writer->file, GF_ISOM_FRAG_MOOF_FIRST);
    if (e < GF_OK) {
        return e;
    }
    
    /* 為每個軌道設置 base media decode time（tfdt box）*/
    if (writer->video_sample_count > 0) {
        e = gf_isom_set_traf_base_media_decode_time(writer->file, writer->video_track_id, writer->next_video_dts);
        if (e < GF_OK) {
            return e;
        }
    }
    
    if (writer->audio_sample_count > 0) {
        e = gf_isom_set_traf_base_media_decode_time(writer->file, writer->audio_track_id, writer->next_audio_dts);
        if (e < GF_OK) {
            return e;
        }
    }
    
    if (writer->metadata_sample_count > 0) {
        e = gf_isom_set_traf_base_media_decode_time(writer->file, writer->metadata_track_id, writer->next_metadata_dts);
        if (e < GF_OK) {
            return e;
        }
    }
    
    writer->fragment_counter++;
    writer->next_fragment_time_us = realtime_mp4_get_time_us() + writer->fragment_duration_us;
    
    return GF_OK;
}

u64 realtime_mp4_get_duration_ms(RealtimeMP4Writer *writer)
{
    if (!writer) {
        return 0;
    }

    return (realtime_mp4_get_time_us() - writer->start_time_us) / 1000ULL;
}

void realtime_mp4_get_stats(RealtimeMP4Writer *writer, u32 *video_samples, u32 *audio_samples, u32 *metadata_samples)
{
    if (!writer) {
        return;
    }

    if (video_samples) {
        *video_samples = writer->video_sample_count;
    }
    if (audio_samples) {
        *audio_samples = writer->audio_sample_count;
    }
    if (metadata_samples) {
        *metadata_samples = writer->metadata_sample_count;
    }
}

GF_Err realtime_mp4_close(RealtimeMP4Writer *writer)
{
    if (!writer) {
        return GF_BAD_PARAM;
    }

    if (writer->file) {
        /* 設置軌道為 enabled（在 fragmented MP4 中軌道默認是 disabled） */
        gf_isom_set_track_enabled(writer->file, writer->video_track, GF_TRUE);
        gf_isom_set_track_enabled(writer->file, writer->audio_track, GF_TRUE);
        gf_isom_set_track_enabled(writer->file, writer->metadata_track, GF_TRUE);
        
        /* 關閉 fragments 並寫入最後的數據 */
        gf_isom_close_fragments(writer->file);
        
        /* 關閉文件 */
        gf_isom_close(writer->file);
        writer->file = NULL;
    }

    if (writer->output_path) {
        free(writer->output_path);
        writer->output_path = NULL;
    }

    free(writer);
    return GF_OK;
}
