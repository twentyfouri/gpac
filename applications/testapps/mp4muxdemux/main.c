#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>

#include <gpac/filters.h>
#include <gpac/tools.h>
#include <gpac/isomedia.h>

static void print_usage(const char *exe)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s mux <in.h264> <in.aac> <out.mp4> <fps>\n"
        "  %s demux <in.mp4> <out.h264> <out.aac>\n"
        "  %s demux_frames <in.mp4> <out_frames_dir>\n"
        "  %s add_metadata_track <in.mp4> <metadata.ndjson> <out.mp4>\n"
        "  %s extract_metadata_track <in.mp4> <out.ndjson>\n"
        "  %s make30\n"
        "\n"
        "Notes:\n"
        "  - H.264 input must be Annex B (start codes).\n"
        "  - AAC input must be ADTS.\n"
        "  - fps can be like 30 or 30000/1001.\n"
        "  - demux_frames: extract each H.264 frame to separate file in folder.\n"
        "  - metadata.ndjson: one JSON object per line with frame_id.\n"
        "  - metadata track uses timescale 30 and METT (application/json).\n",
    exe, exe, exe, exe, exe, exe);
}

static GF_Err run_session(GF_FilterSession *fs)
{
    GF_Err e = gf_fs_run(fs);
    if (e == GF_EOS) {
        return GF_OK;
    }
    if (e >= GF_OK) {
        GF_Err ce = gf_fs_get_last_connect_error(fs);
        if (ce < GF_OK) {
            return ce;
        }
        GF_Err pe = gf_fs_get_last_process_error(fs);
        if (pe < GF_OK) {
            return pe;
        }
        return e;
    }
    return e;
}

static GF_Err build_mux(const char *h264_path, const char *aac_path, const char *out_mp4, const char *fps_str)
{
    GF_Err e = GF_OK;
    GF_FilterSession *fs = gf_fs_new_defaults(0);
    if (!fs) {
        return GF_OUT_OF_MEM;
    }

    char v_in_args[GF_MAX_PATH + 64];
    char a_in_args[GF_MAX_PATH + 64];
    char v_rf_args[128];

    snprintf(v_in_args, sizeof(v_in_args), "fin:src=%s:ext=h264", h264_path);
    snprintf(a_in_args, sizeof(a_in_args), "fin:src=%s:ext=aac", aac_path);
    snprintf(v_rf_args, sizeof(v_rf_args), "rfnalu:fps=%s", fps_str);

    GF_Filter *v_in = gf_fs_load_filter(fs, v_in_args, &e);
    if (!v_in) {
        gf_fs_del(fs);
        return e;
    }
    GF_Filter *a_in = gf_fs_load_filter(fs, a_in_args, &e);
    if (!a_in) {
        gf_fs_del(fs);
        return e;
    }

    GF_Filter *v_rf = gf_fs_load_filter(fs, v_rf_args, &e);
    if (!v_rf) {
        gf_fs_del(fs);
        return e;
    }
    GF_Filter *a_rf = gf_fs_load_filter(fs, "rfadts", &e);
    if (!a_rf) {
        gf_fs_del(fs);
        return e;
    }

    gf_filter_set_source(v_rf, v_in, NULL);
    gf_filter_set_source(a_rf, a_in, NULL);

    GF_Filter *mux = gf_fs_load_filter(fs, "mp4mx", &e);
    if (!mux) {
        gf_fs_del(fs);
        return e;
    }

    gf_filter_set_source(mux, v_rf, NULL);
    gf_filter_set_source(mux, a_rf, NULL);

    GF_Filter *dst = gf_fs_load_destination(fs, out_mp4, NULL, NULL, &e);
    if (!dst) {
        gf_fs_del(fs);
        return e;
    }
    gf_filter_set_source(dst, mux, NULL);

    e = run_session(fs);
    gf_fs_del(fs);
    return e;
}

