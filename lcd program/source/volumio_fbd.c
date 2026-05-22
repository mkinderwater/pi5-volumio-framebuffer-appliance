#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <json-c/json.h>

#ifndef JSON_C_TO_STRING_NOSLASHESCAPE
#define JSON_C_TO_STRING_NOSLASHESCAPE 16
#endif
#include <limits.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include <linux/fb.h>
#include <math.h>
#include <netinet/in.h>
#include <net/if.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// ---------------- Configuration ----------------

#define DESIGN_WIDTH 480
#define DESIGN_HEIGHT 320

#define VOLUMIO_HOST "127.0.0.1"
#define VOLUMIO_PORT 3000
#define VOLUMIO_PATH "/api/v1/getState"

// ---------------- Optimization Extensions ----------------

#define HARDWARE_FPS_SCROLLING 30.0   // 33.3ms windows for marquee text and fade animation
#define HARDWARE_FPS_STATIC     1.0   // 1000ms low-power check window
#define HARDWARE_FPS_SLEEP      0.2   // 5000ms deep-idle check window when nothing animates
#define HARDWARE_FPS_PLAYING    1.0   // 1000ms metadata/progress refresh when playback is static


#define COLON_FADE_TABLE_SIZE 64
#define COLON_FADE_HZ 0.65

#define BG_COLOR 0x000000
#define TEXT_COLOR_MAIN 0xFFFFFF
#define TEXT_COLOR_DIM 0x888888
#define PROGRESS_BAR_BG 0x333333
#define PROGRESS_BAR_FG 0x00FF00
#define VOLUME_OVERLAY_DIM_PERCENT 35
#define VOLUME_BAR_BG 0x101010

#define FONT_BOLD "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
#define FONT_REG  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

#define CONFIG_PATH "/etc/volumio_fbd_config.json"

#define HTTP_BUFFER_INITIAL (64u * 1024u)
#define HTTP_BUFFER_MAX     (4u * 1024u * 1024u)

#define FIXED_ONE 65536

#define DEFAULT_DEBUG_LOG_PATH "/var/log/volumio_fbd.log"
#define DEFAULT_DEBUG_LOG_MAX_BYTES (256 * 1024)
#define DEFAULT_WAIT_TIMEOUT_SECONDS 8.0
#define DEFAULT_ALBUM_ART_DELAY_SECONDS 1.5

// ---------------- Runtime State ----------------

static volatile sig_atomic_t g_running = 1;

// JSON config
static char g_cfg_fb_path[256] = "/dev/fb0";
static int g_cfg_width = DESIGN_WIDTH;
static int g_cfg_height = DESIGN_HEIGHT;
static bool g_cfg_clock_24h = false; // false = 12h, true = 24h
static char g_cfg_cpu_temp_unit = 'C';
static double g_cfg_wait_timeout_seconds = DEFAULT_WAIT_TIMEOUT_SECONDS;
static bool g_cfg_debug_enabled = false;
static char g_cfg_debug_log_path[256] = DEFAULT_DEBUG_LOG_PATH;
static long g_cfg_debug_log_max_bytes = DEFAULT_DEBUG_LOG_MAX_BYTES;
static FILE *g_debug_log_fp = NULL;
static long g_debug_log_bytes_written = 0;

// Dynamic framebuffer geometry
static int g_width = DESIGN_WIDTH;
static int g_height = DESIGN_HEIGHT;
static int g_bpp = 16;
static int g_bytes_per_pixel = 2;
static int g_line_length = DESIGN_WIDTH * 2;
static size_t g_fb_map_bytes = 0;
static struct fb_var_screeninfo g_vinfo;
static struct fb_fix_screeninfo g_finfo;

// Fast lookup tables for pixel format conversion
static uint32_t g_r_lut[256];
static uint32_t g_g_lut[256];
static uint32_t g_b_lut[256];

// Display descriptors
static int g_fb_fd = -1;
static uint8_t *g_fb_mem = NULL;

// Dynamic surfaces
static uint8_t *g_img = NULL;          // RGB888, g_width * g_height * 3
static uint32_t *g_native = NULL;      // native framebuffer pixel value per pixel, one 32-bit slot per pixel
static uint32_t *g_prev_native = NULL; // previous native pixel value per pixel, one 32-bit slot per pixel
static uint16_t *g_native16 = NULL;    // packed native 16-bit pixels for true memcpy blits on RGB565-style framebuffers
static uint16_t *g_prev_native16 = NULL; // previous packed native 16-bit pixels
static bool g_prev_frame_valid = false;

typedef struct {
    int min_x;
    int max_x;
    bool dirty;
} RowSpanTracker;

// Runtime-sized dirty row span map. One entry per visible framebuffer row.
static RowSpanTracker *g_dirty_spans = NULL;

// FreeType
static FT_Library g_ft = NULL;
static FT_Face g_font_bold = NULL;
static FT_Face g_font_reg = NULL;

#define GLYPH_ATLAS_MAX 64
#define GLYPH_CACHE_CHARS 128

typedef struct {
    bool valid;
    int width;
    int height;
    int pitch;
    int advance;
    int left;
    int top;
    uint8_t *alpha;
} GlyphCacheEntry;

typedef struct {
    bool used;
    FT_Face face;
    int pixel_size;
    GlyphCacheEntry glyphs[GLYPH_CACHE_CHARS];
} GlyphAtlas;

static GlyphAtlas g_glyph_atlases[GLYPH_ATLAS_MAX];
static size_t g_glyph_atlas_count = 0;

// Scroll-strip cache. Long scrolling titles are rendered once, then copied by offset.
static uint8_t *g_scroll_strip_rgb = NULL;
static int g_scroll_strip_w = 0;
static int g_scroll_strip_h = 0;
static int g_scroll_strip_cycle_w = 0;
static int g_scroll_strip_font_size = 0;
static int g_scroll_strip_clip_w = 0;
static int g_scroll_strip_clip_h = 0;
static int g_scroll_strip_baseline = 0;
static int g_scroll_strip_gap = 0;
static uint32_t g_scroll_strip_color = 0;
static FT_Face g_scroll_strip_face = NULL;
static char g_scroll_strip_text[256] = "";
static double g_scroll_strip_started_at = 0.0;

static bool g_last_big_clock_heartbeat = false;
static bool g_colon_fade_active = false;
static uint8_t g_colon_alpha = 255;

static const uint8_t g_sine_alpha_lut[COLON_FADE_TABLE_SIZE] = {
    0, 1, 2, 5, 10, 15, 21, 29,
    37, 47, 57, 67, 79, 90, 103, 115,
    128, 140, 152, 165, 176, 188, 198, 208,
    218, 226, 234, 240, 245, 250, 253, 254,
    255, 254, 253, 250, 245, 240, 234, 226,
    218, 208, 198, 188, 176, 165, 152, 140,
    128, 115, 103, 90, 79, 67, 57, 47,
    37, 29, 21, 15, 10, 5, 2, 1
};

static void free_scroll_strip(void);

// Keep-alive HTTP
static int g_http_fd = -1;
static uint8_t *g_http_buffer = NULL;
static size_t g_http_buf_cap = 0;
static size_t g_http_buf_len = 0;

// Volumio state
static char g_status_state[32] = "stop";
static char g_status_title[256] = "";
static char g_status_artist[256] = "";
static char g_status_album[256] = "";
static char g_status_albumart[512] = "";
static char g_status_track_type[64] = "";
static char g_status_samplerate[64] = "";
static char g_status_bitdepth[64] = "";
static int g_status_seek = 0;       // seconds, as reported by Volumio
static int g_status_duration = 0;   // seconds
static int g_status_volume = -1;    // 0-100, as reported by Volumio
static bool g_status_volume_seen = false;
static int g_volume_overlay_start_volume = -1;
static int g_volume_overlay_direction = 0; // 1 = up, -1 = down
static double g_volume_overlay_last_change_at = -1.0;
static double g_status_last_updated = 0.0;
static double g_no_metadata_started_at = -1.0;
static double g_status_fetch_failed_started_at = -1.0;
static bool g_wait_timeout_latched = false;
static double g_last_latch_log_at = -1.0;

// Album art
static uint8_t *g_art_rgba = NULL;
static int g_art_w = 0;
static int g_art_h = 0;
static char g_art_loaded_url[512] = "";
static char g_art_pending_key[1536] = "";
static double g_art_pending_since = -1.0;

// Scaled Album Art Cache (Optimization)
static uint8_t *g_art_scaled_rgb = NULL;
static int g_cached_art_w = 0;
static int g_cached_art_h = 0;

// ---------------- Fast Math ----------------

// Exact fast division by 255 for values up to 65025 (255*255)
static inline uint8_t fast_div_255(uint16_t val) {
    return (uint8_t)((val + 1 + (val >> 8)) >> 8);
}

// ---------------- Timing ----------------

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void sleep_seconds(double s) {
    if (s <= 0.0) return;

    struct timespec req;
    req.tv_sec = (time_t)s;
    req.tv_nsec = (long)((s - (double)req.tv_sec) * 1e9);

    while (nanosleep(&req, &req) == -1 && errno == EINTR && g_running) {}
}

static void update_colon_fade_alpha(double now) {
    if (!g_colon_fade_active) {
        g_colon_alpha = 255;
        return;
    }

    double phase = fmod(now * COLON_FADE_HZ, 1.0);
    if (phase < 0.0) phase += 1.0;

    size_t idx = (size_t)(phase * (double)COLON_FADE_TABLE_SIZE);
    if (idx >= COLON_FADE_TABLE_SIZE) idx = COLON_FADE_TABLE_SIZE - 1;

    g_colon_alpha = g_sine_alpha_lut[idx];
}

// ---------------- Debug Logging ----------------

static void debug_log_close(void) {
    if (g_debug_log_fp) {
        fflush(g_debug_log_fp);
        fclose(g_debug_log_fp);
        g_debug_log_fp = NULL;
    }

    g_debug_log_bytes_written = 0;
}

static void debug_log_rotate_if_needed(void) {
    if (!g_cfg_debug_enabled || !g_cfg_debug_log_path[0]) return;

    if (g_cfg_debug_log_max_bytes < 4096) {
        g_cfg_debug_log_max_bytes = 4096;
    }

    struct stat st;
    if (stat(g_cfg_debug_log_path, &st) != 0) return;

    if (st.st_size < g_cfg_debug_log_max_bytes) return;

    char rotated[512];
    snprintf(rotated, sizeof(rotated), "%s.1", g_cfg_debug_log_path);

    unlink(rotated);
    rename(g_cfg_debug_log_path, rotated);
}

static void debug_log_open(void) {
    if (!g_cfg_debug_enabled) return;

    debug_log_rotate_if_needed();

    g_debug_log_fp = fopen(g_cfg_debug_log_path, "a");
    if (!g_debug_log_fp) {
        fprintf(stderr, "[Debug Warning] Could not open log file: %s\n", g_cfg_debug_log_path);
        g_cfg_debug_enabled = false;
        return;
    }

    struct stat st;
    if (stat(g_cfg_debug_log_path, &st) == 0 && st.st_size > 0) {
        g_debug_log_bytes_written = (long)st.st_size;
    } else {
        g_debug_log_bytes_written = 0;
    }

    setvbuf(g_debug_log_fp, NULL, _IOLBF, 0);
}

static void debug_log_reopen_if_limit_reached(void) {
    if (!g_cfg_debug_enabled || !g_debug_log_fp) return;

    if (g_cfg_debug_log_max_bytes < 4096) {
        g_cfg_debug_log_max_bytes = 4096;
    }

    if (g_debug_log_bytes_written < g_cfg_debug_log_max_bytes) return;

    debug_log_close();
    debug_log_rotate_if_needed();
    debug_log_open();
}

static void debug_logf(const char *fmt, ...) {
    if (!g_cfg_debug_enabled || !g_debug_log_fp) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);

    int wrote = fprintf(g_debug_log_fp, "[%s] ", stamp);
    if (wrote > 0) g_debug_log_bytes_written += wrote;

    va_list ap;
    va_start(ap, fmt);
    wrote = vfprintf(g_debug_log_fp, fmt, ap);
    va_end(ap);
    if (wrote > 0) g_debug_log_bytes_written += wrote;

    if (fputc('\n', g_debug_log_fp) != EOF) {
        g_debug_log_bytes_written++;
    }

    debug_log_reopen_if_limit_reached();
}

#define DBG_LOG(...) debug_logf(__VA_ARGS__)

// ---------------- Utility ----------------

static inline int clamp_int(int v, int min_v, int max_v) {
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static inline int min_int(int a, int b) {
    return a < b ? a : b;
}

static inline int max_int(int a, int b) {
    return a > b ? a : b;
}

static inline int scale_x(int v) {
    return (int)(((long long)v * g_width + (DESIGN_WIDTH / 2)) / DESIGN_WIDTH);
}

static inline int scale_y(int v) {
    return (int)(((long long)v * g_height + (DESIGN_HEIGHT / 2)) / DESIGN_HEIGHT);
}

static int scale_font(int v) {
    int sx = scale_x(v);
    int sy = scale_y(v);
    int out = sx < sy ? sx : sy;
    return out < 6 ? 6 : out;
}

static inline int lerp_int_simple(int a, int b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (int)((float)a + ((float)(b - a) * t));
}

static void trim_in_place(char *s) {
    if (!s) return;

    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) {
        *(--end) = '\0';
    }
}

