#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <json-c/json.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#include <linux/fb.h>
#include <math.h>
#include <netinet/in.h>
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

#define DRAW_INTERVAL_IDLE 1.0
#define DRAW_INTERVAL_PLAYING 1.0
#define DRAW_INTERVAL_SCROLLING 0.05
#define DRAW_INTERVAL_ANIMATING 0.02

#define BG_COLOR 0x000000
#define TEXT_COLOR_MAIN 0xFFFFFF
#define TEXT_COLOR_DIM 0x888888
#define PROGRESS_BAR_BG 0x333333
#define PROGRESS_BAR_FG 0x00FF00

#define FONT_BOLD "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
#define FONT_REG  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

#define CONFIG_PATH "/etc/volumio_display.json"

#define HTTP_BUFFER_INITIAL (64u * 1024u)
#define HTTP_BUFFER_MAX     (4u * 1024u * 1024u)

#define FIXED_ONE 65536

#define DEFAULT_DEBUG_LOG_PATH "/var/log/volumio-clock-fb.log"
#define DEFAULT_DEBUG_LOG_MAX_BYTES (256 * 1024)
#define DEFAULT_WAIT_TIMEOUT_SECONDS 8.0

// ---------------- Runtime State ----------------

static volatile sig_atomic_t g_running = 1;

// JSON config
static char g_cfg_fb_path[256] = "/dev/fb0";
static int g_cfg_width = DESIGN_WIDTH;
static int g_cfg_height = DESIGN_HEIGHT;
static bool g_cfg_clock_24h = false;
static double g_cfg_wait_timeout_seconds = DEFAULT_WAIT_TIMEOUT_SECONDS;
static bool g_cfg_debug_enabled = false;
static char g_cfg_debug_log_path[256] = DEFAULT_DEBUG_LOG_PATH;
static long g_cfg_debug_log_max_bytes = DEFAULT_DEBUG_LOG_MAX_BYTES;
static FILE *g_debug_log_fp = NULL;

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
static uint32_t *g_native = NULL;      // native framebuffer pixel value per pixel
static uint32_t *g_prev_native = NULL; // previous native pixel value per pixel
static bool g_prev_frame_valid = false;

// FreeType
static FT_Library g_ft = NULL;
static FT_Face g_font_bold = NULL;
static FT_Face g_font_reg = NULL;

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
static double g_status_last_updated = 0.0;
static double g_no_metadata_started_at = -1.0;
static double g_status_fetch_failed_started_at = -1.0;

// Album art
static uint8_t *g_art_rgba = NULL;
static int g_art_w = 0;
static int g_art_h = 0;
static char g_art_loaded_url[512] = "";

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

// ---------------- Debug Logging ----------------

static void debug_log_close(void) {
    if (g_debug_log_fp) {
        fflush(g_debug_log_fp);
        fclose(g_debug_log_fp);
        g_debug_log_fp = NULL;
    }
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

    setvbuf(g_debug_log_fp, NULL, _IOLBF, 0);
}

static void debug_logf(const char *fmt, ...) {
    if (!g_cfg_debug_enabled || !g_debug_log_fp) return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(g_debug_log_fp, "[%s] ", stamp);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_debug_log_fp, fmt, ap);
    va_end(ap);

    fputc('\n', g_debug_log_fp);
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

static bool state_has_track_payload(void) {
    return g_status_title[0] != '\0' ||
           g_status_artist[0] != '\0' ||
           g_status_album[0] != '\0' ||
           g_status_albumart[0] != '\0' ||
           g_status_duration > 0;
}

static bool state_shows_metadata(void) {
    return strcmp(g_status_state, "play") == 0 || strcmp(g_status_state, "pause") == 0;
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
    g_no_metadata_started_at = -1.0;
    g_status_fetch_failed_started_at = -1.0;

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }
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

// ---------------- Config ----------------

