#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gpac/filters.h>
#include <gpac/tools.h>

static void print_usage(const char *exe)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s mux <in.h264> <in.aac> <out.mp4> <fps>\n"
        "  %s demux <in.mp4> <out.h264> <out.aac>\n"
    "  %s make30\n"
        "\n"
        "Notes:\n"
        "  - H.264 input must be Annex B (start codes).\n"
        "  - AAC input must be ADTS.\n"
        "  - fps can be like 30 or 30000/1001.\n",
    exe, exe, exe);
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
    const char *frames_dir = "/Users/lance//work/Github/gpac/applications/testapps/mp4muxdemux/h264";
    const char *aac_path = "/Users/lance//work/Github/gpac/applications/testapps/mp4muxdemux/aac.aac";
    const char *out_mp4 = "/Users/lance//work/Github/gpac/applications/testapps/mp4muxdemux/out_30s.mp4";
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