static void lower_ascii(char *s) {
    if (!s) return;
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

static void copy_config_string(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;

    if (!src || !*src) {
        dst[0] = '\0';
        return;
    }

    size_t src_len = strlen(src);
    if (src_len >= dst_sz) src_len = dst_sz - 1;

    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

static void normalize_state(char *dst, size_t dst_sz, const char *src) {
    char tmp[64];

    if (!dst || dst_sz == 0) return;

    snprintf(tmp, sizeof(tmp), "%s", src ? src : "stop");
    trim_in_place(tmp);
    lower_ascii(tmp);

    if (strcmp(tmp, "playing") == 0 || strcmp(tmp, "play") == 0) {
        snprintf(dst, dst_sz, "play");
    } else if (strcmp(tmp, "paused") == 0 || strcmp(tmp, "pause") == 0) {
        snprintf(dst, dst_sz, "pause");
    } else if (strcmp(tmp, "stopped") == 0 ||
               strcmp(tmp, "stopping") == 0 ||
               strcmp(tmp, "stop") == 0 ||
               strcmp(tmp, "idle") == 0 ||
               strcmp(tmp, "") == 0) {
        snprintf(dst, dst_sz, "stop");
    } else {
        snprintf(dst, dst_sz, "%s", tmp);
    }
}

static bool looks_empty_value(const char *s) {
    if (!s) return true;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return true;

    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s", s);
    trim_in_place(tmp);
    lower_ascii(tmp);

    return strcmp(tmp, "null") == 0 ||
           strcmp(tmp, "none") == 0 ||
           strcmp(tmp, "unknown") == 0 ||
           strcmp(tmp, "unknown track") == 0 ||
           strcmp(tmp, "unknown artist") == 0;
}

static void copy_json_string(char *dst, size_t dst_sz, json_object *root, const char *key, const char *fallback) {
    if (!dst || dst_sz == 0) return;

    json_object *val = NULL;
    const char *s = NULL;

    if (root && json_object_object_get_ex(root, key, &val) && val) {
        s = json_object_get_string(val);
    }

    if (looks_empty_value(s)) snprintf(dst, dst_sz, "%s", fallback ? fallback : "");
    else snprintf(dst, dst_sz, "%s", s);
}

static bool albumart_is_real_track_art(void) {
    if (g_status_albumart[0] == '\0') return false;

    if (strcmp(g_status_albumart, "/albumart") == 0) return false;

    // Spotify can return a direct HTTPS CDN URL such as:
    // https://i.scdn.co/image/...
    // Treat that as real track art so the metadata latch does not suppress playback.
    if (strncmp(g_status_albumart, "http://", 7) == 0 ||
        strncmp(g_status_albumart, "https://", 8) == 0) {
        return true;
    }

    if (strchr(g_status_albumart, '?') != NULL) return true;
    if (strstr(g_status_albumart, "cacheid=") != NULL) return true;
    if (strstr(g_status_albumart, "path=") != NULL) return true;
    if (strstr(g_status_albumart, "web=") != NULL) return true;

    if (strncmp(g_status_albumart, "/albumart/", 10) == 0) return true;
    if (strncmp(g_status_albumart, "albumart/", 9) == 0) return true;

    return false;
}

static bool state_has_track_payload(void) {
    return g_status_title[0] != '\0' ||
           g_status_artist[0] != '\0' ||
           g_status_album[0] != '\0' ||
           g_status_duration > 0 ||
           albumart_is_real_track_art();
}

static bool state_shows_metadata(void) {
    return strcmp(g_status_state, "play") == 0 || strcmp(g_status_state, "pause") == 0;
}

static void free_scaled_art(void) {
    if (g_art_scaled_rgb) {
        free(g_art_scaled_rgb);
        g_art_scaled_rgb = NULL;
    }
    g_cached_art_w = 0;
    g_cached_art_h = 0;
}

static void reset_album_art_delay(void) {
    g_art_pending_key[0] = '\0';
    g_art_pending_since = -1.0;
}

static void force_large_clock_state(const char *reason) {
    DBG_LOG("Returning to large clock: reason=%s state=%s title='%s' duration=%d",
            reason ? reason : "unknown", g_status_state, g_status_title, g_status_duration);

    snprintf(g_status_state, sizeof(g_status_state), "stop");
    g_status_seek = 0;
    g_status_duration = 0;
    g_status_title[0] = '\0';
    g_status_artist[0] = '\0';
    g_status_album[0] = '\0';
    g_status_albumart[0] = '\0';
    g_status_track_type[0] = '\0';
    g_status_samplerate[0] = '\0';
    g_status_bitdepth[0] = '\0';
    g_art_loaded_url[0] = '\0';
    reset_album_art_delay();
    g_no_metadata_started_at = -1.0;
    g_status_fetch_failed_started_at = -1.0;
    g_wait_timeout_latched = true;

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }
    free_scaled_art();
    free_scroll_strip();
}

static void clear_track_payload_only(void) {
    g_status_seek = 0;
    g_status_duration = 0;
    g_status_title[0] = '\0';
    g_status_artist[0] = '\0';
    g_status_album[0] = '\0';
    g_status_albumart[0] = '\0';
    g_status_track_type[0] = '\0';
    g_status_samplerate[0] = '\0';
    g_status_bitdepth[0] = '\0';
    g_art_loaded_url[0] = '\0';
    reset_album_art_delay();

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }
    free_scaled_art();
    free_scroll_strip();
}

static bool suppress_metadata_less_playback_if_latched(double now) {
    if (!g_wait_timeout_latched) return false;

    if (state_shows_metadata() && !state_has_track_payload()) {
        if (g_last_latch_log_at < 0.0 || now - g_last_latch_log_at >= 5.0) {
            DBG_LOG("Wait-timeout latch active: suppressing metadata-less playback state=%s albumart='%s'",
                    g_status_state, g_status_albumart);
            g_last_latch_log_at = now;
        }

        snprintf(g_status_state, sizeof(g_status_state), "stop");
        clear_track_payload_only();
        g_no_metadata_started_at = -1.0;
        return true;
    }

    if (state_has_track_payload()) {
        DBG_LOG("Wait-timeout latch cleared: real metadata returned state=%s title='%s' duration=%d albumart='%s'",
                g_status_state, g_status_title, g_status_duration, g_status_albumart);
        g_wait_timeout_latched = false;
        g_last_latch_log_at = -1.0;
    }

    return false;
}

static void apply_wait_timeout(double now) {
    if (g_cfg_wait_timeout_seconds <= 0.0) {
        g_no_metadata_started_at = -1.0;
        return;
    }

    if (state_shows_metadata() && !state_has_track_payload()) {
        if (g_no_metadata_started_at < 0.0) {
            g_no_metadata_started_at = now;
            DBG_LOG("No metadata while in playback state. timeout_started state=%s limit=%.2f",
                    g_status_state, g_cfg_wait_timeout_seconds);
        }

        if (now - g_no_metadata_started_at >= g_cfg_wait_timeout_seconds) {
            force_large_clock_state("no metadata timeout");
        }
    } else {
        g_no_metadata_started_at = -1.0;
    }
}


static int interface_ipv4_priority(const char *name) {
    if (!name || !*name) return 0;

    if (strncmp(name, "eth", 3) == 0) return 100;
    if (strncmp(name, "en", 2) == 0) return 95;
    if (strncmp(name, "wlan", 4) == 0) return 90;
    if (strncmp(name, "wl", 2) == 0) return 85;
    if (strncmp(name, "usb", 3) == 0) return 80;

    if (strncmp(name, "lo", 2) == 0) return 0;
    if (strncmp(name, "docker", 6) == 0) return 0;
    if (strncmp(name, "br-", 3) == 0) return 0;
    if (strncmp(name, "veth", 4) == 0) return 0;
    if (strncmp(name, "tun", 3) == 0) return 0;
    if (strncmp(name, "tap", 3) == 0) return 0;

    return 50;
}

static bool get_primary_ipv4(char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return false;

    snprintf(dst, dst_sz, "No IP");

    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) {
        return false;
    }

    int best_score = 0;
    char best_ip[INET_ADDRSTRLEN] = "";

    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        int score = interface_ipv4_priority(ifa->ifa_name);
        if (score <= 0) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        char ip[INET_ADDRSTRLEN];

        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;

        if (strncmp(ip, "127.", 4) == 0) continue;
        if (strncmp(ip, "169.254.", 8) == 0) continue;

        if (score > best_score) {
            best_score = score;
            snprintf(best_ip, sizeof(best_ip), "%s", ip);
        }
    }

    freeifaddrs(ifaddr);

    if (best_ip[0]) {
        snprintf(dst, dst_sz, "%s", best_ip);
        return true;
    }

    return false;
}

static void format_idle_date(char *dst, size_t dst_sz, const struct tm *tm_value) {
    if (!dst || dst_sz == 0) return;

    if (!tm_value) {
        dst[0] = '\0';
        return;
    }

    char weekday[32];
    char month[32];

    strftime(weekday, sizeof(weekday), "%A", tm_value);
    strftime(month, sizeof(month), "%B", tm_value);

    snprintf(dst, dst_sz, "%s, %s %d", weekday, month, tm_value->tm_mday);
}

static void set_cpu_temp_unit_from_string(const char *unit) {
    if (!unit || !*unit) return;

    char c = (char)toupper((unsigned char)unit[0]);
    if (c == 'C' || c == 'F') {
        g_cfg_cpu_temp_unit = c;
    }
}

static void set_clock_type_from_string(const char *clock_type) {
    if (!clock_type || !*clock_type) return;

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%s", clock_type);
    trim_in_place(tmp);
    lower_ascii(tmp);

    if (strcmp(tmp, "24") == 0 || strcmp(tmp, "24h") == 0 || strcmp(tmp, "24-hour") == 0) {
        g_cfg_clock_24h = true;
    } else if (strcmp(tmp, "12") == 0 || strcmp(tmp, "12h") == 0 || strcmp(tmp, "12-hour") == 0) {
        g_cfg_clock_24h = false;
    }
}

static bool read_cpu_temp_c(double *out_c) {
    if (!out_c) return false;

    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) return false;

    long raw = 0;
    int ok = fscanf(fp, "%ld", &raw);
    fclose(fp);

    if (ok != 1 || raw <= 0) return false;

    *out_c = (raw > 1000) ? ((double)raw / 1000.0) : (double)raw;
    return true;
}

static bool format_cpu_temp_text(char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return false;

    double temp_c = 0.0;
    if (!read_cpu_temp_c(&temp_c)) {
        snprintf(dst, dst_sz, "CPU: N/A");
        return false;
    }

    if (g_cfg_cpu_temp_unit == 'F') {
        double temp_f = (temp_c * 9.0 / 5.0) + 32.0;
        snprintf(dst, dst_sz, "CPU: %.0fF", temp_f);
    } else {
        snprintf(dst, dst_sz, "CPU: %.0fC", temp_c);
    }

    return temp_c > 80.0;
}

// ---------------- Config ----------------

static bool write_all_fd(int fd, const char *data, size_t len) {
    size_t written = 0;

    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        written += (size_t)n;
    }

    return true;
}

static bool create_default_json_config(void) {
    json_object *root = json_object_new_object();
    json_object *display = json_object_new_object();
    json_object *volumio = json_object_new_object();
    json_object *debug = json_object_new_object();

    if (!root || !display || !volumio || !debug) {
        if (root) json_object_put(root);
        if (display) json_object_put(display);
        if (volumio) json_object_put(volumio);
        if (debug) json_object_put(debug);
        return false;
    }

    json_object_object_add(display, "fb_path", json_object_new_string(g_cfg_fb_path));
    json_object_object_add(display, "width", json_object_new_int(g_cfg_width));
    json_object_object_add(display, "height", json_object_new_int(g_cfg_height));
    json_object_object_add(display, "clock_type", json_object_new_string(g_cfg_clock_24h ? "24h" : "12h"));

    char temp_unit[2] = { g_cfg_cpu_temp_unit, '\0' };
    json_object_object_add(display, "cpu_temp_unit", json_object_new_string(temp_unit));

    json_object_object_add(volumio, "wait_timeout_seconds", json_object_new_double(g_cfg_wait_timeout_seconds));

    json_object_object_add(debug, "enabled", json_object_new_boolean(g_cfg_debug_enabled));
    json_object_object_add(debug, "log_path", json_object_new_string(g_cfg_debug_log_path));
    json_object_object_add(debug, "max_bytes", json_object_new_int64(g_cfg_debug_log_max_bytes));

    json_object_object_add(root, "display", display);
    json_object_object_add(root, "volumio", volumio);
    json_object_object_add(root, "debug", debug);

    const char *json_text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
    size_t json_len = strlen(json_text);

    int fd = open(CONFIG_PATH, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        if (errno != EEXIST) {
            fprintf(stderr, "[Config Warning] Could not create %s: %s\n", CONFIG_PATH, strerror(errno));
        }
        json_object_put(root);
        return false;
    }

    bool ok = write_all_fd(fd, json_text, json_len) && write_all_fd(fd, "\n", 1);
    if (!ok) {
        fprintf(stderr, "[Config Warning] Could not write %s: %s\n", CONFIG_PATH, strerror(errno));
        close(fd);
        unlink(CONFIG_PATH);
        json_object_put(root);
        return false;
    }

    close(fd);
    json_object_put(root);

    printf("[Config] Created default config at %s.\n", CONFIG_PATH);
    return true;
}