static void load_json_config(void) {
    int fd = open(CONFIG_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("[Config] No config found at %s. Using defaults.\n", CONFIG_PATH);
        return;
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
            if (s && *s) snprintf(g_cfg_fb_path, sizeof(g_cfg_fb_path), "%s", s);
        }

        if (json_object_object_get_ex(display_obj, "width", &v) && v) {
            int w = json_object_get_int(v);
            if (w > 0) g_cfg_width = w;
        }

        if (json_object_object_get_ex(display_obj, "height", &v) && v) {
            int h = json_object_get_int(v);
            if (h > 0) g_cfg_height = h;
        }

        if (json_object_object_get_ex(display_obj, "default_24h_format", &v) && v) {
            g_cfg_clock_24h = json_object_get_boolean(v);
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
            if (s && *s) snprintf(g_cfg_debug_log_path, sizeof(g_cfg_debug_log_path), "%s", s);
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
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += (size_t)n;
    }

    return true;
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
    
    // Temporarily null-terminate the buffer in-place to avoid malloc
    char saved_char = headers[header_len];
    headers[header_len] = '\0';
    bool found = strcasestr((char *)headers, needle) != NULL;
    headers[header_len] = saved_char;

    return found;
}

static ssize_t header_content_length(uint8_t *headers, size_t header_len) {
    if (!headers || header_len == 0) return -1;

    // Temporarily null-terminate the buffer in-place to avoid malloc
    char saved_char = headers[header_len];
    headers[header_len] = '\0';
    
    char *p = strcasestr((char *)headers, "Content-Length:");
    ssize_t out = -1;

    if (p) {
        p += 15; // length of "Content-Length:"
        while (*p && isspace((unsigned char)*p)) p++;
        long v = strtol(p, NULL, 10);
        if (v >= 0) out = (ssize_t)v;
    }

    headers[header_len] = saved_char;
    return out;
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

    if (!connect_http_keepalive()) return NULL;

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Accept: %s\r\n"
             "Connection: keep-alive\r\n"
             "User-Agent: volumio-clock-fb/merged-compat\r\n"
             "\r\n",
             path, VOLUMIO_HOST, VOLUMIO_PORT, accept_header ? accept_header : "*/*");

    if (!send_all(g_http_fd, req, strlen(req))) {
        DBG_LOG("HTTP send failed for path %s errno=%d", path ? path : "(null)", errno);
        close_http_keepalive();
        return NULL;
    }

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
                close_http_keepalive();
                return NULL;
            }

            uint8_t *new_buf = realloc(g_http_buffer, new_cap);
            if (!new_buf) {
                DBG_LOG("HTTP buffer realloc failed for path %s requested=%zu", path ? path : "(null)", new_cap);
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
        // Always null-terminate the current buffer edge, enabling in-place string processing
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
    uint8_t *body = http_get_once(path, accept_header, out_len);
    if (body) return body;

    close_http_keepalive();
    return http_get_once(path, accept_header, out_len);
}

// ---------------- Volumio ----------------

static void normalize_albumart_path(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';

    if (!src || !*src) return;

    if (strncmp(src, "http://127.0.0.1:3000", 21) == 0) {
        snprintf(dst, dst_sz, "%s", src + 21);
        return;
    }

    if (strncmp(src, "http://localhost:3000", 21) == 0) {
        snprintf(dst, dst_sz, "%s", src + 21);
        return;
    }

    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
        return;
    }

    if (src[0] == '/') snprintf(dst, dst_sz, "%s", src);
    else snprintf(dst, dst_sz, "/%s", src);
}

static void update_album_art_if_needed(void) {
    if (!state_shows_metadata() || g_status_albumart[0] == '\0') {
        if (g_art_rgba) {
            DBG_LOG("Album art cleared: state=%s albumart='%s'", g_status_state, g_status_albumart);
            stbi_image_free(g_art_rgba);
            g_art_rgba = NULL;
        }
        g_art_loaded_url[0] = '\0';
        return;
    }

    if (strcmp(g_status_albumart, g_art_loaded_url) == 0 && g_art_rgba) return;

    char art_path[1024];
    normalize_albumart_path(art_path, sizeof(art_path), g_status_albumart);

    if (art_path[0] == '\0') {
        DBG_LOG("Album art path unsupported: raw='%s'", g_status_albumart);
        if (g_art_rgba) {
            stbi_image_free(g_art_rgba);
            g_art_rgba = NULL;
        }
        g_art_loaded_url[0] = '\0';
        return;
    }

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }

    size_t img_len = 0;
    uint8_t *img_data = http_get_localhost_body_keepalive(art_path, "image/*", &img_len);

    if (img_data && img_len > 0) {
        int comp = 0;
        g_art_rgba = stbi_load_from_memory(img_data, (int)img_len, &g_art_w, &g_art_h, &comp, 4);
        if (g_art_rgba) {
            snprintf(g_art_loaded_url, sizeof(g_art_loaded_url), "%s", g_status_albumart);
            DBG_LOG("Album art loaded: path=%s bytes=%zu size=%dx%d", art_path, img_len, g_art_w, g_art_h);
        } else {
            DBG_LOG("Album art decode failed: path=%s bytes=%zu", art_path, img_len);
            g_art_loaded_url[0] = '\0';
        }
    } else {
        DBG_LOG("Album art download failed: path=%s", art_path);
    }

    free(img_data);
}