static GF_Err build_demux_one(const char *in_mp4, const char *tkid, const char *out_path, const char *unframe_filter)
{
    GF_Err e = GF_OK;
    GF_FilterSession *fs = gf_fs_new_defaults(0);
    if (!fs) {
        return GF_OUT_OF_MEM;
    }

    char in_args[GF_MAX_PATH + 128];
    char dmx_args[128];

    snprintf(in_args, sizeof(in_args), "fin:src=%s:ext=mp4", in_mp4);
    snprintf(dmx_args, sizeof(dmx_args), "mp4dmx:tkid=%s", tkid);

    GF_Filter *fin = gf_fs_load_filter(fs, in_args, &e);
    if (!fin) {
        gf_fs_del(fs);
        return e;
    }
    GF_Filter *dmx = gf_fs_load_filter(fs, dmx_args, &e);
    if (!dmx) {
        gf_fs_del(fs);
        return e;
    }
    gf_filter_set_source(dmx, fin, NULL);

    GF_Filter *uf = gf_fs_load_filter(fs, unframe_filter, &e);
    if (!uf) {
        gf_fs_del(fs);
        return e;
    }
    gf_filter_set_source(uf, dmx, NULL);

    GF_Filter *dst = gf_fs_load_destination(fs, out_path, NULL, NULL, &e);
    if (!dst) {
        gf_fs_del(fs);
        return e;
    }
    gf_filter_set_source(dst, uf, NULL);

    e = run_session(fs);
    gf_fs_del(fs);
    return e;
}

static GF_Err build_demux(const char *in_mp4, const char *out_h264, const char *out_aac)
{
    GF_Err e = build_demux_one(in_mp4, "video", out_h264, "ufnalu");
    if (e < GF_OK) {
        return e;
    }
    return build_demux_one(in_mp4, "audio", out_aac, "ufadts");
}

typedef struct
{
    u32 frame_id;
    char *json;
} FrameJson;

typedef struct
{
    FrameJson *items;
    u32 count;
    u32 cap;
} FrameJsonList;