static void load_json_config(void) {
    int fd = open(CONFIG_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            create_default_json_config();
            fd = open(CONFIG_PATH, O_RDONLY | O_CLOEXEC);
        }

        if (fd < 0) {
            printf("[Config] No readable config at %s. Using built-in defaults.\n", CONFIG_PATH);
            return;
        }
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 65536) {
        close(fd);
        return;
    }

    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        close(fd);
        return;
    }

    ssize_t got = read(fd, buf, (size_t)st.st_size);
    close(fd);

    if (got <= 0) {
        free(buf);
        return;
    }

    buf[got] = '\0';
    json_object *root = json_tokener_parse(buf);
    free(buf);

    if (!root) return;

    json_object *display_obj = NULL;
    if (json_object_object_get_ex(root, "display", &display_obj) && display_obj) {
        json_object *v = NULL;

        if (json_object_object_get_ex(display_obj, "fb_path", &v) && v) {
            const char *s = json_object_get_string(v);
            if (s && *s) copy_config_string(g_cfg_fb_path, sizeof(g_cfg_fb_path), s);
        }

        if (json_object_object_get_ex(display_obj, "width", &v) && v) {
            int w = json_object_get_int(v);
            if (w > 0) g_cfg_width = w;
        }

        if (json_object_object_get_ex(display_obj, "height", &v) && v) {
            int h = json_object_get_int(v);
            if (h > 0) g_cfg_height = h;
        }

        if (json_object_object_get_ex(display_obj, "clock_type", &v) && v) {
            set_clock_type_from_string(json_object_get_string(v));
        }

        if (json_object_object_get_ex(display_obj, "cpu_temp_unit", &v) && v) {
            set_cpu_temp_unit_from_string(json_object_get_string(v));
        }

        if (json_object_object_get_ex(display_obj, "temperature_unit", &v) && v) {
            set_cpu_temp_unit_from_string(json_object_get_string(v));
        }

        if (json_object_object_get_ex(display_obj, "volumio_wait_timeout_seconds", &v) && v) {
            double timeout = json_object_get_double(v);
            if (timeout >= 0.0) g_cfg_wait_timeout_seconds = timeout;
        }

        if (json_object_object_get_ex(display_obj, "no_metadata_timeout_seconds", &v) && v) {
            double timeout = json_object_get_double(v);
            if (timeout >= 0.0) g_cfg_wait_timeout_seconds = timeout;
        }
    }

    json_object *volumio_obj = NULL;
    if (json_object_object_get_ex(root, "volumio", &volumio_obj) && volumio_obj) {
        json_object *v = NULL;

        if (json_object_object_get_ex(volumio_obj, "wait_timeout_seconds", &v) && v) {
            double timeout = json_object_get_double(v);
            if (timeout >= 0.0) g_cfg_wait_timeout_seconds = timeout;
        }

        if (json_object_object_get_ex(volumio_obj, "no_metadata_timeout_seconds", &v) && v) {
            double timeout = json_object_get_double(v);
            if (timeout >= 0.0) g_cfg_wait_timeout_seconds = timeout;
        }
    }

    json_object *debug_obj = NULL;
    if (json_object_object_get_ex(root, "debug", &debug_obj) && debug_obj) {
        json_object *v = NULL;

        if (json_object_object_get_ex(debug_obj, "enabled", &v) && v) {
            g_cfg_debug_enabled = json_object_get_boolean(v);
        }

        if (json_object_object_get_ex(debug_obj, "log_path", &v) && v) {
            const char *s = json_object_get_string(v);
            if (s && *s) copy_config_string(g_cfg_debug_log_path, sizeof(g_cfg_debug_log_path), s);
        }

        if (json_object_object_get_ex(debug_obj, "max_bytes", &v) && v) {
            long max_bytes = (long)json_object_get_int(v);
            if (max_bytes >= 4096) g_cfg_debug_log_max_bytes = max_bytes;
        }
    }

    json_object_put(root);
}

// ---------------- HTTP Keep-Alive ----------------

static void close_http_keepalive(void) {
    if (g_http_fd >= 0) {
        close(g_http_fd);
        g_http_fd = -1;
    }
}

static void reset_http_buffer(void) {
    free(g_http_buffer);
    g_http_buffer = NULL;
    g_http_buf_cap = 0;
    g_http_buf_len = 0;
}

static bool set_socket_timeout(int fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    bool ok = true;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) ok = false;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) ok = false;
    return ok;
}

static bool connect_http_keepalive(void) {
    if (!g_running) return false;
    if (g_http_fd >= 0) return true;

    g_http_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_http_fd < 0) {
        DBG_LOG("HTTP socket create failed: errno=%d", errno);
        return false;
    }

    set_socket_timeout(g_http_fd, 2);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VOLUMIO_PORT);

    if (inet_pton(AF_INET, VOLUMIO_HOST, &addr.sin_addr) != 1) {
        DBG_LOG("HTTP inet_pton failed for host %s", VOLUMIO_HOST);
        close_http_keepalive();
        return false;
    }

    if (connect(g_http_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        DBG_LOG("HTTP connect failed to %s:%d errno=%d", VOLUMIO_HOST, VOLUMIO_PORT, errno);
        close_http_keepalive();
        return false;
    }

    DBG_LOG("HTTP keep-alive connected to %s:%d", VOLUMIO_HOST, VOLUMIO_PORT);
    return true;
}

static bool send_all(int fd, const char *data, size_t len) {
    if (fd < 0 || (!data && len > 0)) return false;
    if (!g_running) return false;

    size_t sent = 0;

    while (sent < len && g_running) {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLOUT;

        // Explicit 1000ms write deadline per chunk so the display loop cannot hang here.
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            close_http_keepalive();
            return false;
        }

        if (ret == 0) {
            close_http_keepalive();
            return false;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close_http_keepalive();
            return false;
        }
        if (!(pfd.revents & POLLOUT)) continue;
        if (!g_running) return false;

        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            close_http_keepalive();
            return false;
        }

        if (n == 0) {
            close_http_keepalive();
            return false;
        }
        sent += (size_t)n;
    }

    return sent == len;
}

static uint8_t *find_header_end(uint8_t *data, size_t len) {
    if (!data || len < 4) return NULL;

    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }

    return NULL;
}

static bool header_has_token(uint8_t *headers, size_t header_len, const char *needle) {
    if (!headers || !needle || header_len == 0) return false;

    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > header_len) return false;

    // Bounded scan. Do not temporarily write a null terminator into the receive buffer.
    for (size_t i = 0; i <= header_len - needle_len; i++) {
        if (strncasecmp((const char *)(headers + i), needle, needle_len) == 0) {
            return true;
        }
    }

    return false;
}

static ssize_t header_content_length(uint8_t *headers, size_t header_len) {
    if (!headers || header_len == 0) return -1;

    const char *target = "Content-Length:";
    size_t target_len = strlen(target);
    if (target_len > header_len) return -1;

    for (size_t i = 0; i <= header_len - target_len; i++) {
        if (strncasecmp((const char *)(headers + i), target, target_len) != 0) continue;

        uint8_t *p = headers + i + target_len;
        uint8_t *end = headers + header_len;

        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end || !isdigit((unsigned char)*p)) return -1;

        unsigned long value = 0;
        while (p < end && isdigit((unsigned char)*p)) {
            unsigned digit = (unsigned)(*p - '0');
            if (value > ((unsigned long)SSIZE_MAX - digit) / 10UL) return -1;
            value = (value * 10UL) + digit;
            p++;
        }

        return (ssize_t)value;
    }

    return -1;
}

static uint8_t *decode_chunked_body(const uint8_t *body, size_t body_len, size_t *out_len) {
    const uint8_t *p = body;
    const uint8_t *end = body + body_len;
    uint8_t *out = malloc(body_len + 1);
    if (!out) return NULL;

    size_t used = 0;

    while (p < end) {
        const uint8_t *line_end = NULL;
        for (const uint8_t *q = p; q + 1 < end; q++) {
            if (q[0] == '\r' && q[1] == '\n') {
                line_end = q;
                break;
            }
        }

        if (!line_end) {
            free(out);
            return NULL;
        }

        char line[64];
        size_t line_len = (size_t)(line_end - p);
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';

        long chunk = strtol(line, NULL, 16);
        if (chunk < 0) {
            free(out);
            return NULL;
        }

        p = line_end + 2;

        if (chunk == 0) break;

        if ((size_t)(end - p) < (size_t)chunk + 2) {
            free(out);
            return NULL;
        }

        memcpy(out + used, p, (size_t)chunk);
        used += (size_t)chunk;
        p += chunk;

        if (p + 1 >= end || p[0] != '\r' || p[1] != '\n') {
            free(out);
            return NULL;
        }

        p += 2;
    }

    out[used] = '\0';
    if (out_len) *out_len = used;
    return out;
}

static uint8_t *http_get_once(const char *path, const char *accept_header, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!g_running) return NULL;

    if (!connect_http_keepalive()) return NULL;

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Accept: %s\r\n"
             "Connection: keep-alive\r\n"
             "User-Agent: volumio_fbd/merged-compat\r\n"
             "\r\n",
             path, VOLUMIO_HOST, VOLUMIO_PORT, accept_header ? accept_header : "*/*");

    if (!send_all(g_http_fd, req, strlen(req))) {
        DBG_LOG("HTTP send failed for path %s errno=%d", path ? path : "(null)", errno);
        close_http_keepalive();
        return NULL;
    }
    if (!g_running) return NULL;

    g_http_buf_len = 0;
    if (!g_http_buffer) {
        g_http_buf_cap = HTTP_BUFFER_INITIAL;
        g_http_buffer = malloc(g_http_buf_cap);
        if (!g_http_buffer) {
            close_http_keepalive();
            return NULL;
        }
    }

    size_t header_len = 0;
    size_t body_start = 0;
    bool header_seen = false;
    bool is_chunked = false;
    ssize_t content_len = -1;

    while (g_running) {
        if (g_http_buf_len + 4096 + 1 > g_http_buf_cap) {
            size_t new_cap = g_http_buf_cap ? g_http_buf_cap * 2 : HTTP_BUFFER_INITIAL;

            if (new_cap < g_http_buf_cap || new_cap > HTTP_BUFFER_MAX) {
                DBG_LOG("HTTP buffer limit reached for path %s cap=%zu", path ? path : "(null)", g_http_buf_cap);
                reset_http_buffer();
                close_http_keepalive();
                return NULL;
            }

            uint8_t *new_buf = realloc(g_http_buffer, new_cap);
            if (!new_buf) {
                DBG_LOG("HTTP buffer realloc failed for path %s requested=%zu", path ? path : "(null)", new_cap);
                reset_http_buffer();
                close_http_keepalive();
                return NULL;
            }

            g_http_buffer = new_buf;
            g_http_buf_cap = new_cap;
        }

        ssize_t n = recv(g_http_fd, g_http_buffer + g_http_buf_len, g_http_buf_cap - g_http_buf_len - 1, 0);
        if (n <= 0) {
            DBG_LOG("HTTP recv failed/closed for path %s n=%zd errno=%d", path ? path : "(null)", n, errno);
            close_http_keepalive();
            return NULL;
        }

        g_http_buf_len += (size_t)n;
        g_http_buffer[g_http_buf_len] = '\0';

        if (!header_seen) {
            uint8_t *he = find_header_end(g_http_buffer, g_http_buf_len);
            if (!he) continue;

            header_len = (size_t)(he - g_http_buffer);
            body_start = header_len + 4;
            header_seen = true;

            int status_code = 0;
            sscanf((char *)g_http_buffer, "HTTP/%*s %d", &status_code);
            if (status_code < 200 || status_code >= 300) {
                DBG_LOG("HTTP non-success status %d for path %s", status_code, path ? path : "(null)");
                close_http_keepalive();
                return NULL;
            }

            is_chunked = header_has_token(g_http_buffer, header_len, "Transfer-Encoding: chunked");
            content_len = header_content_length(g_http_buffer, header_len);
        }

        if (!header_seen) continue;

        size_t have_body = g_http_buf_len - body_start;

        if (!is_chunked && content_len >= 0 && have_body >= (size_t)content_len) {
            uint8_t *body = malloc((size_t)content_len + 1);
            if (!body) return NULL;

            memcpy(body, g_http_buffer + body_start, (size_t)content_len);
            body[content_len] = '\0';
            if (out_len) *out_len = (size_t)content_len;
            return body;
        }

        if (is_chunked) {
            size_t decoded_len = 0;
            uint8_t *decoded = decode_chunked_body(g_http_buffer + body_start, have_body, &decoded_len);
            if (decoded) {
                if (out_len) *out_len = decoded_len;
                return decoded;
            }
        }
    }

    return NULL;
}

static uint8_t *http_get_localhost_body_keepalive(const char *path, const char *accept_header, size_t *out_len) {
    if (!g_running) return NULL;

    uint8_t *body = http_get_once(path, accept_header, out_len);
    if (body) return body;
    if (!g_running) return NULL;

    close_http_keepalive();
    if (!g_running) return NULL;

    return http_get_once(path, accept_header, out_len);
}

// ---------------- Volumio ----------------

static void url_query_component(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';

    if (!src || !*src) return;

    size_t used = 0;

    for (const unsigned char *p = (const unsigned char *)src; *p && used + 1 < dst_sz; p++) {
        unsigned char c = *p;

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[used++] = (char)c;
            dst[used] = '\0';
            continue;
        }

        if (used + 3 >= dst_sz) break;
        snprintf(dst + used, dst_sz - used, "%%%02X", c);
        used += 3;
    }

    dst[used] = '\0';
}

static bool build_metadata_albumart_path(char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return false;
    dst[0] = '\0';

    if (looks_empty_value(g_status_artist) || looks_empty_value(g_status_album)) {
        return false;
    }

    char artist[512];
    char album[512];

    url_query_component(artist, sizeof(artist), g_status_artist);
    url_query_component(album, sizeof(album), g_status_album);

    if (!artist[0] || !album[0]) return false;

    snprintf(dst, dst_sz, "/albumart?web=%s/%s/extralarge&metadata=false", artist, album);
    return true;
}