static void update_volumio_state(void) {
    double now = monotonic_seconds();
    static double s_last_http_fetch = 0.0;

    // Rate Limit Optimization: Only hit the REST API twice a second maximum, 
    // rather than potentially up to 50 times a second during text scrolling UI updates.
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

    g_status_last_updated = monotonic_seconds();
    json_object_put(root);

    if (strcmp(old_state, g_status_state) != 0 ||
        strcmp(old_title, g_status_title) != 0 ||
        strcmp(old_artist, g_status_artist) != 0) {
        DBG_LOG("Volumio state: state=%s title='%s' artist='%s' album='%s' seek=%d duration=%d trackType='%s' samplerate='%s' bitdepth='%s' albumart='%s'",
                g_status_state, g_status_title, g_status_artist, g_status_album,
                g_status_seek, g_status_duration, g_status_track_type, g_status_samplerate,
                g_status_bitdepth, g_status_albumart);
    }

    apply_wait_timeout(now);
    update_album_art_if_needed();
}

// ---------------- Drawing ----------------

static inline void draw_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= g_width || y < 0 || y >= g_height || !g_img) return;

    size_t idx = ((size_t)y * (size_t)g_width + (size_t)x) * 3;
    g_img[idx + 0] = r;
    g_img[idx + 1] = g;
    g_img[idx + 2] = b;
}

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
        size_t row = ((size_t)cy * (size_t)g_width + (size_t)x0) * 3;
        for (int cx = x0; cx < x1; cx++) {
            g_img[row + 0] = r;
            g_img[row + 1] = g;
            g_img[row + 2] = b;
            row += 3;
        }
    }
}

static void draw_text(FT_Face face, const char *text, int x_start, int y_baseline, uint32_t color) {
    if (!face || !text || !*text) return;

    uint8_t r_text = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g_text = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b_text = (uint8_t)(color & 0xFF);

    int x = x_start;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (FT_Load_Char(face, (FT_ULong)ch, FT_LOAD_RENDER) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap *bitmap = &slot->bitmap;

        int x_p = x + slot->bitmap_left;
        int y_p = y_baseline - slot->bitmap_top;

        for (unsigned int row = 0; row < bitmap->rows; row++) {
            for (unsigned int col = 0; col < bitmap->width; col++) {
                uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                if (alpha == 0) continue;

                int tx = x_p + (int)col;
                int ty = y_p + (int)row;
                if (tx < 0 || tx >= g_width || ty < 0 || ty >= g_height) continue;

                size_t idx = ((size_t)ty * (size_t)g_width + (size_t)tx) * 3;

                if (alpha == 255) {
                    g_img[idx + 0] = r_text;
                    g_img[idx + 1] = g_text;
                    g_img[idx + 2] = b_text;
                } else {
                    uint16_t inv = (uint16_t)(255 - alpha);
                    g_img[idx + 0] = (uint8_t)(((uint16_t)alpha * r_text + inv * g_img[idx + 0]) / 255);
                    g_img[idx + 1] = (uint8_t)(((uint16_t)alpha * g_text + inv * g_img[idx + 1]) / 255);
                    g_img[idx + 2] = (uint8_t)(((uint16_t)alpha * b_text + inv * g_img[idx + 2]) / 255);
                }
            }
        }

        x += (int)(slot->advance.x >> 6);
    }
}