static void frame_json_list_free(FrameJsonList *list)
{
    if (!list || !list->items) {
        return;
    }
    for (u32 i = 0; i < list->count; i++) {
        free(list->items[i].json);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static GF_Err frame_json_list_add(FrameJsonList *list, u32 frame_id, char *json)
{
    if (list->count == list->cap) {
        u32 new_cap = list->cap ? list->cap * 2 : 64;
        FrameJson *new_items = (FrameJson *)realloc(list->items, new_cap * sizeof(FrameJson));
        if (!new_items) {
            return GF_OUT_OF_MEM;
        }
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->count].frame_id = frame_id;
    list->items[list->count].json = json;
    list->count++;
    return GF_OK;
}

static char *trim_line(char *line)
{
    if (!line) {
        return line;
    }
    while (isspace((unsigned char)*line)) {
        line++;
    }
    if (!*line) {
        return line;
    }
    char *end = line + strlen(line) - 1;
    while (end > line && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return line;
}

static Bool parse_frame_id(const char *line, u32 *out_frame_id)
{
    const char *p = strstr(line, "\"frame_id\"");
    if (!p) {
        p = strstr(line, "frame_id");
    }
    if (!p) {
        return GF_FALSE;
    }
    p = strchr(p, ':');
    if (!p) {
        return GF_FALSE;
    }
    p++;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p || v < 0) {
        return GF_FALSE;
    }
    *out_frame_id = (u32)v;
    return GF_TRUE;
}

static GF_Err load_frame_json_lines(const char *json_path, char ***out_frames, u32 *out_frame_count, u32 *out_max_frame)
{
    FILE *f = fopen(json_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open metadata file: %s\n", json_path);
        return GF_URL_ERROR;
    }

    FrameJsonList list = {0};
    char *line = NULL;
    size_t cap = 0;
    ssize_t len = 0;
    u32 max_frame = 0;

    while ((len = getline(&line, &cap, f)) != -1) {
        char *trimmed = trim_line(line);
        if (!trimmed[0]) {
            continue;
        }
        u32 frame_id = 0;
        if (!parse_frame_id(trimmed, &frame_id)) {
            fprintf(stderr, "Invalid metadata line (missing frame_id): %s\n", trimmed);
            free(line);
            fclose(f);
            frame_json_list_free(&list);
            return GF_BAD_PARAM;
        }
        char *json_copy = strdup(trimmed);
        if (!json_copy) {
            free(line);
            fclose(f);
            frame_json_list_free(&list);
            return GF_OUT_OF_MEM;
        }
        GF_Err e = frame_json_list_add(&list, frame_id, json_copy);
        if (e < GF_OK) {
            free(line);
            fclose(f);
            frame_json_list_free(&list);
            return e;
        }
        if (frame_id > max_frame) {
            max_frame = frame_id;
        }
    }

    free(line);
    fclose(f);

    if (!list.count) {
        frame_json_list_free(&list);
        fprintf(stderr, "No metadata entries found in %s\n", json_path);
        return GF_BAD_PARAM;
    }

    u32 frame_count = max_frame + 1;
    char **frames = (char **)calloc(frame_count, sizeof(char *));
    if (!frames) {
        frame_json_list_free(&list);
        return GF_OUT_OF_MEM;
    }

    for (u32 i = 0; i < list.count; i++) {
        u32 fid = list.items[i].frame_id;
        if (fid < frame_count) {
            if (frames[fid]) {
                free(frames[fid]);
            }
            frames[fid] = list.items[i].json;
            list.items[i].json = NULL;
        }
    }

    frame_json_list_free(&list);
    *out_frames = frames;
    *out_frame_count = frame_count;
    *out_max_frame = max_frame;
    return GF_OK;
}

static u32 find_json_metadata_track(GF_ISOFile *file)
{
    u32 track_count = gf_isom_get_track_count(file);
    u32 fallback_track = 0;
    for (u32 i = 1; i <= track_count; i++) {
        u32 media_type = gf_isom_get_media_type(file, i);
        if (media_type != GF_ISOM_MEDIA_META) {
            continue;
        }
        u32 subtype = gf_isom_get_media_subtype(file, i, 1);
        if (subtype != GF_ISOM_SUBTYPE_METT) {
            continue;
        }
        const char *mime = NULL;
        gf_isom_stxt_get_description(file, i, 1, &mime, NULL, NULL);
        if (mime && strstr(mime, "application/json")) {
            return i;
        }
        if (!fallback_track) {
            fallback_track = i;
        }
    }
    return fallback_track;
}

static GF_Err add_metadata_track(const char *in_mp4, const char *json_path, const char *out_mp4)
{
    GF_ISOFile *file = gf_isom_open(in_mp4, GF_ISOM_OPEN_EDIT, NULL);
    if (!file) {
        fprintf(stderr, "Failed to open input MP4: %s\n", in_mp4);
        return GF_URL_ERROR;
    }
    GF_Err e = gf_isom_set_final_name(file, (char *)out_mp4);
    if (e < GF_OK) {
        gf_isom_close(file);
        return e;
    }

    u32 track = gf_isom_new_track(file, 0, GF_ISOM_MEDIA_META, 30);
    if (!track) {
        gf_isom_close(file);
        return GF_IO_ERR;
    }

    u32 desc_index = 0;
    e = gf_isom_new_stxt_description(file, track, GF_ISOM_SUBTYPE_METT, "application/json", "utf-8", NULL, &desc_index);
    if (e < GF_OK) {
        gf_isom_close(file);
        return e;
    }

    char **frames = NULL;
    u32 frame_count = 0;
    u32 max_frame = 0;
    e = load_frame_json_lines(json_path, &frames, &frame_count, &max_frame);
    if (e < GF_OK) {
        gf_isom_close(file);
        return e;
    }

    GF_ISOSample *sample = gf_isom_sample_new();
    if (!sample) {
        for (u32 i = 0; i < frame_count; i++) {
            free(frames[i]);
        }
        free(frames);
        gf_isom_close(file);
        return GF_OUT_OF_MEM;
    }

    for (u32 i = 0; i < frame_count; i++) {
        char fallback[128];
        const char *payload = frames[i];
        if (!payload) {
            snprintf(fallback, sizeof(fallback), "{\"frame_id\":%u,\"objects\":[]}", i);
            payload = fallback;
        }
        sample->DTS = i;
        sample->CTS_Offset = 0;
        sample->IsRAP = RAP;
        sample->data = (u8 *)payload;
        sample->dataLength = (u32)strlen(payload);
        sample->nb_pack = 0;

        e = gf_isom_add_sample(file, track, desc_index, sample);
        if (e < GF_OK) {
            break;
        }
    }

    sample->dataLength = 0;
    gf_isom_sample_del(&sample);
    for (u32 i = 0; i < frame_count; i++) {
        free(frames[i]);
    }
    free(frames);

    if (e < GF_OK) {
        gf_isom_close(file);
        return e;
    }
    return gf_isom_close(file);
}

static GF_Err extract_metadata_track(const char *in_mp4, const char *out_json)
{
    GF_ISOFile *file = gf_isom_open(in_mp4, GF_ISOM_OPEN_READ, NULL);
    if (!file) {
        fprintf(stderr, "Failed to open input MP4: %s\n", in_mp4);
        return GF_URL_ERROR;
    }

    u32 track = find_json_metadata_track(file);
    if (!track) {
        gf_isom_close(file);
        fprintf(stderr, "No JSON metadata track found in %s\n", in_mp4);
        return GF_BAD_PARAM;
    }

    FILE *out = fopen(out_json, "wb");
    if (!out) {
        gf_isom_close(file);
        fprintf(stderr, "Failed to write output file: %s\n", out_json);
        return GF_IO_ERR;
    }

    u32 sample_count = gf_isom_get_sample_count(file, track);
    for (u32 i = 1; i <= sample_count; i++) {
        u32 desc_index = 0;
        GF_ISOSample *sample = gf_isom_get_sample(file, track, i, &desc_index);
        if (!sample) {
            fclose(out);
            gf_isom_close(file);
            return GF_IO_ERR;
        }
        u32 frame_id = (u32)sample->DTS;
        if (sample->dataLength) {
            fwrite(sample->data, 1, sample->dataLength, out);
            fwrite("\n", 1, 1, out);
        } else {
            fprintf(out, "{\"frame_id\":%u,\"objects\":[]}" "\n", frame_id);
        }
        gf_isom_sample_del(&sample);
    }

    fclose(out);
    gf_isom_close(file);
    return GF_OK;
}

// Split H.264 file into individual frames
static GF_Err split_h264_frames(const char *h264_path, const char *out_dir)
{
    // Create output directory
    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory: %s\n", out_dir);
        return GF_IO_ERR;
    }

    FILE *f = fopen(h264_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open H.264 file: %s\n", h264_path);
        return GF_URL_ERROR;
    }

    u32 frame_count = 0;
    FILE *frame_file = NULL;
    char frame_path[GF_MAX_PATH];
    
    // Read entire file into memory for easier processing
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *buf = (unsigned char *)malloc(file_size);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(f);
        return GF_OUT_OF_MEM;
    }
    
    if (fread(buf, 1, file_size, f) != file_size) {
        fprintf(stderr, "Failed to read H.264 file\n");
        free(buf);
        fclose(f);
        return GF_IO_ERR;
    }
    fclose(f);
    
    // Scan for start codes and extract frames
    unsigned long i = 0;
    
    while (i < file_size) {
        int start_code_len = 0;
        
        // Check for 4-byte start code
        if (i + 4 <= file_size && 
            buf[i] == 0x00 && buf[i+1] == 0x00 && 
            buf[i+2] == 0x00 && buf[i+3] == 0x01) {
            start_code_len = 4;
        }
        // Check for 3-byte start code (but not 4-byte)
        else if (i + 3 <= file_size && 
                 buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x01 &&
                 (i + 3 >= file_size || buf[i+3] != 0x00)) {
            start_code_len = 3;
        }
        
        if (start_code_len > 0) {
            // If already have a frame open, save it
            if (frame_file) {
                fclose(frame_file);
                frame_count++;
            }
            
            // Open new frame file
            snprintf(frame_path, sizeof(frame_path), "%s/frame_%05u.h264", out_dir, frame_count);
            frame_file = fopen(frame_path, "wb");
            if (!frame_file) {
                fprintf(stderr, "Failed to create frame file: %s\n", frame_path);
                free(buf);
                return GF_IO_ERR;
            }
            
            i += start_code_len;
        } else {
            // Write data to current frame
            if (frame_file) {
                if (fwrite(&buf[i], 1, 1, frame_file) != 1) {
                    fprintf(stderr, "Failed to write frame data at offset %lu\n", i);
                    fclose(frame_file);
                    free(buf);
                    return GF_IO_ERR;
                }
            }
            i++;
        }
    }
    
    // Close last frame
    if (frame_file) {
        fclose(frame_file);
        frame_count++;
    }
    
    free(buf);
    
    fprintf(stderr, "Extracted %u frames to %s\n", frame_count, out_dir);
    return GF_OK;
}