static int build_album_art_candidates(char paths[][2048], int max_count, const char *src) {
    if (!paths || max_count <= 0) return 0;

    int count = 0;
    for (int i = 0; i < max_count; i++) paths[i][0] = '\0';

    if (!src || !*src) return 0;

    if (strncmp(src, "http://127.0.0.1:3000", 21) == 0) {
        snprintf(paths[count++], 2048, "%s", src + 21);
        return count;
    }

    if (strncmp(src, "http://localhost:3000", 21) == 0) {
        snprintf(paths[count++], 2048, "%s", src + 21);
        return count;
    }

    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
        if (build_metadata_albumart_path(paths[count], 2048)) count++;

        if (count < max_count) {
            snprintf(paths[count++], 2048, "/albumart?web=%s", src);
        }

        return count;
    }

    if (src[0] == '/') snprintf(paths[count++], 2048, "%s", src);
    else snprintf(paths[count++], 2048, "/%s", src);

    return count;
}

static void update_album_art_if_needed(void) {
    if (!state_shows_metadata() || g_status_albumart[0] == '\0') {
        if (g_art_rgba) {
            DBG_LOG("Album art cleared: state=%s albumart='%s'", g_status_state, g_status_albumart);
            stbi_image_free(g_art_rgba);
            g_art_rgba = NULL;
            free_scaled_art();
        }
        g_art_loaded_url[0] = '\0';
        reset_album_art_delay();
        return;
    }

    char art_cache_key[1536];
    snprintf(art_cache_key, sizeof(art_cache_key), "%s|%s|%s|%s|%d",
             g_status_albumart,
             g_status_title,
             g_status_artist,
             g_status_album,
             g_status_duration);

    if (strcmp(art_cache_key, g_art_loaded_url) == 0 && g_art_rgba) return;

    double now = monotonic_seconds();

    if (strcmp(art_cache_key, g_art_pending_key) != 0) {
        snprintf(g_art_pending_key, sizeof(g_art_pending_key), "%s", art_cache_key);
        g_art_pending_since = now;

        if (g_art_rgba) {
            DBG_LOG("Album art delayed: clearing previous art while waiting for stable Volumio artwork");
            stbi_image_free(g_art_rgba);
            g_art_rgba = NULL;
            free_scaled_art();
        }

        return;
    }

    if (g_art_pending_since >= 0.0 && now - g_art_pending_since < DEFAULT_ALBUM_ART_DELAY_SECONDS) {
        return;
    }

    char art_paths[2][2048];
    int art_path_count = build_album_art_candidates(art_paths, 2, g_status_albumart);

    if (art_path_count <= 0) {
        DBG_LOG("Album art path unsupported: raw='%s'", g_status_albumart);
        if (g_art_rgba) {
            stbi_image_free(g_art_rgba);
            g_art_rgba = NULL;
            free_scaled_art();
        }
        g_art_loaded_url[0] = '\0';
        return;
    }

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
        free_scaled_art();
    }

    bool loaded = false;

    for (int i = 0; i < art_path_count && !loaded; i++) {
        const char *art_path = art_paths[i];
        if (!art_path || !*art_path) continue;

        close_http_keepalive();

        size_t img_len = 0;
        uint8_t *img_data = http_get_localhost_body_keepalive(art_path, "image/*", &img_len);

        if (img_data && img_len > 0) {
            int comp = 0;
            g_art_rgba = stbi_load_from_memory(img_data, (int)img_len, &g_art_w, &g_art_h, &comp, 4);
            if (g_art_rgba) {
                snprintf(g_art_loaded_url, sizeof(g_art_loaded_url), "%s", art_cache_key);
                reset_album_art_delay();
                loaded = true;
                DBG_LOG("Album art loaded: candidate=%d path=%s bytes=%zu size=%dx%d",
                        i + 1, art_path, img_len, g_art_w, g_art_h);
            } else {
                DBG_LOG("Album art decode failed: candidate=%d path=%s bytes=%zu", i + 1, art_path, img_len);
            }
        } else {
            DBG_LOG("Album art download failed: candidate=%d path=%s", i + 1, art_path);
        }

        free(img_data);
    }

    if (!loaded) {
        g_art_loaded_url[0] = '\0';
        g_art_w = 0;
        g_art_h = 0;
        free_scaled_art();
    }
}


static bool json_value_to_volume_int(json_object *obj, int *out_volume) {
    if (!obj || !out_volume) return false;

    json_type type = json_object_get_type(obj);

    if (type == json_type_int || type == json_type_double) {
        int v = (int)round(json_object_get_double(obj));
        *out_volume = clamp_int(v, 0, 100);
        return true;
    }

    if (type == json_type_string) {
        const char *s = json_object_get_string(obj);
        if (!s) return false;

        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) return false;

        char *end = NULL;
        errno = 0;
        long v = strtol(s, &end, 10);
        if (errno != 0 || end == s) return false;

        *out_volume = clamp_int((int)v, 0, 100);
        return true;
    }

    if (type == json_type_object) {
        static const char *keys[] = { "value", "current", "volume", "vol" };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            json_object *child = NULL;
            if (json_object_object_get_ex(obj, keys[i], &child) && child) {
                if (json_value_to_volume_int(child, out_volume)) return true;
            }
        }
    }

    return false;
}

static bool extract_volume_from_state(json_object *root, int *out_volume) {
    if (!root || !out_volume) return false;

    static const char *keys[] = {
        "volume",
        "vol",
        "Volume",
        "airplayVolume",
        "airplay_volume"
    };

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        json_object *val = NULL;
        if (json_object_object_get_ex(root, keys[i], &val) && val) {
            if (json_value_to_volume_int(val, out_volume)) return true;
        }
    }

    return false;
}

static void note_volume_state(int new_volume, double now) {
    new_volume = clamp_int(new_volume, 0, 100);

    if (!g_status_volume_seen) {
        g_status_volume = new_volume;
        g_status_volume_seen = true;
        DBG_LOG("Volumio volume initialized: volume=%d", g_status_volume);
        return;
    }

    if (new_volume == g_status_volume) return;

    int old_volume = g_status_volume;
    g_volume_overlay_start_volume = old_volume;
    g_status_volume = new_volume;
    g_volume_overlay_direction = (new_volume > old_volume) ? 1 : -1;
    g_volume_overlay_last_change_at = now;

    DBG_LOG("Volumio volume changed: old=%d new=%d direction=%s overlay_timeout=%.2f",
            old_volume, new_volume,
            g_volume_overlay_direction > 0 ? "up" : "down",
            g_cfg_wait_timeout_seconds * 0.5);
}

static void update_volumio_state(void) {
    double now = monotonic_seconds();
    static double s_last_http_fetch = 0.0;

    if (now - s_last_http_fetch < 0.5) return;
    s_last_http_fetch = now;

    size_t len = 0;
    uint8_t *body = http_get_localhost_body_keepalive(VOLUMIO_PATH, "application/json", &len);
    if (!body) {
        DBG_LOG("Volumio state fetch failed");

        if (g_cfg_wait_timeout_seconds > 0.0 && state_shows_metadata() && !state_has_track_payload()) {
            if (g_status_fetch_failed_started_at < 0.0) {
                g_status_fetch_failed_started_at = now;
            }

            if (now - g_status_fetch_failed_started_at >= g_cfg_wait_timeout_seconds) {
                force_large_clock_state("volumio fetch timeout with no metadata");
            }
        }

        return;
    }

    g_status_fetch_failed_started_at = -1.0;

    json_object *root = json_tokener_parse((char *)body);
    free(body);

    if (!root) {
        DBG_LOG("Volumio JSON parse failed, bytes=%zu", len);
        return;
    }

    char old_state[sizeof(g_status_state)];
    char old_title[sizeof(g_status_title)];
    char old_artist[sizeof(g_status_artist)];

    snprintf(old_state, sizeof(old_state), "%s", g_status_state);
    snprintf(old_title, sizeof(old_title), "%s", g_status_title);
    snprintf(old_artist, sizeof(old_artist), "%s", g_status_artist);

    json_object *val = NULL;

    if (json_object_object_get_ex(root, "status", &val)) {
        normalize_state(g_status_state, sizeof(g_status_state), json_object_get_string(val));
    }

    copy_json_string(g_status_title, sizeof(g_status_title), root, "title", "");
    copy_json_string(g_status_artist, sizeof(g_status_artist), root, "artist", "");
    copy_json_string(g_status_album, sizeof(g_status_album), root, "album", "");
    copy_json_string(g_status_albumart, sizeof(g_status_albumart), root, "albumart", "");
    copy_json_string(g_status_track_type, sizeof(g_status_track_type), root, "trackType", "");
    copy_json_string(g_status_samplerate, sizeof(g_status_samplerate), root, "samplerate", "");
    copy_json_string(g_status_bitdepth, sizeof(g_status_bitdepth), root, "bitdepth", "");

    if (json_object_object_get_ex(root, "seek", &val)) {
        int seek_ms = json_object_get_int(val);
        g_status_seek = seek_ms / 1000;
    }

    if (json_object_object_get_ex(root, "duration", &val)) {
        g_status_duration = json_object_get_int(val);
    }

    int parsed_volume = 0;
    if (extract_volume_from_state(root, &parsed_volume)) {
        note_volume_state(parsed_volume, now);
    }

    g_status_last_updated = monotonic_seconds();
    json_object_put(root);

    suppress_metadata_less_playback_if_latched(now);

    if (strcmp(old_state, g_status_state) != 0 ||
        strcmp(old_title, g_status_title) != 0 ||
        strcmp(old_artist, g_status_artist) != 0) {
        DBG_LOG("Volumio state: state=%s title='%s' artist='%s' album='%s' seek=%d duration=%d volume=%d trackType='%s' samplerate='%s' bitdepth='%s' albumart='%s'",
                g_status_state, g_status_title, g_status_artist, g_status_album,
                g_status_seek, g_status_duration, g_status_volume, g_status_track_type, g_status_samplerate,
                g_status_bitdepth, g_status_albumart);
    }

    apply_wait_timeout(now);
    update_album_art_if_needed();
}

// ---------------- Drawing ----------------

static void draw_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_img || w <= 0 || h <= 0) return;

    int x0 = clamp_int(x, 0, g_width);
    int y0 = clamp_int(y, 0, g_height);
    int x1 = clamp_int(x + w, 0, g_width);
    int y1 = clamp_int(y + h, 0, g_height);

    if (x1 <= x0 || y1 <= y0) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    for (int cy = y0; cy < y1; cy++) {
        uint8_t *row_ptr = g_img + ((size_t)cy * (size_t)g_width + (size_t)x0) * 3;
        for (int cx = x0; cx < x1; cx++) {
            *row_ptr++ = r;
            *row_ptr++ = g;
            *row_ptr++ = b;
        }
    }
}

static int current_face_pixel_size(FT_Face face) {
    if (!face || !face->size) return 0;
    int y_ppem = (int)face->size->metrics.y_ppem;
    return y_ppem > 0 ? y_ppem : 0;
}

static void free_glyph_entry(GlyphCacheEntry *g) {
    if (!g) return;
    free(g->alpha);
    memset(g, 0, sizeof(*g));
}

static void free_font_atlases(void) {
    for (size_t a = 0; a < g_glyph_atlas_count; a++) {
        for (int ch = 0; ch < GLYPH_CACHE_CHARS; ch++) {
            free_glyph_entry(&g_glyph_atlases[a].glyphs[ch]);
        }
        memset(&g_glyph_atlases[a], 0, sizeof(g_glyph_atlases[a]));
    }
    g_glyph_atlas_count = 0;
}

static GlyphAtlas *get_font_atlas(FT_Face face, int pixel_size, bool create) {
    if (!face || pixel_size <= 0) return NULL;

    for (size_t i = 0; i < g_glyph_atlas_count; i++) {
        if (g_glyph_atlases[i].used &&
            g_glyph_atlases[i].face == face &&
            g_glyph_atlases[i].pixel_size == pixel_size) {
            return &g_glyph_atlases[i];
        }
    }

    if (!create || g_glyph_atlas_count >= GLYPH_ATLAS_MAX) return NULL;

    GlyphAtlas *atlas = &g_glyph_atlases[g_glyph_atlas_count++];
    memset(atlas, 0, sizeof(*atlas));
    atlas->used = true;
    atlas->face = face;
    atlas->pixel_size = pixel_size;
    return atlas;
}

static GlyphCacheEntry *cache_glyph(GlyphAtlas *atlas, unsigned char ch) {
    if (!atlas || ch >= GLYPH_CACHE_CHARS) return NULL;

    GlyphCacheEntry *entry = &atlas->glyphs[ch];
    if (entry->valid) return entry;

    FT_Set_Pixel_Sizes(atlas->face, 0, (FT_UInt)atlas->pixel_size);

    if (FT_Load_Char(atlas->face, (FT_ULong)ch, FT_LOAD_RENDER) != 0) {
        entry->valid = true;
        entry->advance = atlas->pixel_size / 2;
        return entry;
    }

    FT_GlyphSlot slot = atlas->face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;

    entry->width = (int)bitmap->width;
    entry->height = (int)bitmap->rows;
    entry->pitch = entry->width;
    entry->advance = (int)(slot->advance.x >> 6);
    entry->left = slot->bitmap_left;
    entry->top = slot->bitmap_top;
    entry->valid = true;

    if (entry->width > 0 && entry->height > 0) {
        size_t bytes = (size_t)entry->width * (size_t)entry->height;
        entry->alpha = malloc(bytes);
        if (!entry->alpha) {
            entry->width = 0;
            entry->height = 0;
            entry->pitch = 0;
            return entry;
        }

        for (int y = 0; y < entry->height; y++) {
            memcpy(entry->alpha + ((size_t)y * (size_t)entry->width),
                   bitmap->buffer + ((size_t)y * (size_t)bitmap->pitch),
                   (size_t)entry->width);
        }
    }

    return entry;
}