static void draw_text_clipped(FT_Face face, const char *text, int x_start, int y_baseline,
                              int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                              uint32_t color) {
    if (!face || !text || !*text) return;

    clip_x0 = clamp_int(clip_x0, 0, g_width);
    clip_x1 = clamp_int(clip_x1, 0, g_width);
    clip_y0 = clamp_int(clip_y0, 0, g_height);
    clip_y1 = clamp_int(clip_y1, 0, g_height);

    if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) return;

    uint8_t r_text = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g_text = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b_text = (uint8_t)(color & 0xFF);

    int x = x_start;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (FT_Load_Char(face, (FT_ULong)ch, FT_LOAD_RENDER) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        FT_Bitmap *bitmap = &slot->bitmap;

        int x_p = x + slot->bitmap_left;
        int y_p = y_baseline - slot->bitmap_top;

        for (unsigned int row = 0; row < bitmap->rows; row++) {
            for (unsigned int col = 0; col < bitmap->width; col++) {
                uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                if (alpha == 0) continue;

                int tx = x_p + (int)col;
                int ty = y_p + (int)row;

                if (tx < clip_x0 || tx >= clip_x1 || ty < clip_y0 || ty >= clip_y1) continue;
                if (tx < 0 || tx >= g_width || ty < 0 || ty >= g_height) continue;

                size_t idx = ((size_t)ty * (size_t)g_width + (size_t)tx) * 3;

                if (alpha == 255) {
                    g_img[idx + 0] = r_text;
                    g_img[idx + 1] = g_text;
                    g_img[idx + 2] = b_text;
                } else {
                    uint16_t inv = (uint16_t)(255 - alpha);
                    g_img[idx + 0] = (uint8_t)(((uint16_t)alpha * r_text + inv * g_img[idx + 0]) / 255);
                    g_img[idx + 1] = (uint8_t)(((uint16_t)alpha * g_text + inv * g_img[idx + 1]) / 255);
                    g_img[idx + 2] = (uint8_t)(((uint16_t)alpha * b_text + inv * g_img[idx + 2]) / 255);
                }
            }
        }

        x += (int)(slot->advance.x >> 6);
    }
}