static GF_Err build_demux_frames(const char *in_mp4, const char *out_frames_dir)
{
    // First, extract H.264 stream to temporary file
    const char *temp_h264 = "/tmp/temp_demux.h264";
    
    GF_Err e = build_demux_one(in_mp4, "video", temp_h264, "ufnalu");
    if (e < GF_OK) {
        return e;
    }
    
    // Then split into individual frames
    e = split_h264_frames(temp_h264, out_frames_dir);
    
    // Clean up temp file
    unlink(temp_h264);
    
    return e;
}

typedef struct
{
    char **paths;
    u32 count;
    u32 cap;
} FrameList;

static void frame_list_free(FrameList *list)
{
    if (!list) {
        return;
    }
    if (list->paths) {
        u32 i;
        for (i = 0; i < list->count; i++) {
            free(list->paths[i]);
        }
        free(list->paths);
    }
    list->paths = NULL;
    list->count = 0;
    list->cap = 0;
}

static int frame_path_cmp(const void *a, const void *b)
{
    const char *pa = *(const char * const *)a;
    const char *pb = *(const char * const *)b;
    return strcmp(pa, pb);
}

static Bool is_regular_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return GF_FALSE;
    }
    return S_ISREG(st.st_mode) ? GF_TRUE : GF_FALSE;
}

