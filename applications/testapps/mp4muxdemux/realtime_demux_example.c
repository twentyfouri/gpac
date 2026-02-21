/*
 * Realtime MP4 Demux Example
 * 
 * 演示如何讀取 fMP4，將 Video/Audio/Metadata 分開交付
 */

#include "realtime_demux.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *mp4_file = "/tmp/realtime_output.mp4";
    
    if (argc > 1)
        mp4_file = argv[1];
    
    printf("[DEMUX] Opening: %s\n", mp4_file);
    
    RealtimeDemuxer *demuxer = realtime_demux_open(mp4_file);
    if (!demuxer) {
        fprintf(stderr, "Failed to open demuxer\n");
        return 1;
    }
    
    /* 打印軌道信息 */
    printf("\n=== Track Information ===\n");
    
    u32 video_count = realtime_demux_get_track_sample_count(demuxer, 1);
    u32 audio_count = realtime_demux_get_track_sample_count(demuxer, 2);
    u32 metadata_count = realtime_demux_get_track_sample_count(demuxer, 3);
    
    printf("Video track (ID=1):    %u samples\n", video_count);
    printf("Audio track (ID=2):    %u samples\n", audio_count);
    printf("Metadata track (ID=3): %u samples\n", metadata_count);
    
    /* 視頻尺寸 */
    u32 width, height;
    if (realtime_demux_get_video_size(demuxer, &width, &height) == GF_OK) {
        printf("Resolution: %u x %u\n", width, height);
    }
    
    /* 音頻信息 */
    u32 sample_rate, channels;
    if (realtime_demux_get_audio_info(demuxer, &sample_rate, &channels) == GF_OK) {
        printf("Audio: %u Hz, %u channels\n", sample_rate, channels);
    }
    
    /* ========== 讀取所有樣本 ========== */
    printf("\n=== Reading Samples (按時間戳順序) ===\n");
    printf("%-8s | %-8s | %-12s | %-8s | Track Type\n",
           "Sample", "Track ID", "DTS (ms)", "Size (bytes)");
    printf("---------+-----------+-------------+----------+------\n");
    
    u32 video_count_read = 0, audio_count_read = 0, metadata_count_read = 0;
    RealtimeDemuxSample sample;
    
    while (realtime_demux_read_sample(demuxer, &sample) == GF_OK) {
        const char *type = realtime_demux_get_track_type(demuxer, sample.track_id);
        
        printf("%-8u | %-8u | %-12llu | %-8u | %s\n",
               sample.sample_index,
               sample.track_id,
               sample.dts,
               sample.data_size,
               type);
        
        /* 追蹤各軌道的樣本計數 */
        if (sample.track_id == 1) video_count_read++;
        else if (sample.track_id == 2) audio_count_read++;
        else if (sample.track_id == 3) metadata_count_read++;
        
        /* 示例：提取第一個視頻樣本的資訊 */
        if (sample.track_id == 1 && video_count_read == 1) {
            printf("  [INFO] First video sample: %u bytes, DTS=%.2f s\n",
                   sample.data_size, sample.dts / 1000.0);
        }
        
        /* 示例：檢查是否為關鍵幀 */
        if (sample.is_sync && sample.track_id == 1) {
            printf("  [SYNC] Video keyframe at DTS=%.2f s\n", sample.dts / 1000.0);
        }
        
        /* 示例：音頻樣本大小檢查 */
        if (sample.track_id == 2 && audio_count_read == 1) {
            printf("  [INFO] First audio sample: %u bytes (expected ~256 for AAC frame)\n",
                   sample.data_size);
        }
        
        /* 示例：元數據內容 (JSON) */
        if (sample.track_id == 3 && metadata_count_read <= 3) {
            printf("  [METADATA] Sample %u: ", metadata_count_read);
            /* 打印前 60 字元 */
            for (u32 i = 0; i < (sample.data_size < 60 ? sample.data_size : 60); i++) {
                printf("%c", (char)sample.data[i]);
            }
            if (sample.data_size > 60)
                printf("...");
            printf("\n");
        }
        
        /* 釋放樣本數據 */
        free(sample.data);
    }
    
    printf("\n=== Summary ===\n");
    printf("Video samples read:    %u\n", video_count_read);
    printf("Audio samples read:    %u\n", audio_count_read);
    printf("Metadata samples read: %u\n", metadata_count_read);
    
    /* 統計信息 */
    realtime_demux_print_stats(demuxer);
    
    /* 清理 */
    realtime_demux_close(demuxer);
    
    printf("[DEMUX] Done.\n");
    return 0;
}