static GlyphCacheEntry *get_cached_glyph(FT_Face face, unsigned char ch) {
    int pixel_size = current_face_pixel_size(face);
    GlyphAtlas *atlas = get_font_atlas(face, pixel_size, true);
    if (!atlas) return NULL;
    return cache_glyph(atlas, ch);
}

static void warm_font_atlas(FT_Face face, int pixel_size) {
    if (!face || pixel_size <= 0) return;

    static const unsigned char preload[] =
        " 0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        ":/-. ,()[]'\"&+#%_|!?@";

    GlyphAtlas *atlas = get_font_atlas(face, pixel_size, true);
    if (!atlas) return;

    for (size_t i = 0; preload[i]; i++) {
        cache_glyph(atlas, preload[i]);
    }
}

static void warm_common_font_atlases(void) {
    warm_font_atlas(g_font_bold, scale_font(130));
    warm_font_atlas(g_font_bold, scale_font(70));
    warm_font_atlas(g_font_bold, scale_font(26));
    warm_font_atlas(g_font_reg, scale_font(22));
    warm_font_atlas(g_font_reg, scale_font(20));
    warm_font_atlas(g_font_reg, scale_font(16));
    warm_font_atlas(g_font_reg, scale_font(11));
}

static void blend_alpha_rgb(uint8_t *dst, uint8_t r_text, uint8_t g_text, uint8_t b_text, uint8_t alpha) {
    if (alpha == 0) return;

    if (alpha == 255) {
        dst[0] = r_text;
        dst[1] = g_text;
        dst[2] = b_text;
        return;
    }

    uint16_t inv = (uint16_t)(255 - alpha);
    dst[0] = fast_div_255((uint16_t)alpha * r_text + inv * dst[0]);
    dst[1] = fast_div_255((uint16_t)alpha * g_text + inv * dst[1]);
    dst[2] = fast_div_255((uint16_t)alpha * b_text + inv * dst[2]);
}

static void draw_cached_glyph_to_image_alpha(GlyphCacheEntry *glyph, int x_p, int y_p,
                                             int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                                             uint32_t color, uint8_t global_alpha) {
    if (!glyph || !glyph->alpha || glyph->width <= 0 || glyph->height <= 0) return;
    if (global_alpha == 0) return;

    uint8_t r_text = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g_text = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b_text = (uint8_t)(color & 0xFF);

    for (int row = 0; row < glyph->height; row++) {
        int ty = y_p + row;
        if (ty < clip_y0 || ty >= clip_y1 || ty < 0 || ty >= g_height) continue;

        for (int col = 0; col < glyph->width; col++) {
            int tx = x_p + col;
            if (tx < clip_x0 || tx >= clip_x1 || tx < 0 || tx >= g_width) continue;

            uint8_t alpha = glyph->alpha[(size_t)row * (size_t)glyph->width + (size_t)col];
            if (alpha == 0) continue;

            if (global_alpha != 255) {
                alpha = fast_div_255((uint16_t)alpha * (uint16_t)global_alpha);
                if (alpha == 0) continue;
            }

            size_t idx = ((size_t)ty * (size_t)g_width + (size_t)tx) * 3;
            blend_alpha_rgb(&g_img[idx], r_text, g_text, b_text, alpha);
        }
    }
}

static void draw_text_clipped_alpha(FT_Face face, const char *text, int x_start, int y_baseline,
                                    int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                                    uint32_t color, uint8_t global_alpha) {
    if (!face || !text || !*text) return;
    if (global_alpha == 0) return;

    clip_x0 = clamp_int(clip_x0, 0, g_width);
    clip_x1 = clamp_int(clip_x1, 0, g_width);
    clip_y0 = clamp_int(clip_y0, 0, g_height);
    clip_y1 = clamp_int(clip_y1, 0, g_height);

    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;

    int x = x_start;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        GlyphCacheEntry *glyph = get_cached_glyph(face, ch);

        if (!glyph) continue;

        int x_p = x + glyph->left;
        int y_p = y_baseline - glyph->top;
        draw_cached_glyph_to_image_alpha(glyph, x_p, y_p, clip_x0, clip_y0, clip_x1, clip_y1, color, global_alpha);
        x += glyph->advance;
    }
}

static void draw_text_clipped(FT_Face face, const char *text, int x_start, int y_baseline,
                              int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                              uint32_t color) {
    draw_text_clipped_alpha(face, text, x_start, y_baseline, clip_x0, clip_y0, clip_x1, clip_y1, color, 255);
}

static void draw_text_alpha(FT_Face face, const char *text, int x_start, int y_baseline,
                            uint32_t color, uint8_t global_alpha) {
    draw_text_clipped_alpha(face, text, x_start, y_baseline, 0, 0, g_width, g_height, color, global_alpha);
}

static void draw_text(FT_Face face, const char *text, int x_start, int y_baseline, uint32_t color) {
    draw_text_alpha(face, text, x_start, y_baseline, color, 255);
}

static void draw_text_to_rgb_buffer(FT_Face face, const char *text,
                                    uint8_t *dst, int dst_w, int dst_h,
                                    int x_start, int y_baseline,
                                    uint32_t color) {
    if (!face || !text || !*text || !dst || dst_w <= 0 || dst_h <= 0) return;

    uint8_t r_text = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g_text = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b_text = (uint8_t)(color & 0xFF);

    int x = x_start;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        GlyphCacheEntry *glyph = get_cached_glyph(face, ch);
        if (!glyph) continue;

        int x_p = x + glyph->left;
        int y_p = y_baseline - glyph->top;

        if (glyph->alpha && glyph->width > 0 && glyph->height > 0) {
            for (int row = 0; row < glyph->height; row++) {
                int ty = y_p + row;
                if (ty < 0 || ty >= dst_h) continue;

                for (int col = 0; col < glyph->width; col++) {
                    int tx = x_p + col;
                    if (tx < 0 || tx >= dst_w) continue;

                    uint8_t alpha = glyph->alpha[(size_t)row * (size_t)glyph->width + (size_t)col];
                    if (alpha == 0) continue;

                    size_t idx = ((size_t)ty * (size_t)dst_w + (size_t)tx) * 3;
                    blend_alpha_rgb(&dst[idx], r_text, g_text, b_text, alpha);
                }
            }
        }

        x += glyph->advance;
    }
}

static void format_duration_text(char *dst, size_t dst_sz, int seconds) {
    if (!dst || dst_sz == 0) return;

    if (seconds < 0) seconds = 0;

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int sec = seconds % 60;

    if (h > 0) snprintf(dst, dst_sz, "%d:%02d:%02d", h, m, sec);
    else snprintf(dst, dst_sz, "%d:%02d", m, sec);
}

static void build_audio_format_text(char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;

    dst[0] = '\0';

    const char *parts[3] = {
        g_status_track_type,
        g_status_samplerate,
        g_status_bitdepth
    };

    for (int i = 0; i < 3; i++) {
        const char *p = parts[i];
        if (!p || !*p) continue;

        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s", p);
        trim_in_place(tmp);
        if (!tmp[0]) continue;

        if (dst[0]) {
            size_t used = strlen(dst);
            if (used + 4 < dst_sz) {
                snprintf(dst + used, dst_sz - used, " | ");
            }
        }

        size_t used = strlen(dst);
        if (used + 1 < dst_sz) {
            snprintf(dst + used, dst_sz - used, "%s", tmp);
        }
    }
}

static int calculate_text_width(FT_Face face, const char *text) {
    if (!face || !text || !*text) return 0;

    int width = 0;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        GlyphCacheEntry *glyph = get_cached_glyph(face, ch);
        if (!glyph) continue;
        width += glyph->advance;
    }

    return width;
}

static int text_top_offset_from_baseline(FT_Face face, const char *text) {
    if (!face || !text || !*text) return 0;

    int min_top = 0;
    bool seen = false;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        GlyphCacheEntry *glyph = get_cached_glyph(face, ch);
        if (!glyph) continue;

        int top = -glyph->top;
        if (!seen || top < min_top) {
            min_top = top;
            seen = true;
        }
    }

    if (!seen && face->size) {
        return -(int)(face->size->metrics.ascender >> 6);
    }

    return min_top;
}

static void draw_text_centered(FT_Face face, const char *text, int x0, int x1, int baseline_y, uint32_t color) {
    int w = calculate_text_width(face, text ? text : "");
    int x = x0 + ((x1 - x0 - w) / 2);
    draw_text(face, text ? text : "", x, baseline_y, color);
}

static void free_scroll_strip(void) {
    free(g_scroll_strip_rgb);
    g_scroll_strip_rgb = NULL;
    g_scroll_strip_w = 0;
    g_scroll_strip_h = 0;
    g_scroll_strip_cycle_w = 0;
    g_scroll_strip_font_size = 0;
    g_scroll_strip_clip_w = 0;
    g_scroll_strip_clip_h = 0;
    g_scroll_strip_baseline = 0;
    g_scroll_strip_gap = 0;
    g_scroll_strip_color = 0;
    g_scroll_strip_face = NULL;
    g_scroll_strip_text[0] = '\0';
    g_scroll_strip_started_at = 0.0;
}

static bool ensure_scroll_strip(FT_Face face, const char *text, int font_size,
                                int clip_w, int clip_h, int baseline_in_strip,
                                int gap, uint32_t color, int text_w) {
    if (!face || !text || !*text || font_size <= 0 || clip_w <= 0 || clip_h <= 0 || text_w <= 0) {
        free_scroll_strip();
        return false;
    }

    int cycle_w = text_w + gap;
    if (cycle_w <= 0) return false;

    if (g_scroll_strip_rgb &&
        g_scroll_strip_face == face &&
        g_scroll_strip_font_size == font_size &&
        g_scroll_strip_clip_w == clip_w &&
        g_scroll_strip_clip_h == clip_h &&
        g_scroll_strip_baseline == baseline_in_strip &&
        g_scroll_strip_gap == gap &&
        g_scroll_strip_color == color &&
        strcmp(g_scroll_strip_text, text) == 0) {
        return true;
    }

    free_scroll_strip();

    g_scroll_strip_w = cycle_w + clip_w + 2;
    g_scroll_strip_h = clip_h;
    g_scroll_strip_cycle_w = cycle_w;
    g_scroll_strip_font_size = font_size;
    g_scroll_strip_clip_w = clip_w;
    g_scroll_strip_clip_h = clip_h;
    g_scroll_strip_baseline = baseline_in_strip;
    g_scroll_strip_gap = gap;
    g_scroll_strip_color = color;
    g_scroll_strip_face = face;
    snprintf(g_scroll_strip_text, sizeof(g_scroll_strip_text), "%s", text);
    g_scroll_strip_started_at = monotonic_seconds();

    size_t bytes = (size_t)g_scroll_strip_w * (size_t)g_scroll_strip_h * 3u;
    g_scroll_strip_rgb = malloc(bytes);
    if (!g_scroll_strip_rgb) {
        free_scroll_strip();
        return false;
    }

    uint8_t bg_r = (uint8_t)((BG_COLOR >> 16) & 0xFF);
    uint8_t bg_g = (uint8_t)((BG_COLOR >> 8) & 0xFF);
    uint8_t bg_b = (uint8_t)(BG_COLOR & 0xFF);

    for (size_t i = 0; i < bytes; i += 3) {
        g_scroll_strip_rgb[i + 0] = bg_r;
        g_scroll_strip_rgb[i + 1] = bg_g;
        g_scroll_strip_rgb[i + 2] = bg_b;
    }

    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
    draw_text_to_rgb_buffer(face, text, g_scroll_strip_rgb, g_scroll_strip_w, g_scroll_strip_h,
                            0, baseline_in_strip, color);
    draw_text_to_rgb_buffer(face, text, g_scroll_strip_rgb, g_scroll_strip_w, g_scroll_strip_h,
                            cycle_w, baseline_in_strip, color);

    return true;
}

static void draw_scroll_strip_window(int dst_x, int dst_y, int clip_w, int clip_h, int offset) {
    if (!g_scroll_strip_rgb || !g_img || clip_w <= 0 || clip_h <= 0) return;
    if (g_scroll_strip_cycle_w <= 0) return;

    offset %= g_scroll_strip_cycle_w;
    if (offset < 0) offset += g_scroll_strip_cycle_w;

    for (int y = 0; y < clip_h; y++) {
        int out_y = dst_y + y;
        if (out_y < 0 || out_y >= g_height) continue;

        int out_x = dst_x;
        int copy_w = clip_w;
        int src_x = offset;

        if (out_x < 0) {
            int shift = -out_x;
            src_x += shift;
            copy_w -= shift;
            out_x = 0;
        }

        if (out_x + copy_w > g_width) {
            copy_w = g_width - out_x;
        }

        if (copy_w <= 0) continue;
        if (src_x + copy_w > g_scroll_strip_w) {
            copy_w = g_scroll_strip_w - src_x;
        }
        if (copy_w <= 0) continue;

        size_t src_idx = ((size_t)y * (size_t)g_scroll_strip_w + (size_t)src_x) * 3u;
        size_t dst_idx = ((size_t)out_y * (size_t)g_width + (size_t)out_x) * 3u;
        memcpy(g_img + dst_idx, g_scroll_strip_rgb + src_idx, (size_t)copy_w * 3u);
    }
}