static GF_Err frame_list_add(FrameList *list, const char *path)
{
    if (list->count == list->cap) {
        u32 new_cap = list->cap ? list->cap * 2 : 64;
        char **new_paths = (char **)realloc(list->paths, new_cap * sizeof(char *));
        if (!new_paths) {
            return GF_OUT_OF_MEM;
        }
        list->paths = new_paths;
        list->cap = new_cap;
    }
    list->paths[list->count] = strdup(path);
    if (!list->paths[list->count]) {
        return GF_OUT_OF_MEM;
    }
    list->count++;
    return GF_OK;
}

static Bool has_h264_ext(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) {
        return GF_FALSE;
    }
    return (!strcmp(ext, ".h264") || !strcmp(ext, ".264")) ? GF_TRUE : GF_FALSE;
}

static GF_Err load_frames_from_dir(const char *dir_path, FrameList *list)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return GF_URL_ERROR;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (!has_h264_ext(ent->d_name)) {
            continue;
        }
        char full_path[GF_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        if (!is_regular_file(full_path)) {
            continue;
        }
        GF_Err e = frame_list_add(list, full_path);
        if (e < GF_OK) {
            closedir(dir);
            return e;
        }
    }
    closedir(dir);

    if (!list->count) {
        return GF_NOT_FOUND;
    }
    qsort(list->paths, list->count, sizeof(char *), frame_path_cmp);
    return GF_OK;
}