static void format_duration_text(char *dst, size_t dst_sz, int seconds) {
    if (!dst || dst_sz == 0) return;

    if (seconds < 0) seconds = 0;

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    if (h > 0) snprintf(dst, dst_sz, "%d:%02d:%02d", h, m, s);
    else snprintf(dst, dst_sz, "%d:%02d", m, s);
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
        if (FT_Load_Char(face, (FT_ULong)ch, FT_LOAD_DEFAULT) != 0) continue;
        width += (int)(face->glyph->advance.x >> 6);
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
        if (FT_Load_Char(face, (FT_ULong)ch, FT_LOAD_RENDER) != 0) continue;

        int top = -face->glyph->bitmap_top;
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

static void draw_album_art_bilinear(int target_x, int target_y, int target_w, int target_h) {
    if (!g_art_rgba || g_art_w <= 0 || g_art_h <= 0 || target_w <= 0 || target_h <= 0) return;

    int x_step = 0;
    int y_step = 0;

    if (target_w > 1) x_step = (int)(((int64_t)(g_art_w - 1) << 16) / (target_w - 1));
    if (target_h > 1) y_step = (int)(((int64_t)(g_art_h - 1) << 16) / (target_h - 1));

    int src_y_fp = 0;

    for (int y = 0; y < target_h; y++) {
        int out_y = target_y + y;

        if (out_y < 0 || out_y >= g_height) {
            src_y_fp += y_step;
            continue;
        }

        int y0 = src_y_fp >> 16;
        int y_frac = src_y_fp & 0xFFFF;

        if (y0 >= g_art_h) y0 = g_art_h - 1;

        int y1 = y0 + 1;
        if (y1 >= g_art_h) y1 = g_art_h - 1;

        uint32_t wy1 = (uint32_t)y_frac;
        uint32_t wy0 = (uint32_t)(FIXED_ONE - y_frac);

        int src_x_fp = 0;

        for (int x = 0; x < target_w; x++) {
            int out_x = target_x + x;

            if (out_x < 0 || out_x >= g_width) {
                src_x_fp += x_step;
                continue;
            }

            int x0 = src_x_fp >> 16;
            int x_frac = src_x_fp & 0xFFFF;

            if (x0 >= g_art_w) x0 = g_art_w - 1;

            int x1 = x0 + 1;
            if (x1 >= g_art_w) x1 = g_art_w - 1;

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

            uint8_t out_rgb[3];

            for (int c = 0; c < 3; c++) {
                uint64_t value =
                    ((uint64_t)g_art_rgba[idx_00 + c] * w00) +
                    ((uint64_t)g_art_rgba[idx_10 + c] * w10) +
                    ((uint64_t)g_art_rgba[idx_01 + c] * w01) +
                    ((uint64_t)g_art_rgba[idx_11 + c] * w11);

                out_rgb[c] = (uint8_t)((value + (1ULL << 31)) >> 32);
            }

            draw_pixel(out_x, out_y, out_rgb[0], out_rgb[1], out_rgb[2]);
            src_x_fp += x_step;
        }

        src_y_fp += y_step;
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
    // Extremely fast lookup table driven conversion
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
    init_pixel_luts(); // Precompute colors

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
    if (!g_fb_mem) return;

    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);
    uint32_t native = rgb_to_native_pixel(r, g, b);

    for (int y = 0; y < g_height; y++) {
        uint8_t *row = g_fb_mem + (size_t)y * (size_t)g_line_length;
        for (int x = 0; x < g_width; x++) {
            memcpy(row + (size_t)x * (size_t)g_bytes_per_pixel, &native, (size_t)g_bytes_per_pixel);
        }
    }

    if (g_prev_native) memset(g_prev_native, 0, (size_t)g_width * (size_t)g_height * sizeof(uint32_t));
    g_prev_frame_valid = false;
}

static void update_dirty_framebuffer_bounds(void) {
    if (!g_img || !g_native || !g_prev_native || !g_fb_mem) return;

    size_t total_pixels = (size_t)g_width * (size_t)g_height;

    // Convert the rendering buffer utilizing the newly optimized LUT mappings
    for (size_t i = 0; i < total_pixels; i++) {
        size_t img_idx = i * 3;
        g_native[i] = rgb_to_native_pixel(g_img[img_idx + 0], g_img[img_idx + 1], g_img[img_idx + 2]);
    }

    if (!g_prev_frame_valid) {
        for (int y = 0; y < g_height; y++) {
            uint8_t *dst = g_fb_mem + (size_t)y * (size_t)g_line_length;
            uint32_t *src = g_native + (size_t)y * (size_t)g_width;

            for (int x = 0; x < g_width; x++) {
                memcpy(dst + (size_t)x * (size_t)g_bytes_per_pixel, src + x, (size_t)g_bytes_per_pixel);
            }
        }

        memcpy(g_prev_native, g_native, total_pixels * sizeof(uint32_t));
        g_prev_frame_valid = true;
        return;
    }

    int start_y = -1;

    for (int y = 0; y < g_height; y++) {
        bool row_dirty = false;
        size_t row_offset = (size_t)y * (size_t)g_width;

        for (int x = 0; x < g_width; x++) {
            size_t idx = row_offset + (size_t)x;
            if (g_native[idx] != g_prev_native[idx]) {
                row_dirty = true;
                break;
            }
        }

        if (row_dirty) {
            if (start_y == -1) start_y = y;
        } else if (start_y != -1) {
            for (int yy = start_y; yy < y; yy++) {
                uint8_t *dst = g_fb_mem + (size_t)yy * (size_t)g_line_length;
                uint32_t *src = g_native + (size_t)yy * (size_t)g_width;

                for (int x = 0; x < g_width; x++) {
                    memcpy(dst + (size_t)x * (size_t)g_bytes_per_pixel, src + x, (size_t)g_bytes_per_pixel);
                }
            }
            start_y = -1;
        }
    }

    if (start_y != -1) {
        for (int yy = start_y; yy < g_height; yy++) {
            uint8_t *dst = g_fb_mem + (size_t)yy * (size_t)g_line_length;
            uint32_t *src = g_native + (size_t)yy * (size_t)g_width;

            for (int x = 0; x < g_width; x++) {
                memcpy(dst + (size_t)x * (size_t)g_bytes_per_pixel, src + x, (size_t)g_bytes_per_pixel);
            }
        }
    }

    memcpy(g_prev_native, g_native, total_pixels * sizeof(uint32_t));
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

static bool draw_clock(bool *out_needs_scroll, bool *out_layout_animating) {
    *out_needs_scroll = false;
    *out_layout_animating = false;

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

    draw_fill_rect(0, 0, g_width, g_height, BG_COLOR);

    char time_str[16];
    if (g_cfg_clock_24h) {
        strftime(time_str, sizeof(time_str), "%H:%M", &now_tm);
    } else {
        strftime(time_str, sizeof(time_str), "%I:%M", &now_tm);
        if (time_str[0] == '0') memmove(time_str, time_str + 1, strlen(time_str));
    }

    const int margin_x = scale_x(24);
    const int margin_y = scale_y(20);
    const int safe_bottom = scale_y(26);

    const int idle_font = scale_font(110);
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

    draw_text(g_font_bold, time_str, cur_clock_x, cur_clock_y, TEXT_COLOR_MAIN);

    if (s_anim_progress > 0.01f) {
        int title_size = scale_font(26);
        int artist_size = scale_font(20);
        int small_size = scale_font(16);

        if (g_art_rgba) {
            draw_album_art_bilinear(cur_art_x, art_target_y, art_size, art_size);
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

            /*
              Continuous marquee:
              Draw the title twice on a virtual strip so it wraps smoothly.
              This avoids the old behaviour where the title reached the end,
              jumped back to the beginning, and visually restarted.
            */
            if (is_playing) {
                double elapsed = monotonic_seconds() - s_scroll_start;
                int scroll_gap = scale_x(80);
                if (scroll_gap < 32) scroll_gap = 32;

                int cycle_w = title_w + scroll_gap;
                int offset = 0;

                if (cycle_w > 0) {
                    offset = (int)fmod(elapsed * (double)scale_x(30), (double)cycle_w);
                }

                int first_x = text_block_x - offset;
                int second_x = first_x + cycle_w;

                draw_text_clipped(g_font_bold, g_status_title, first_x, title_y,
                                  text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                                  TEXT_COLOR_MAIN);

                draw_text_clipped(g_font_bold, g_status_title, second_x, title_y,
                                  text_block_x, title_y - scale_y(34), text_right, title_y + scale_y(12),
                                  TEXT_COLOR_MAIN);
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

    update_dirty_framebuffer_bounds();
    return is_playing;
}

// ---------------- Init / Cleanup ----------------

static bool allocate_surfaces(void) {
    size_t pixels = (size_t)g_width * (size_t)g_height;

    g_img = malloc(pixels * 3);
    g_native = malloc(pixels * sizeof(uint32_t));
    g_prev_native = calloc(pixels, sizeof(uint32_t));

    if (!g_img || !g_native || !g_prev_native) {
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

    return true;
}

static void cleanup_all(void) {
    DBG_LOG("Shutdown requested");
    close_http_keepalive();

    if (g_art_rgba) {
        stbi_image_free(g_art_rgba);
        g_art_rgba = NULL;
    }

    free(g_http_buffer);
    g_http_buffer = NULL;

    free(g_img);
    g_img = NULL;

    free(g_native);
    g_native = NULL;

    free(g_prev_native);
    g_prev_native = NULL;

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
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (!init_all()) {
        cleanup_all();
        return 1;
    }

    hide_console_cursor(true);
    blank_physical_framebuffer(BG_COLOR);
    DBG_LOG("Initial framebuffer blank complete");

    while (g_running) {
        double start = monotonic_seconds();

        bool needs_scroll = false;
        bool layout_animating = false;
        bool is_playing = draw_clock(&needs_scroll, &layout_animating);

        double interval = DRAW_INTERVAL_IDLE;
        if (layout_animating) interval = DRAW_INTERVAL_ANIMATING;
        else if (needs_scroll) interval = DRAW_INTERVAL_SCROLLING;
        else if (is_playing) interval = DRAW_INTERVAL_PLAYING;

        double elapsed = monotonic_seconds() - start;
        double sleep_for = interval - elapsed;
        if (sleep_for < 0.02) sleep_for = 0.02;

        sleep_seconds(sleep_for);
    }

    cleanup_all();
    return 0;
}