// Generates the scaled album art cache once
static void scale_art_once(int target_w, int target_h) {
    if (g_cached_art_w == target_w && g_cached_art_h == target_h && g_art_scaled_rgb) return;
    free_scaled_art();
    
    if (!g_art_rgba || g_art_w <= 0 || g_art_h <= 0 || target_w <= 0 || target_h <= 0) return;

    g_art_scaled_rgb = malloc((size_t)target_w * (size_t)target_h * 3);
    if (!g_art_scaled_rgb) return;

    g_cached_art_w = target_w;
    g_cached_art_h = target_h;

    int x_step = (target_w > 1) ? (int)(((int64_t)(g_art_w - 1) << 16) / (target_w - 1)) : 0;
    int y_step = (target_h > 1) ? (int)(((int64_t)(g_art_h - 1) << 16) / (target_h - 1)) : 0;

    int src_y_fp = 0;

    for (int y = 0; y < target_h; y++) {
        int y0 = src_y_fp >> 16;
        int y_frac = src_y_fp & 0xFFFF;
        if (y0 >= g_art_h) y0 = g_art_h - 1;
        int y1 = (y0 + 1 >= g_art_h) ? g_art_h - 1 : y0 + 1;

        uint32_t wy1 = (uint32_t)y_frac;
        uint32_t wy0 = (uint32_t)(FIXED_ONE - y_frac);

        int src_x_fp = 0;
        size_t dst_row_idx = (size_t)y * target_w * 3;

        for (int x = 0; x < target_w; x++) {
            int x0 = src_x_fp >> 16;
            int x_frac = src_x_fp & 0xFFFF;
            if (x0 >= g_art_w) x0 = g_art_w - 1;
            int x1 = (x0 + 1 >= g_art_w) ? g_art_w - 1 : x0 + 1;

            uint32_t wx1 = (uint32_t)x_frac;
            uint32_t wx0 = (uint32_t)(FIXED_ONE - x_frac);

            uint64_t w00 = (uint64_t)wx0 * wy0;
            uint64_t w10 = (uint64_t)wx1 * wy0;
            uint64_t w01 = (uint64_t)wx0 * wy1;
            uint64_t w11 = (uint64_t)wx1 * wy1;

            size_t idx_00 = ((size_t)y0 * g_art_w + x0) * 4;
            size_t idx_10 = ((size_t)y0 * g_art_w + x1) * 4;
            size_t idx_01 = ((size_t)y1 * g_art_w + x0) * 4;
            size_t idx_11 = ((size_t)y1 * g_art_w + x1) * 4;

            for (int c = 0; c < 3; c++) {
                uint64_t value =
                    ((uint64_t)g_art_rgba[idx_00 + c] * w00) +
                    ((uint64_t)g_art_rgba[idx_10 + c] * w10) +
                    ((uint64_t)g_art_rgba[idx_01 + c] * w01) +
                    ((uint64_t)g_art_rgba[idx_11 + c] * w11);

                g_art_scaled_rgb[dst_row_idx + (x * 3) + c] = (uint8_t)((value + (1ULL << 31)) >> 32);
            }
            src_x_fp += x_step;
        }
        src_y_fp += y_step;
    }
}

// Blits pre-scaled cache directly to the frame 
static void draw_album_art_cached(int target_x, int target_y, int target_w, int target_h) {
    scale_art_once(target_w, target_h);
    if (!g_art_scaled_rgb) return;

    for (int y = 0; y < target_h; y++) {
        int out_y = target_y + y;
        if (out_y < 0 || out_y >= g_height) continue;

        int out_x = target_x;
        int copy_w = target_w;
        int src_offset_pixels = 0;

        if (out_x < 0) {
            copy_w += out_x;
            src_offset_pixels = -out_x;
            out_x = 0;
        }
        if (out_x + copy_w > g_width) {
            copy_w = g_width - out_x;
        }

        if (copy_w > 0) {
            size_t src_idx = ((size_t)y * target_w + src_offset_pixels) * 3;
            size_t dst_idx = ((size_t)out_y * g_width + out_x) * 3;
            memcpy(&g_img[dst_idx], &g_art_scaled_rgb[src_idx], (size_t)copy_w * 3);
        }
    }
}

// ---------------- Framebuffer ----------------

static uint32_t scale_channel_to_length(uint8_t v, uint32_t length) {
    if (length == 0) return 0;
    if (length >= 8) return (uint32_t)v << (length - 8);

    uint32_t max_v = (1u << length) - 1u;
    return ((uint32_t)v * max_v + 127u) / 255u;
}

static void init_pixel_luts(void) {
    for (int i = 0; i < 256; i++) {
        g_r_lut[i] = scale_channel_to_length((uint8_t)i, g_vinfo.red.length) << g_vinfo.red.offset;
        g_g_lut[i] = scale_channel_to_length((uint8_t)i, g_vinfo.green.length) << g_vinfo.green.offset;
        g_b_lut[i] = scale_channel_to_length((uint8_t)i, g_vinfo.blue.length) << g_vinfo.blue.offset;
    }
}

static inline uint32_t rgb_to_native_pixel(uint8_t r, uint8_t g, uint8_t b) {
    return g_r_lut[r] | g_g_lut[g] | g_b_lut[b];
}

static void set_default_fb_masks_if_missing(void) {
    if (g_vinfo.red.length && g_vinfo.green.length && g_vinfo.blue.length) return;

    memset(&g_vinfo.red, 0, sizeof(g_vinfo.red));
    memset(&g_vinfo.green, 0, sizeof(g_vinfo.green));
    memset(&g_vinfo.blue, 0, sizeof(g_vinfo.blue));

    if (g_bpp == 16) {
        g_vinfo.red.offset = 11;
        g_vinfo.red.length = 5;
        g_vinfo.green.offset = 5;
        g_vinfo.green.length = 6;
        g_vinfo.blue.offset = 0;
        g_vinfo.blue.length = 5;
    } else {
        g_vinfo.red.offset = 16;
        g_vinfo.red.length = 8;
        g_vinfo.green.offset = 8;
        g_vinfo.green.length = 8;
        g_vinfo.blue.offset = 0;
        g_vinfo.blue.length = 8;
    }
}

static bool open_framebuffer(void) {
    g_fb_fd = open(g_cfg_fb_path, O_RDWR | O_CLOEXEC);
    if (g_fb_fd < 0) {
        perror("[FB Error] open");
        DBG_LOG("Framebuffer open failed path=%s errno=%d", g_cfg_fb_path, errno);
        return false;
    }

    memset(&g_vinfo, 0, sizeof(g_vinfo));
    memset(&g_finfo, 0, sizeof(g_finfo));

    if (ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) == 0) {
        g_width = (int)g_vinfo.xres;
        g_height = (int)g_vinfo.yres;
        g_bpp = (int)g_vinfo.bits_per_pixel;
    } else {
        printf("[FB Warning] FBIOGET_VSCREENINFO failed. Falling back to config geometry.\n");
        DBG_LOG("FBIOGET_VSCREENINFO failed errno=%d, fallback=%dx%d", errno, g_cfg_width, g_cfg_height);
        g_width = g_cfg_width;
        g_height = g_cfg_height;
        g_bpp = 16;
    }

    if (g_width <= 0) g_width = DESIGN_WIDTH;
    if (g_height <= 0) g_height = DESIGN_HEIGHT;
    if (g_bpp <= 0) g_bpp = 16;

    if (g_bpp != 16 && g_bpp != 24 && g_bpp != 32) {
        fprintf(stderr, "[FB Error] Unsupported framebuffer depth: %d bpp. Supported: 16, 24, 32.\n", g_bpp);
        DBG_LOG("Unsupported framebuffer depth: %d", g_bpp);
        close(g_fb_fd);
        g_fb_fd = -1;
        return false;
    }

    g_bytes_per_pixel = (g_bpp + 7) / 8;

    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_finfo) == 0 && g_finfo.line_length > 0) {
        g_line_length = (int)g_finfo.line_length;
    } else {
        g_line_length = g_width * g_bytes_per_pixel;
        printf("[FB Warning] FBIOGET_FSCREENINFO failed. Assuming stride %d.\n", g_line_length);
        DBG_LOG("FBIOGET_FSCREENINFO failed errno=%d assumed_stride=%d", errno, g_line_length);
    }

    set_default_fb_masks_if_missing();
    init_pixel_luts(); 

    g_fb_map_bytes = (size_t)g_line_length * (size_t)g_height;

    g_fb_mem = mmap(NULL, g_fb_map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb_mem == MAP_FAILED) {
        perror("[FB Error] mmap");
        g_fb_mem = NULL;
        close(g_fb_fd);
        g_fb_fd = -1;
        return false;
    }

    printf("[FB Discovery] %dx%d, %d bpp, stride %d bytes, framebuffer %s\n",
           g_width, g_height, g_bpp, g_line_length, g_cfg_fb_path);
    DBG_LOG("Framebuffer opened: %dx%d bpp=%d stride=%d path=%s",
            g_width, g_height, g_bpp, g_line_length, g_cfg_fb_path);

    return true;
}

static void close_framebuffer(void) {
    if (g_fb_mem && g_fb_mem != MAP_FAILED) {
        munmap(g_fb_mem, g_fb_map_bytes);
        g_fb_mem = NULL;
    }

    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
}

static void blank_physical_framebuffer(uint32_t color) {
    if (!g_fb_mem || g_fb_mem == MAP_FAILED) return;
    if (g_width <= 0 || g_height <= 0 || g_line_length <= 0 || g_bytes_per_pixel <= 0) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint32_t native = rgb_to_native_pixel(r, g, b);

    for (int y = 0; y < g_height; y++) {
        uint8_t *row = g_fb_mem + ((size_t)y * (size_t)g_line_length);
        for (int x = 0; x < g_width; x++) {
            uint8_t *dst = row + ((size_t)x * (size_t)g_bytes_per_pixel);
            memcpy(dst, &native, (size_t)g_bytes_per_pixel);
        }
    }

    if (g_fb_map_bytes > 0) {
        (void)msync(g_fb_mem, g_fb_map_bytes, MS_ASYNC);
    }

    size_t total_pixels = (size_t)g_width * (size_t)g_height;
    if (g_img) memset(g_img, 0, total_pixels * 3);
    if (g_native) memset(g_native, 0, total_pixels * sizeof(uint32_t));
    if (g_prev_native) memset(g_prev_native, 0, total_pixels * sizeof(uint32_t));
    if (g_native16) memset(g_native16, 0, total_pixels * sizeof(uint16_t));
    if (g_prev_native16) memset(g_prev_native16, 0, total_pixels * sizeof(uint16_t));
    g_prev_frame_valid = false;
}

static void build_native_from_rgb_frame(void) {
    if (!g_img) return;

    size_t total_pixels = (size_t)g_width * (size_t)g_height;

    if (g_bpp == 16 && g_native16) {
        for (size_t i = 0; i < total_pixels; i++) {
            size_t img_idx = i * 3u;
            g_native16[i] = (uint16_t)rgb_to_native_pixel(g_img[img_idx + 0], g_img[img_idx + 1], g_img[img_idx + 2]);
        }
        return;
    }

    if (!g_native) return;

    for (size_t i = 0; i < total_pixels; i++) {
        size_t img_idx = i * 3u;
        g_native[i] = rgb_to_native_pixel(g_img[img_idx + 0], g_img[img_idx + 1], g_img[img_idx + 2]);
    }
}

static void write_native_span_to_fb(int y, int start_x, int end_x) {
    if (!g_fb_mem) return;
    if (y < 0 || y >= g_height) return;

    start_x = clamp_int(start_x, 0, g_width - 1);
    end_x = clamp_int(end_x, 0, g_width - 1);
    if (end_x < start_x) return;

    int length = (end_x - start_x) + 1;

    if (g_bpp == 16) {
        if (!g_native16) return;

        // True packed 16-bit path. g_native is uint32_t-per-pixel, so never cast it to uint16_t*.
        // This packed cache exists specifically so dirty spans can be copied with one memcpy per row span.
        uint16_t *fb_pixel = (uint16_t *)(g_fb_mem + ((size_t)y * (size_t)g_line_length)) + start_x;
        uint16_t *src_pixel = g_native16 + ((size_t)y * (size_t)g_width) + start_x;
        memcpy(fb_pixel, src_pixel, (size_t)length * sizeof(uint16_t));
    } else if (g_bpp == 32) {
        // 32-bit native framebuffer path. One continuous transfer for the dirty row span.
        uint32_t *fb_pixel = (uint32_t *)(g_fb_mem + ((size_t)y * (size_t)g_line_length)) + start_x;
        uint32_t *src_pixel = g_native + ((size_t)y * (size_t)g_width) + start_x;
        memcpy(fb_pixel, src_pixel, (size_t)length * sizeof(uint32_t));
    } else {
        // 24-bit and unusual packed formats. Keep the known-safe byte copy fallback.
        uint8_t *dst = g_fb_mem
            + ((size_t)y * (size_t)g_line_length)
            + ((size_t)start_x * (size_t)g_bytes_per_pixel);
        uint32_t *src = g_native + ((size_t)y * (size_t)g_width) + start_x;

        for (int x = 0; x < length; x++) {
            memcpy(dst, src, (size_t)g_bytes_per_pixel);
            dst += g_bytes_per_pixel;
            src++;
        }
    }
}