static GF_Err append_file(FILE *out, const char *path)
{
    FILE *in = fopen(path, "rb");
    if (!in) {
        return GF_URL_ERROR;
    }
    unsigned char buffer[1 << 16];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            return GF_IO_ERR;
        }
    }
    if (ferror(in)) {
        fclose(in);
        return GF_IO_ERR;
    }
    fclose(in);
    return GF_OK;
}

static const u32 aac_sample_rates[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000,
    7350, 0, 0, 0
};

static GF_Err read_adts_frame(FILE *in, u8 **out_buf, u32 *out_len, u32 *out_sr, Bool *sr_set)
{
    int b0;
    int b1;
    u8 hdr[9];
    u32 hdr_size;
    u32 frame_length;
    u32 sf_index;

    while (1) {
        b0 = fgetc(in);
        if (b0 == EOF) {
            return GF_EOS;
        }
        if (b0 != 0xFF) {
            continue;
        }
        b1 = fgetc(in);
        if (b1 == EOF) {
            return GF_EOS;
        }
        if ((b1 & 0xF0) == 0xF0) {
            hdr[0] = (u8) b0;
            hdr[1] = (u8) b1;
            break;
        }
        ungetc(b1, in);
    }

    if (fread(&hdr[2], 1, 5, in) != 5) {
        return GF_IO_ERR;
    }

    hdr_size = (hdr[1] & 0x01) ? 7 : 9;
    if (hdr_size == 9) {
        if (fread(&hdr[7], 1, 2, in) != 2) {
            return GF_IO_ERR;
        }
    }

    frame_length = ((hdr[3] & 0x03) << 11) | (hdr[4] << 3) | ((hdr[5] & 0xE0) >> 5);
    if (frame_length < hdr_size) {
        return GF_NON_COMPLIANT_BITSTREAM;
    }

    if (!*sr_set) {
        sf_index = (hdr[2] >> 2) & 0x0F;
        if (sf_index >= (sizeof(aac_sample_rates) / sizeof(aac_sample_rates[0])) || !aac_sample_rates[sf_index]) {
            return GF_NON_COMPLIANT_BITSTREAM;
        }
        *out_sr = aac_sample_rates[sf_index];
        *sr_set = GF_TRUE;
    }

    *out_buf = (u8 *) malloc(frame_length);
    if (!*out_buf) {
        return GF_OUT_OF_MEM;
    }
    memcpy(*out_buf, hdr, hdr_size);
    if (frame_length > hdr_size) {
        if (fread(*out_buf + hdr_size, 1, frame_length - hdr_size, in) != (size_t)(frame_length - hdr_size)) {
            free(*out_buf);
            *out_buf = NULL;
            return GF_IO_ERR;
        }
    }
    *out_len = frame_length;
    return GF_OK;
}

static GF_Err build_concat_aac_for_seconds(const char *aac_path, const char *out_path, u32 target_seconds)
{
    FILE *in = fopen(aac_path, "rb");
    if (!in) {
        return GF_URL_ERROR;
    }
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fclose(in);
        return GF_URL_ERROR;
    }

    u32 sample_rate = 0;
    Bool sr_set = GF_FALSE;
    u32 frames_written = 0;
    u32 target_frames = 0;

    while (1) {
        u8 *frame = NULL;
        u32 frame_len = 0;
        GF_Err e = read_adts_frame(in, &frame, &frame_len, &sample_rate, &sr_set);
        if (e == GF_EOS) {
            if (!sr_set) {
                fclose(in);
                fclose(out);
                return GF_NON_COMPLIANT_BITSTREAM;
            }
            if (!target_frames) {
                target_frames = (target_seconds * sample_rate + 1023) / 1024;
            }
            if (frames_written >= target_frames) {
                fclose(in);
                fclose(out);
                return GF_OK;
            }
            rewind(in);
            continue;
        }
        if (e < GF_OK) {
            fclose(in);
            fclose(out);
            return e;
        }

        if (!target_frames) {
            target_frames = (target_seconds * sample_rate + 1023) / 1024;
        }
        if (frames_written < target_frames) {
            if (fwrite(frame, 1, frame_len, out) != frame_len) {
                free(frame);
                fclose(in);
                fclose(out);
                return GF_IO_ERR;
            }
            frames_written++;
        }
        free(frame);

        if (frames_written >= target_frames) {
            fclose(in);
            fclose(out);
            return GF_OK;
        }
    }
}