static void flush_native_to_fb(void) {
    if (g_fb_fd < 0 || !g_fb_mem || !g_img) return;
    if (g_bpp == 16) {
        if (!g_native16 || !g_prev_native16) return;
    } else {
        if (!g_native || !g_prev_native) return;
    }

    build_native_from_rgb_frame();

    if (!g_dirty_spans) {
        g_dirty_spans = malloc(sizeof(RowSpanTracker) * (size_t)g_height);
        if (!g_dirty_spans) return;
        g_prev_frame_valid = false; // Force a full screen write next loop
    }

    for (int y = 0; y < g_height; y++) {
        g_dirty_spans[y].dirty = false;
        g_dirty_spans[y].min_x = g_width;
        g_dirty_spans[y].max_x = 0;

        if (!g_prev_frame_valid) {
            g_dirty_spans[y].min_x = 0;
            g_dirty_spans[y].max_x = g_width - 1;
            g_dirty_spans[y].dirty = true;
            continue;
        }

        if (g_bpp == 16) {
            uint16_t *row_now = g_native16 + ((size_t)y * (size_t)g_width);
            uint16_t *row_prev = g_prev_native16 + ((size_t)y * (size_t)g_width);

            if (memcmp(row_now, row_prev, (size_t)g_width * sizeof(uint16_t)) == 0) {
                continue;
            }

            for (int x = 0; x < g_width; x++) {
                if (row_now[x] != row_prev[x]) {
                    g_dirty_spans[y].min_x = x;
                    g_dirty_spans[y].dirty = true;
                    break;
                }
            }

            if (g_dirty_spans[y].dirty) {
                for (int x = g_width - 1; x >= g_dirty_spans[y].min_x; x--) {
                    if (row_now[x] != row_prev[x]) {
                        g_dirty_spans[y].max_x = x;
                        break;
                    }
                }
            }
        } else {
            uint32_t *row_now = g_native + ((size_t)y * (size_t)g_width);
            uint32_t *row_prev = g_prev_native + ((size_t)y * (size_t)g_width);

            if (memcmp(row_now, row_prev, (size_t)g_width * sizeof(uint32_t)) == 0) {
                continue;
            }

            for (int x = 0; x < g_width; x++) {
                if (row_now[x] != row_prev[x]) {
                    g_dirty_spans[y].min_x = x;
                    g_dirty_spans[y].dirty = true;
                    break;
                }
            }

            if (g_dirty_spans[y].dirty) {
                for (int x = g_width - 1; x >= g_dirty_spans[y].min_x; x--) {
                    if (row_now[x] != row_prev[x]) {
                        g_dirty_spans[y].max_x = x;
                        break;
                    }
                }
            }
        }
    }

    for (int y = 0; y < g_height; y++) {
        if (!g_dirty_spans[y].dirty) continue;
        write_native_span_to_fb(y, g_dirty_spans[y].min_x, g_dirty_spans[y].max_x);
    }

    size_t total_pixels = (size_t)g_width * (size_t)g_height;
    if (g_bpp == 16 && g_prev_native16 && g_native16) {
        memcpy(g_prev_native16, g_native16, total_pixels * sizeof(uint16_t));
    } else if (g_prev_native && g_native) {
        memcpy(g_prev_native, g_native, total_pixels * sizeof(uint32_t));
    }
    g_prev_frame_valid = true;
}

static void hide_console_cursor(bool hide) {
    int fd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, hide ? "0\n" : "1\n", 2);
        close(fd);
    }

    const char *ttys[] = {"/dev/tty0", "/dev/tty1", "/dev/tty2"};
    for (size_t i = 0; i < sizeof(ttys) / sizeof(ttys[0]); i++) {
        int tty_fd = open(ttys[i], O_WRONLY | O_CLOEXEC);
        if (tty_fd >= 0) {
            (void)write(tty_fd, hide ? "\033[?25l" : "\033[?25h", 6);
            close(tty_fd);
        }
    }
}

// ---------------- UI ----------------


static bool volume_overlay_is_active(double now) {
    if (!g_status_volume_seen) return false;
    if (g_volume_overlay_last_change_at < 0.0) return false;
    if (g_cfg_wait_timeout_seconds <= 0.0) return false;

    double overlay_timeout = g_cfg_wait_timeout_seconds * 0.5;
    if (overlay_timeout < 1.0) overlay_timeout = 1.0;
    if (overlay_timeout > 4.0) overlay_timeout = 4.0;

    return (now - g_volume_overlay_last_change_at) < overlay_timeout;
}

static void dim_rgb_frame(uint8_t percent) {
    if (!g_img) return;

    if (percent > 100) percent = 100;

    size_t total_bytes = (size_t)g_width * (size_t)g_height * 3u;
    for (size_t i = 0; i < total_bytes; i++) {
        g_img[i] = (uint8_t)(((unsigned int)g_img[i] * (unsigned int)percent) / 100u);
    }
}

static bool draw_volume_overlay_if_needed(double now) {
    if (!volume_overlay_is_active(now)) return false;

    dim_rgb_frame(VOLUME_OVERLAY_DIM_PERCENT);

    double overlay_timeout = g_cfg_wait_timeout_seconds * 0.5;
    if (overlay_timeout < 1.0) overlay_timeout = 1.0;
    if (overlay_timeout > 4.0) overlay_timeout = 4.0;

    double elapsed = now - g_volume_overlay_last_change_at;
    double anim_seconds = 0.35;
    if (overlay_timeout > 0.0 && anim_seconds > overlay_timeout) anim_seconds = overlay_timeout;

    float t = 1.0f;
    if (anim_seconds > 0.0 && elapsed < anim_seconds) {
        t = (float)(elapsed / anim_seconds);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        t = t * t * (3.0f - (2.0f * t));
    }

    int start_vol = g_volume_overlay_start_volume >= 0 ? g_volume_overlay_start_volume : g_status_volume;
    int vol = lerp_int_simple(clamp_int(start_vol, 0, 100), clamp_int(g_status_volume, 0, 100), t);

    int margin = scale_x(28);
    int bar_w = scale_x(34);
    int bar_h = g_height - scale_y(76);

    if (bar_w < 12) bar_w = 12;
    if (bar_h < 80) bar_h = g_height - scale_y(32);
    if (bar_h > g_height - scale_y(32)) bar_h = g_height - scale_y(32);

    int x = g_width - margin - bar_w;
    int y = (g_height - bar_h) / 2;

    if (x < scale_x(8)) x = g_width - bar_w - scale_x(8);
    if (y < scale_y(8)) y = scale_y(8);

    draw_fill_rect(x, y, bar_w, bar_h, VOLUME_BAR_BG);

    int inner_pad = scale_x(5);
    if (inner_pad < 3) inner_pad = 3;

    int inner_x = x + inner_pad;
    int inner_y = y + inner_pad;
    int inner_w = bar_w - (inner_pad * 2);
    int inner_h = bar_h - (inner_pad * 2);

    if (inner_w < 4) inner_w = bar_w;
    if (inner_h < 4) inner_h = bar_h;

    int fill_h = (int)(((double)vol / 100.0) * (double)inner_h);
    fill_h = clamp_int(fill_h, 0, inner_h);

    if (fill_h > 0) {
        int fill_y = inner_y + inner_h - fill_h;
        draw_fill_rect(inner_x, fill_y, inner_w, fill_h, TEXT_COLOR_MAIN);
    }

    int font_size = scale_font(18);
    if (font_size < 10) font_size = 10;

    char vol_txt[16];
    snprintf(vol_txt, sizeof(vol_txt), "%d", vol);

    FT_Set_Pixel_Sizes(g_font_bold, 0, (FT_UInt)font_size);
    int text_w = calculate_text_width(g_font_bold, vol_txt);
    int text_x = x + ((bar_w - text_w) / 2);
    int text_y = y + bar_h + scale_y(22);

    if (text_y < g_height - scale_y(4)) {
        draw_text(g_font_bold, vol_txt, text_x, text_y, TEXT_COLOR_MAIN);
    }

    return true;
}