static GF_Err build_concat_h264(const char *frames_dir, const char *out_path, u32 total_frames)
{
    FrameList list = {0};
    GF_Err e = load_frames_from_dir(frames_dir, &list);
    if (e < GF_OK) {
        frame_list_free(&list);
        return e;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        frame_list_free(&list);
        return GF_URL_ERROR;
    }

    u32 i;
    for (i = 0; i < total_frames; i++) {
        const char *path = list.paths[i % list.count];
        e = append_file(out, path);
        if (e < GF_OK) {
            fclose(out);
            frame_list_free(&list);
            return e;
        }
    }

    fclose(out);
    frame_list_free(&list);
    return GF_OK;
}

static GF_Err build_sample_30s(void)
{
    const char *frames_dir = "applications/testapps/mp4muxdemux/h264";
    const char *aac_path = "applications/testapps/mp4muxdemux/aac.aac";
    const char *out_mp4 = "applications/testapps/mp4muxdemux/out_30s.mp4";
    const char *fps_str = "30";
    const u32 target_seconds = 30;
    const u32 fps = 30;
    const u32 total_frames = target_seconds * fps;
    char concat_path[GF_MAX_PATH];
    char concat_aac_path[GF_MAX_PATH];

    snprintf(concat_path, sizeof(concat_path), "%s/concat_30s.h264", frames_dir);
    snprintf(concat_aac_path, sizeof(concat_aac_path), "%s/concat_30s.aac", frames_dir);

    GF_Err e = build_concat_h264(frames_dir, concat_path, total_frames);
    if (e < GF_OK) {
        fprintf(stderr, "Failed to build H.264 stream: %s\n", gf_error_to_string(e));
        return e;
    }

    e = build_concat_aac_for_seconds(aac_path, concat_aac_path, target_seconds);
    if (e < GF_OK) {
        remove(concat_path);
        remove(concat_aac_path);
        fprintf(stderr, "Failed to build AAC stream: %s\n", gf_error_to_string(e));
        return e;
    }

    e = build_mux(concat_path, concat_aac_path, out_mp4, fps_str);
    remove(concat_path);
    remove(concat_aac_path);
    return e;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    gf_sys_init(GF_MemTrackerNone, NULL);

    GF_Err e = GF_OK;
    if (!strcmp(argv[1], "mux")) {
        if (argc != 6) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = build_mux(argv[2], argv[3], argv[4], argv[5]);
    } else if (!strcmp(argv[1], "demux")) {
        if (argc != 5) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = build_demux(argv[2], argv[3], argv[4]);
    } else if (!strcmp(argv[1], "demux_frames")) {
        if (argc != 4) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = build_demux_frames(argv[2], argv[3]);
    } else if (!strcmp(argv[1], "add_metadata_track")) {
        if (argc != 5) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = add_metadata_track(argv[2], argv[3], argv[4]);
    } else if (!strcmp(argv[1], "extract_metadata_track")) {
        if (argc != 4) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = extract_metadata_track(argv[2], argv[3]);
    } else if (!strcmp(argv[1], "make30")) {
        if (argc != 2) {
            print_usage(argv[0]);
            gf_sys_close();
            return 2;
        }
        e = build_sample_30s();
    } else {
        print_usage(argv[0]);
        gf_sys_close();
        return 2;
    }

    if (e < GF_OK) {
        fprintf(stderr, "Error: %s\n", gf_error_to_string(e));
        gf_sys_close();
        return 1;
    }

    gf_sys_close();
    return 0;
}