static bool draw_clock(bool *out_needs_scroll, bool *out_layout_animating, bool *out_force_full_redraw) {
    *out_needs_scroll = false;
    *out_layout_animating = false;
    *out_force_full_redraw = false;

    update_volumio_state();
    apply_wait_timeout(monotonic_seconds());

    time_t now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);

    bool show_metadata = state_shows_metadata();
    bool is_playing = (strcmp(g_status_state, "play") == 0);

    static float s_anim_progress = 0.0f;

    if (show_metadata) {
        if (s_anim_progress < 1.0f) {
            s_anim_progress += 0.08f;
            if (s_anim_progress > 1.0f) s_anim_progress = 1.0f;
            *out_layout_animating = true;
        }
    } else {
        if (s_anim_progress > 0.0f) {
            s_anim_progress -= 0.08f;
            if (s_anim_progress < 0.0f) s_anim_progress = 0.0f;
            *out_layout_animating = true;
        }
    }

    // Idle safety redraw: once per displayed minute, force the next framebuffer
    // flush to rewrite the whole screen. This keeps the idle clock simple and
    // clears any stray console/framebuffer noise without touching active playback.
    static int s_last_idle_minute_key = -1;
    bool fully_idle = (!show_metadata && s_anim_progress <= 0.01f);
    int idle_minute_key = (now_tm.tm_yday * 24 * 60) + (now_tm.tm_hour * 60) + now_tm.tm_min;

    if (fully_idle) {
        if (s_last_idle_minute_key != idle_minute_key) {
            *out_force_full_redraw = true;
            s_last_idle_minute_key = idle_minute_key;
        }
    } else {
        s_last_idle_minute_key = -1;
    }

    draw_fill_rect(0, 0, g_width, g_height, BG_COLOR);

    char time_str[16];
    if (g_cfg_clock_24h) {
        strftime(time_str, sizeof(time_str), "%H:%M", &now_tm);
    } else {
        strftime(time_str, sizeof(time_str), "%I:%M", &now_tm);
        if (time_str[0] == '0') memmove(time_str, time_str + 1, strlen(time_str));
    }

    // Smooth heartbeat colon for the large idle clock only.
    // The colon keeps its measured width, but its glyph alpha fades through a sine table.
    bool big_clock_heartbeat = (!show_metadata && s_anim_progress <= 0.01f);
    g_last_big_clock_heartbeat = big_clock_heartbeat;
    g_colon_fade_active = big_clock_heartbeat;
    update_colon_fade_alpha(monotonic_seconds());

    const int margin_x = scale_x(24);
    const int margin_y = scale_y(20);
    const int safe_bottom = scale_y(26);

    const int idle_font = scale_font(130); // 10% larger than previous 110px design size
    const int active_font = scale_font(70);
    const int cur_font = lerp_int_simple(idle_font, active_font, s_anim_progress);

    FT_Set_Pixel_Sizes(g_font_bold, 0, (FT_UInt)cur_font);
    int clock_w = calculate_text_width(g_font_bold, time_str);

    FT_Set_Pixel_Sizes(g_font_bold, 0, (FT_UInt)active_font);
    int active_clock_top_offset = text_top_offset_from_baseline(g_font_bold, time_str);
    FT_Set_Pixel_Sizes(g_font_bold, 0, (FT_UInt)cur_font);

    int idle_clock_x = (g_width - clock_w) / 2;
    int idle_clock_y = (g_height / 2) + (cur_font / 3);

    int art_size = scale_font(220);
    int max_art_by_height = g_height - scale_y(100);
    int max_art_by_width = (g_width / 2) - scale_x(20);

    if (art_size > max_art_by_height) art_size = max_art_by_height;
    if (art_size > max_art_by_width) art_size = max_art_by_width;
    if (art_size < scale_font(90)) art_size = scale_font(90);
    art_size = clamp_int(art_size, 40, min_int(g_width - scale_x(40), g_height - scale_y(80)));

    int art_target_x = margin_x;
    int art_target_y = margin_y + scale_y(10);
    int art_start_x = -(art_size + margin_x);
    int cur_art_x = lerp_int_simple(art_start_x, art_target_x, s_anim_progress);

    int text_gap = scale_x(20);
    int text_block_x = art_target_x + art_size + text_gap;
    int text_right = g_width - margin_x;

    if (text_block_x > g_width - scale_x(80)) text_block_x = margin_x;

    int active_clock_center_x = text_block_x + ((text_right - text_block_x) / 2);
    int active_clock_x = active_clock_center_x - (clock_w / 2);

    int active_clock_y = art_target_y - active_clock_top_offset;

    int cur_clock_x = lerp_int_simple(idle_clock_x, active_clock_x, s_anim_progress);
    int cur_clock_y = lerp_int_simple(idle_clock_y, active_clock_y, s_anim_progress);

    if (big_clock_heartbeat) {
        char *colon = strchr(time_str, ':');

        if (colon) {
            char hour_part[16];
            char minute_part[16];
            size_t hour_len = (size_t)(colon - time_str);

            if (hour_len >= sizeof(hour_part)) hour_len = sizeof(hour_part) - 1;
            memcpy(hour_part, time_str, hour_len);
            hour_part[hour_len] = '\0';
            snprintf(minute_part, sizeof(minute_part), "%s", colon + 1);

            int hour_w = calculate_text_width(g_font_bold, hour_part);
            int colon_w = calculate_text_width(g_font_bold, ":");
            int colon_x = cur_clock_x + hour_w;
            int minute_x = colon_x + colon_w;

            draw_text(g_font_bold, hour_part, cur_clock_x, cur_clock_y, TEXT_COLOR_MAIN);
            draw_text_alpha(g_font_bold, ":", colon_x, cur_clock_y, TEXT_COLOR_MAIN, g_colon_alpha);
            draw_text(g_font_bold, minute_part, minute_x, cur_clock_y, TEXT_COLOR_MAIN);
        } else {
            draw_text(g_font_bold, time_str, cur_clock_x, cur_clock_y, TEXT_COLOR_MAIN);
        }

        static char date_str[64] = "";
        static char ip_value[64] = "No IP";
        static char temp_str[32] = "CPU: N/A";
        static bool cpu_temp_hot = false;
        static int cached_yday = -1;
        static double last_ip_refresh = -1000.0;
        static double last_temp_refresh = -1000.0;

        double idle_now = monotonic_seconds();

        if (cached_yday != now_tm.tm_yday || date_str[0] == '\0') {
            format_idle_date(date_str, sizeof(date_str), &now_tm);
            cached_yday = now_tm.tm_yday;
        }

        if (idle_now - last_ip_refresh >= 60.0 || ip_value[0] == '\0') {
            get_primary_ipv4(ip_value, sizeof(ip_value));
            last_ip_refresh = idle_now;
        }

        if (idle_now - last_temp_refresh >= 5.0 || temp_str[0] == '\0') {
            cpu_temp_hot = format_cpu_temp_text(temp_str, sizeof(temp_str));
            last_temp_refresh = idle_now;
        }

        char ip_str[96];
        snprintf(ip_str, sizeof(ip_str), "IP: %s  ", ip_value);

        FT_Set_Pixel_Sizes(g_font_reg, 0, (FT_UInt)scale_font(22));
        draw_text_centered(g_font_reg, date_str,
                           margin_x, g_width - margin_x,
                           scale_y(44), TEXT_COLOR_DIM);

        FT_Set_Pixel_Sizes(g_font_reg, 0, (FT_UInt)scale_font(16));
        int ip_w = calculate_text_width(g_font_reg, ip_str);
        int temp_w = calculate_text_width(g_font_reg, temp_str);
        int bottom_text_x = margin_x + ((g_width - (margin_x * 2) - ip_w - temp_w) / 2);
        int bottom_text_y = g_height - scale_y(28);
        draw_text(g_font_reg, ip_str, bottom_text_x, bottom_text_y, 0x666666);
        draw_text(g_font_reg, temp_str, bottom_text_x + ip_w, bottom_text_y,
                  cpu_temp_hot ? 0xFF0000 : 0x666666);
    } else {
        draw_text(g_font_bold, time_str, cur_clock_x, cur_clock_y, TEXT_COLOR_MAIN);
    }

    if (s_anim_progress > 0.01f) {
        int title_size = scale_font(26);
        int artist_size = scale_font(20);
        int small_size = scale_font(16);

        if (g_art_rgba) {
            draw_album_art_cached(cur_art_x, art_target_y, art_size, art_size);
        } else {
            draw_fill_rect(cur_art_x, art_target_y, art_size, art_size, PROGRESS_BAR_BG);
            FT_Set_Pixel_Sizes(g_font_reg, 0, (FT_UInt)small_size);
            draw_text_centered(g_font_reg, "No Art", cur_art_x, cur_art_x + art_size,
                               art_target_y + (art_size / 2), TEXT_COLOR_DIM);
        }

        int title_y = art_target_y + scale_y(105);
        int artist_y = title_y + scale_y(40);
        int album_y = artist_y + scale_y(40);

        FT_Set_Pixel_Sizes(g_font_bold, 0, (FT_UInt)title_size);
        int max_text_w = text_right - text_block_x;
        if (max_text_w < scale_x(80)) max_text_w = g_width - (margin_x * 2);

        int title_w = calculate_text_width(g_font_bold, g_status_title);

        if (title_w > max_text_w) {
            *out_needs_scroll = is_playing;

            static double s_scroll_start = 0.0;
            static char s_last_scroll_title[256] = "";

            if (strcmp(s_last_scroll_title, g_status_title) != 0) {
                snprintf(s_last_scroll_title, sizeof(s_last_scroll_title), "%s", g_status_title);
                s_scroll_start = monotonic_seconds();
            }

            if (is_playing) {
                int scroll_gap = scale_x(80);
                if (scroll_gap < 32) scroll_gap = 32;

                int clip_x0 = text_block_x;
                int clip_y0 = title_y - scale_y(34);
                int clip_x1 = text_right;
                int clip_y1 = title_y + scale_y(12);
                int clip_w = clip_x1 - clip_x0;
                int clip_h = clip_y1 - clip_y0;
                int baseline_in_strip = title_y - clip_y0;

                bool strip_ok = ensure_scroll_strip(g_font_bold, g_status_title, title_size,
                                                    clip_w, clip_h, baseline_in_strip,
                                                    scroll_gap, TEXT_COLOR_MAIN, title_w);

                double elapsed = strip_ok ? (monotonic_seconds() - g_scroll_strip_started_at)
                                         : (monotonic_seconds() - s_scroll_start);
                int cycle_w = strip_ok ? g_scroll_strip_cycle_w : (title_w + scroll_gap);
                int offset = 0;

                if (cycle_w > 0) {
                    offset = (int)fmod(elapsed * (double)scale_x(30), (double)cycle_w);
                }

                if (strip_ok) {
                    draw_scroll_strip_window(clip_x0, clip_y0, clip_w, clip_h, offset);
                } else {
                    int first_x = text_block_x - offset;
                    int second_x = first_x + cycle_w;

                    draw_text_clipped(g_font_bold, g_status_title, first_x, title_y,
                                      text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                                      TEXT_COLOR_MAIN);

                    draw_text_clipped(g_font_bold, g_status_title, second_x, title_y,
                                      text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                                      TEXT_COLOR_MAIN);
                }
            } else {
                draw_text_clipped(g_font_bold, g_status_title, text_block_x, title_y,
                                  text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                                  TEXT_COLOR_MAIN);
            }
        } else {
            draw_text_clipped(g_font_bold, g_status_title, text_block_x, title_y,
                              text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                              TEXT_COLOR_MAIN);
        }

        FT_Set_Pixel_Sizes(g_font_reg, 0, (FT_UInt)artist_size);
        draw_text_clipped(g_font_reg, g_status_artist, text_block_x, artist_y,
                          text_block_x, artist_y - scale_y(28), text_right, artist_y + scale_y(10),
                          TEXT_COLOR_DIM);
        draw_text_clipped(g_font_reg, g_status_album, text_block_x, album_y,
                          text_block_x, album_y - scale_y(28), text_right, album_y + scale_y(10),
                          TEXT_COLOR_DIM);

        int pb_x = margin_x;
        int pb_w = g_width - (margin_x * 2);
        int pb_h = scale_y(8);
        if (pb_h < 3) pb_h = 3;

        int progress_font = scale_font(11);
        if (progress_font < 8) progress_font = 8;

        int pb_y = g_height - safe_bottom - scale_y(30);
        if (pb_y < album_y + scale_y(16)) pb_y = album_y + scale_y(16);
        if (pb_y + pb_h + scale_y(18) > g_height) pb_y = g_height - scale_y(28);

        draw_fill_rect(pb_x, pb_y, pb_w, pb_h, PROGRESS_BAR_BG);

        if (g_status_duration > 0) {
            int seek = clamp_int(g_status_seek, 0, g_status_duration);
            int fill_w = (int)(((double)seek / (double)g_status_duration) * (double)pb_w);
            fill_w = clamp_int(fill_w, 0, pb_w);
            if (fill_w > 0) draw_fill_rect(pb_x, pb_y, fill_w, pb_h, PROGRESS_BAR_FG);

            char elapsed_txt[32];
            char duration_txt[32];
            format_duration_text(elapsed_txt, sizeof(elapsed_txt), seek);
            format_duration_text(duration_txt, sizeof(duration_txt), g_status_duration);

            FT_Set_Pixel_Sizes(g_font_reg, 0, (FT_UInt)progress_font);

            int time_y = pb_y + pb_h + scale_y(14);
            if (time_y > g_height - scale_y(4)) time_y = g_height - scale_y(4);

            draw_text(g_font_reg, elapsed_txt, pb_x, time_y, TEXT_COLOR_DIM);

            int duration_w = calculate_text_width(g_font_reg, duration_txt);
            draw_text(g_font_reg, duration_txt, pb_x + pb_w - duration_w, time_y, TEXT_COLOR_DIM);

            char audio_format_txt[192];
            build_audio_format_text(audio_format_txt, sizeof(audio_format_txt));
            if (audio_format_txt[0]) {
                int left_time_w = calculate_text_width(g_font_reg, elapsed_txt);
                int center_left = pb_x + left_time_w + scale_x(12);
                int center_right = pb_x + pb_w - duration_w - scale_x(12);

                if (center_right > center_left + scale_x(40)) {
                    draw_text_centered(g_font_reg, audio_format_txt, center_left, center_right,
                                       time_y, TEXT_COLOR_DIM);
                }
            }
        }
    }

    if (draw_volume_overlay_if_needed(monotonic_seconds())) {
        *out_layout_animating = true;
    }

    return is_playing;
}

// ---------------- Init / Cleanup ----------------

static bool allocate_surfaces(void) {
    size_t pixels = (size_t)g_width * (size_t)g_height;

    g_img = malloc(pixels * 3);
    g_native = malloc(pixels * sizeof(uint32_t));
    g_prev_native = calloc(pixels, sizeof(uint32_t));

    if (g_bpp == 16) {
        g_native16 = malloc(pixels * sizeof(uint16_t));
        g_prev_native16 = calloc(pixels, sizeof(uint16_t));
    }

    if (!g_img || !g_native || !g_prev_native || (g_bpp == 16 && (!g_native16 || !g_prev_native16))) {
        fprintf(stderr, "[Memory Error] Failed to allocate render buffers for %dx%d.\n", g_width, g_height);
        DBG_LOG("Render buffer allocation failed for %dx%d", g_width, g_height);
        return false;
    }

    return true;
}

static bool init_all(void) {
    load_json_config();
    debug_log_open();
    DBG_LOG("Startup: config=%s fb_path=%s debug=%s log_path=%s max_bytes=%ld",
            CONFIG_PATH, g_cfg_fb_path, g_cfg_debug_enabled ? "true" : "false",
            g_cfg_debug_log_path, g_cfg_debug_log_max_bytes);
    DBG_LOG("Volumio config: wait_timeout=%.2f", g_cfg_wait_timeout_seconds);

    if (FT_Init_FreeType(&g_ft) != 0) {
        fprintf(stderr, "[Font Error] FreeType init failed.\n");
        return false;
    }

    if (FT_New_Face(g_ft, FONT_BOLD, 0, &g_font_bold) != 0) {
        fprintf(stderr, "[Font Error] Missing bold font: %s\n", FONT_BOLD);
        return false;
    }

    if (FT_New_Face(g_ft, FONT_REG, 0, &g_font_reg) != 0) {
        fprintf(stderr, "[Font Error] Missing regular font: %s\n", FONT_REG);
        return false;
    }

    if (!open_framebuffer()) return false;
    if (!allocate_surfaces()) return false;
    warm_common_font_atlases();

    return true;
}

static void cleanup_all(void) {
    DBG_LOG("Shutdown requested");
    close_http_keepalive();

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }
    
    free_scaled_art();
    free_scroll_strip();

    free(g_http_buffer);
    g_http_buffer = NULL;

    free(g_img);
    g_img = NULL;

    free(g_native);
    g_native = NULL;

    free(g_prev_native);
    g_prev_native = NULL;

    free(g_native16);
    g_native16 = NULL;

    free(g_prev_native16);
    g_prev_native16 = NULL;

    free(g_dirty_spans);
    g_dirty_spans = NULL;

    free_font_atlases();

    if (g_font_bold) {
        FT_Done_Face(g_font_bold);
        g_font_bold = NULL;
    }

    if (g_font_reg) {
        FT_Done_Face(g_font_reg);
        g_font_reg = NULL;
    }

    if (g_ft) {
        FT_Done_FreeType(g_ft);
        g_ft = NULL;
    }

    hide_console_cursor(false);
    close_framebuffer();
    DBG_LOG("Shutdown complete");
    debug_log_close();
}

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;

    // Wake any blocking HTTP poll/recv/send so systemd restart does not wait on it.
    int fd = g_http_fd;
    if (fd >= 0) {
        g_http_fd = -1;
        close(fd);
    }
}

static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    // Do not use SA_RESTART. We want blocking HTTP syscalls to break immediately on restart.
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int main(void) {
    install_signal_handlers();

    if (!init_all()) {
        cleanup_all();
        return 1;
    }

    hide_console_cursor(true);
    blank_physical_framebuffer(BG_COLOR);
    DBG_LOG("Initial framebuffer setup complete");

    double target_fps = HARDWARE_FPS_STATIC;
    double target_interval = 1.0 / target_fps;
    double next_frame_deadline = monotonic_seconds();

    while (g_running) {
        bool needs_scroll = false;
        bool layout_animating = false;
        bool force_full_redraw = false;

        bool is_playing = draw_clock(&needs_scroll, &layout_animating, &force_full_redraw);

        if (layout_animating || g_colon_fade_active || (needs_scroll && is_playing)) {
            // Full-rate only when something on screen actually benefits from it.
            target_fps = HARDWARE_FPS_SCROLLING;
        } else if (is_playing) {
            target_fps = HARDWARE_FPS_PLAYING;
        } else {
            // Keep the old 1Hz behaviour when heartbeat-style idle refresh is active.
            // Otherwise drop to a deep idle check window.
            target_fps = g_last_big_clock_heartbeat ? HARDWARE_FPS_STATIC : HARDWARE_FPS_SLEEP;
        }

        if (target_fps <= 0.0) target_fps = HARDWARE_FPS_STATIC;
        target_interval = 1.0 / target_fps;

        if (force_full_redraw) {
            g_prev_frame_valid = false;
        }

        flush_native_to_fb();

        next_frame_deadline += target_interval;
        double loop_end_time = monotonic_seconds();

        if (loop_end_time < next_frame_deadline) {
            sleep_seconds(next_frame_deadline - loop_end_time);
        } else {
            // We fell behind. Reset pacing so the loop does not drift or stutter trying to catch up.
            next_frame_deadline = loop_end_time;
        }
    }

    cleanup_all();
    return 0;
}
