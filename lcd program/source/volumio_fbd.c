#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glob.h>
#include <ifaddrs.h>
#include <json-c/json.h>
#include <linux/fb.h>
#include <math.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#ifndef JSON_C_TO_STRING_NOSLASHESCAPE
#define JSON_C_TO_STRING_NOSLASHESCAPE 16
#endif

#define DESIGN_WIDTH 480
#define DESIGN_HEIGHT 320
#define CONFIG_PATH "/etc/volumio_fbd_config.json"
#define VOLUMIO_STATE_URL "http://127.0.0.1:3000/api/v1/getState"
#define FONT_BOLD "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
#define FONT_REG  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
#define HTTP_MAX_BYTES (4u * 1024u * 1024u)
#define CURL_CONNECT_TIMEOUT_MS 500L
#define CURL_TOTAL_TIMEOUT_MS 1500L
#define BG_BLACK 0x000000
#define WHITE 0xFFFFFF
#define DIM 0xAAAAAA
#define MUTED 0x777777
#define BAR_BG 0x202020
#define BAR_FG 0x00FF00

typedef struct {
    int padding;
    int footer_height;
    int header_height;
    int progress_bar_height;
    int volume_bar_width;
    int volume_bar_height;
    int volume_bar_right_margin;
    int volume_overlay_seconds;
    int album_art_size;
    int generic_album_art_size;
    int title_font_size;
    int artist_font_size;
    int album_font_size;
    int source_font_size;
    int footer_font_size;
    int small_clock_font_size;
    int idle_clock_font_size;
    int idle_date_font_size;
    int idle_ip_font_size;
    int airplay_icon_width;
    int airplay_icon_height;
    uint32_t color_background;
    uint32_t color_text_main;
    uint32_t color_text_dim;
    uint32_t color_text_muted;
    uint32_t color_panel;
    uint32_t color_panel_line;
    uint32_t color_progress_bg;
    uint32_t color_progress_fg;
    uint32_t color_volume_bar;
    uint32_t color_album_placeholder;
} UiConfig;

typedef struct {
    char fb_path[256];
    int display_width;
    int display_height;
    bool clock_24h;
    double return_to_clock_seconds;
    bool colon_alpha_enabled;
    double colon_alpha_hz;
    int colon_alpha_min;
    int colon_alpha_max;
    bool album_bg_enabled;
    int album_bg_zoom_percent;
    int album_bg_brightness_percent;
    bool album_bg_fade_when_title_scrolls;
    int album_bg_fade_start;
    int album_bg_fade_end;
    UiConfig ui;
} Config;

typedef struct {
    char state[16];
    char service[64];
    char track_type[64];
    char title[256];
    char artist[256];
    char album[256];
    char albumart[1024];
    char art_url[1536];
    char samplerate[64];
    char bitdepth[64];
    double seek;
    double duration;
    int volume;
} PlayerState;

typedef struct {
    bool running;
    char rate[32];
    char bits[32];
    char channels[32];
    char line[128];
} AudioInfo;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} MemBuf;

static volatile sig_atomic_t g_running = 1;
static Config g_cfg;
static int g_fb_fd = -1;
static uint8_t *g_fb = NULL;
static size_t g_fb_bytes = 0;
static struct fb_var_screeninfo g_vinfo;
static struct fb_fix_screeninfo g_finfo;
static int g_w = DESIGN_WIDTH;
static int g_h = DESIGN_HEIGHT;
static int g_bpp = 16;
static int g_stride = DESIGN_WIDTH * 2;
static uint8_t *g_img = NULL;       /* RGB888 */
static uint8_t *g_prev_img = NULL;  /* previous RGB888 frame for dirty-span flush */
static bool g_prev_valid = false;

typedef struct {
    bool dirty;
    int min_x;
    int max_x;
} DirtySpan;
static DirtySpan *g_dirty_spans = NULL;

#define GLYPH_CACHE_SLOTS 1024
typedef struct {
    bool valid;
    FT_Face face;
    int px;
    unsigned int codepoint;
    int width;
    int height;
    int advance;
    int left;
    int top;
    uint8_t *alpha;
} GlyphCacheEntry;
static GlyphCacheEntry g_glyph_cache[GLYPH_CACHE_SLOTS];

static uint8_t *g_art_scaled_rgb = NULL;
static int g_art_scaled_w = 0;
static int g_art_scaled_h = 0;
static unsigned long g_art_scaled_generation = 0;

static uint8_t *g_scroll_strip_alpha = NULL;
static int g_scroll_strip_w = 0;
static int g_scroll_strip_h = 0;
static int g_scroll_strip_cycle_w = 0;
static int g_scroll_strip_px = 0;
static int g_scroll_strip_clip_w = 0;
static uint32_t g_scroll_strip_color = 0;
static FT_Face g_scroll_strip_face = NULL;
static char g_scroll_strip_text[256] = "";

static FT_Library g_ft;
static FT_Face g_font_bold;
static FT_Face g_font_reg;
static CURL *g_curl = NULL;
static uint8_t *g_art_rgba = NULL;
static int g_art_w = 0;
static int g_art_h = 0;
static char g_art_url[1536] = "";
static unsigned long g_art_generation = 1;
static uint8_t *g_bg_rgb = NULL;
static int g_bg_w = 0;
static int g_bg_h = 0;
static unsigned long g_bg_generation = 0;
static bool g_bg_fade = false;
static int g_bg_brightness = -1;
static int g_bg_zoom = -1;
static double g_audio_stopped_at = -1.0;
static double g_last_volumio_fetch = -99.0;
static double g_last_alsa_fetch = -99.0;
static PlayerState g_state;
static AudioInfo g_audio;
static int g_last_observed_volume = -1000;
static int g_volume_overlay_volume = -1;
static int g_volume_overlay_direction = 0; /* 1 = up, -1 = down */
static double g_volume_overlay_started_at = -1.0;
#define VOLUME_OVERLAY_SECONDS 2.0

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sleep_seconds(double s) {
    if (s <= 0.0) return;
    struct timespec req;
    req.tv_sec = (time_t)s;
    req.tv_nsec = (long)((s - (double)req.tv_sec) * 1000000000.0);
    while (g_running && nanosleep(&req, &req) == -1 && errno == EINTR) {}
}

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int max_i(int a, int b) { return a > b ? a : b; }
static int sx(int v) { return (int)(((long long)v * g_w + DESIGN_WIDTH / 2) / DESIGN_WIDTH); }
static int sy(int v) { return (int)(((long long)v * g_h + DESIGN_HEIGHT / 2) / DESIGN_HEIGHT); }
static int sf(int v) { int a = sx(v), b = sy(v); int r = a < b ? a : b; return r < 8 ? 8 : r; }

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";

    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;

    if (n > 0) memcpy(dst, src, n);
    dst[n] = '\0';
}

static void trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void lower_ascii(char *s) {
    if (!s) return;
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static bool empty_value(const char *s) {
    if (!s) return true;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return true;
    char tmp[64];
    safe_copy(tmp, sizeof(tmp), s);
    trim(tmp);
    lower_ascii(tmp);
    return strcmp(tmp, "null") == 0 || strcmp(tmp, "none") == 0 || strcmp(tmp, "unknown") == 0;
}

static void normalize_state(char *dst, size_t dst_sz, const char *src) {
    char tmp[32];
    safe_copy(tmp, sizeof(tmp), src ? src : "stop");
    trim(tmp);
    lower_ascii(tmp);
    if (strcmp(tmp, "playing") == 0) safe_copy(dst, dst_sz, "play");
    else if (strcmp(tmp, "paused") == 0) safe_copy(dst, dst_sz, "pause");
    else if (strcmp(tmp, "stopped") == 0 || strcmp(tmp, "stopping") == 0 || strcmp(tmp, "idle") == 0 || tmp[0] == 0) safe_copy(dst, dst_sz, "stop");
    else safe_copy(dst, dst_sz, tmp);
}

static bool write_all(int fd, const char *data, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        done += (size_t)n;
    }
    return true;
}

static void config_defaults(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    safe_copy(g_cfg.fb_path, sizeof(g_cfg.fb_path), "/dev/fb0");
    g_cfg.display_width = DESIGN_WIDTH;
    g_cfg.display_height = DESIGN_HEIGHT;
    g_cfg.clock_24h = false;
    g_cfg.return_to_clock_seconds = 5.0;
    g_cfg.colon_alpha_enabled = true;
    g_cfg.colon_alpha_hz = 0.5;
    g_cfg.colon_alpha_min = 32;
    g_cfg.colon_alpha_max = 255;
    g_cfg.album_bg_enabled = true;
    g_cfg.album_bg_zoom_percent = 220;
    g_cfg.album_bg_brightness_percent = 42;
    g_cfg.album_bg_fade_when_title_scrolls = true;
    g_cfg.album_bg_fade_start = 96;
    g_cfg.album_bg_fade_end = 128;

    g_cfg.ui.padding = 24;
    g_cfg.ui.footer_height = 24;
    g_cfg.ui.header_height = 32;
    g_cfg.ui.progress_bar_height = 7;
    g_cfg.ui.volume_bar_width = 20;
    g_cfg.ui.volume_bar_height = 228;
    g_cfg.ui.volume_bar_right_margin = 32;
    g_cfg.ui.volume_overlay_seconds = 2;
    g_cfg.ui.album_art_size = 150;
    g_cfg.ui.generic_album_art_size = 136;
    g_cfg.ui.title_font_size = 26;
    g_cfg.ui.artist_font_size = 18;
    g_cfg.ui.album_font_size = 15;
    g_cfg.ui.source_font_size = 14;
    g_cfg.ui.footer_font_size = 11;
    g_cfg.ui.small_clock_font_size = 16;
    g_cfg.ui.idle_clock_font_size = 132;
    g_cfg.ui.idle_date_font_size = 20;
    g_cfg.ui.idle_ip_font_size = 16;
    g_cfg.ui.airplay_icon_width = 132;
    g_cfg.ui.airplay_icon_height = 78;
    g_cfg.ui.color_background = BG_BLACK;
    g_cfg.ui.color_text_main = WHITE;
    g_cfg.ui.color_text_dim = DIM;
    g_cfg.ui.color_text_muted = MUTED;
    g_cfg.ui.color_panel = 0x050505;
    g_cfg.ui.color_panel_line = 0x242424;
    g_cfg.ui.color_progress_bg = BAR_BG;
    g_cfg.ui.color_progress_fg = BAR_FG;
    g_cfg.ui.color_volume_bar = WHITE;
    g_cfg.ui.color_album_placeholder = 0x181818;
}

static bool create_default_config(void) {
    json_object *root = json_object_new_object();
    json_object *display = json_object_new_object();
    json_object *visual = json_object_new_object();
    json_object *ui = json_object_new_object();
    json_object *colors = json_object_new_object();
    if (!root || !display || !visual || !ui || !colors) return false;

    json_object_object_add(display, "fb_path", json_object_new_string(g_cfg.fb_path));
    json_object_object_add(display, "width", json_object_new_int(g_cfg.display_width));
    json_object_object_add(display, "height", json_object_new_int(g_cfg.display_height));
    json_object_object_add(display, "clock_type", json_object_new_string(g_cfg.clock_24h ? "24h" : "12h"));
    json_object_object_add(display, "return_to_clock_seconds", json_object_new_double(g_cfg.return_to_clock_seconds));

    json_object_object_add(visual, "colon_alpha_fade_enabled", json_object_new_boolean(g_cfg.colon_alpha_enabled));
    json_object_object_add(visual, "colon_alpha_fade_hz", json_object_new_double(g_cfg.colon_alpha_hz));
    json_object_object_add(visual, "colon_alpha_min", json_object_new_int(g_cfg.colon_alpha_min));
    json_object_object_add(visual, "colon_alpha_max", json_object_new_int(g_cfg.colon_alpha_max));
    json_object_object_add(visual, "album_background_enabled", json_object_new_boolean(g_cfg.album_bg_enabled));
    json_object_object_add(visual, "album_background_zoom_percent", json_object_new_int(g_cfg.album_bg_zoom_percent));
    json_object_object_add(visual, "album_background_brightness_percent", json_object_new_int(g_cfg.album_bg_brightness_percent));
    json_object_object_add(visual, "album_background_diagonal_fade_when_title_scrolls", json_object_new_boolean(g_cfg.album_bg_fade_when_title_scrolls));
    json_object_object_add(visual, "album_background_diagonal_fade_start", json_object_new_int(g_cfg.album_bg_fade_start));
    json_object_object_add(visual, "album_background_diagonal_fade_end", json_object_new_int(g_cfg.album_bg_fade_end));

    json_object_object_add(ui, "padding", json_object_new_int(g_cfg.ui.padding));
    json_object_object_add(ui, "footer_height", json_object_new_int(g_cfg.ui.footer_height));
    json_object_object_add(ui, "header_height", json_object_new_int(g_cfg.ui.header_height));
    json_object_object_add(ui, "progress_bar_height", json_object_new_int(g_cfg.ui.progress_bar_height));
    json_object_object_add(ui, "volume_bar_width", json_object_new_int(g_cfg.ui.volume_bar_width));
    json_object_object_add(ui, "volume_bar_height", json_object_new_int(g_cfg.ui.volume_bar_height));
    json_object_object_add(ui, "volume_bar_right_margin", json_object_new_int(g_cfg.ui.volume_bar_right_margin));
    json_object_object_add(ui, "volume_overlay_seconds", json_object_new_int(g_cfg.ui.volume_overlay_seconds));
    json_object_object_add(ui, "album_art_size", json_object_new_int(g_cfg.ui.album_art_size));
    json_object_object_add(ui, "generic_album_art_size", json_object_new_int(g_cfg.ui.generic_album_art_size));
    json_object_object_add(ui, "title_font_size", json_object_new_int(g_cfg.ui.title_font_size));
    json_object_object_add(ui, "artist_font_size", json_object_new_int(g_cfg.ui.artist_font_size));
    json_object_object_add(ui, "album_font_size", json_object_new_int(g_cfg.ui.album_font_size));
    json_object_object_add(ui, "source_font_size", json_object_new_int(g_cfg.ui.source_font_size));
    json_object_object_add(ui, "footer_font_size", json_object_new_int(g_cfg.ui.footer_font_size));
    json_object_object_add(ui, "small_clock_font_size", json_object_new_int(g_cfg.ui.small_clock_font_size));
    json_object_object_add(ui, "idle_clock_font_size", json_object_new_int(g_cfg.ui.idle_clock_font_size));
    json_object_object_add(ui, "idle_date_font_size", json_object_new_int(g_cfg.ui.idle_date_font_size));
    json_object_object_add(ui, "idle_ip_font_size", json_object_new_int(g_cfg.ui.idle_ip_font_size));
    json_object_object_add(ui, "airplay_icon_width", json_object_new_int(g_cfg.ui.airplay_icon_width));
    json_object_object_add(ui, "airplay_icon_height", json_object_new_int(g_cfg.ui.airplay_icon_height));

    json_object_object_add(colors, "background", json_object_new_string("#000000"));
    json_object_object_add(colors, "text_main", json_object_new_string("#FFFFFF"));
    json_object_object_add(colors, "text_dim", json_object_new_string("#AAAAAA"));
    json_object_object_add(colors, "text_muted", json_object_new_string("#777777"));
    json_object_object_add(colors, "panel", json_object_new_string("#050505"));
    json_object_object_add(colors, "panel_line", json_object_new_string("#242424"));
    json_object_object_add(colors, "progress_bg", json_object_new_string("#202020"));
    json_object_object_add(colors, "progress_fg", json_object_new_string("#00FF00"));
    json_object_object_add(colors, "volume_bar", json_object_new_string("#FFFFFF"));
    json_object_object_add(colors, "album_placeholder", json_object_new_string("#181818"));

    json_object_object_add(root, "display", display);
    json_object_object_add(root, "visual", visual);
    json_object_object_add(root, "ui", ui);
    json_object_object_add(root, "colors", colors);

    const char *txt = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
    int fd = open(CONFIG_PATH, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        json_object_put(root);
        return false;
    }
    bool ok = write_all(fd, txt, strlen(txt)) && write_all(fd, "\n", 1);
    close(fd);
    json_object_put(root);
    return ok;
}

static void parse_clock_type(const char *s) {
    char tmp[32];
    safe_copy(tmp, sizeof(tmp), s ? s : "");
    trim(tmp);
    lower_ascii(tmp);
    if (strcmp(tmp, "24") == 0 || strcmp(tmp, "24h") == 0 || strcmp(tmp, "24-hour") == 0) g_cfg.clock_24h = true;
    if (strcmp(tmp, "12") == 0 || strcmp(tmp, "12h") == 0 || strcmp(tmp, "12-hour") == 0) g_cfg.clock_24h = false;
}

static bool parse_hex_color(const char *s, uint32_t *out) {
    if (!s || !out) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    unsigned int v = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i];
        unsigned int n;
        if (c >= '0' && c <= '9') n = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') n = (unsigned int)(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') n = (unsigned int)(10 + c - 'A');
        else return false;
        v = (v << 4) | n;
    }
    *out = (uint32_t)v;
    return true;
}

static void read_json_int(json_object *obj, const char *key, int *dst, int min_v, int max_v) {
    json_object *v = NULL;
    if (!obj || !key || !dst) return;
    if (json_object_object_get_ex(obj, key, &v) && v) {
        *dst = clampi(json_object_get_int(v), min_v, max_v);
    }
}

static void read_json_color(json_object *obj, const char *key, uint32_t *dst) {
    json_object *v = NULL;
    uint32_t color;
    if (!obj || !key || !dst) return;
    if (json_object_object_get_ex(obj, key, &v) && v && parse_hex_color(json_object_get_string(v), &color)) {
        *dst = color;
    }
}

static void load_config(void) {
    config_defaults();
    int fd = open(CONFIG_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) create_default_config();
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 65536) { close(fd); return; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return; }
    ssize_t n = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (n <= 0) { free(buf); return; }
    buf[n] = 0;
    json_object *root = json_tokener_parse(buf);
    free(buf);
    if (!root) return;

    json_object *display = NULL, *visual = NULL, *ui = NULL, *colors = NULL, *v = NULL;
    if (json_object_object_get_ex(root, "display", &display) && display) {
        if (json_object_object_get_ex(display, "fb_path", &v) && v) safe_copy(g_cfg.fb_path, sizeof(g_cfg.fb_path), json_object_get_string(v));
        if (json_object_object_get_ex(display, "width", &v) && v) g_cfg.display_width = clampi(json_object_get_int(v), 1, 8192);
        if (json_object_object_get_ex(display, "height", &v) && v) g_cfg.display_height = clampi(json_object_get_int(v), 1, 8192);
        if (json_object_object_get_ex(display, "clock_type", &v) && v) parse_clock_type(json_object_get_string(v));
        if (json_object_object_get_ex(display, "return_to_clock_seconds", &v) && v) {
            double d = json_object_get_double(v);
            if (d >= 0.0 && d <= 3600.0) g_cfg.return_to_clock_seconds = d;
        }
    }
    if (json_object_object_get_ex(root, "visual", &visual) && visual) {
        if (json_object_object_get_ex(visual, "colon_alpha_fade_enabled", &v) && v) g_cfg.colon_alpha_enabled = json_object_get_boolean(v);
        if (json_object_object_get_ex(visual, "colon_alpha_fade_hz", &v) && v) {
            double d = json_object_get_double(v);
            if (d >= 0.1 && d <= 10.0) g_cfg.colon_alpha_hz = d;
        }
        if (json_object_object_get_ex(visual, "colon_alpha_min", &v) && v) g_cfg.colon_alpha_min = clampi(json_object_get_int(v), 0, 255);
        if (json_object_object_get_ex(visual, "colon_alpha_max", &v) && v) g_cfg.colon_alpha_max = clampi(json_object_get_int(v), 0, 255);
        if (json_object_object_get_ex(visual, "album_background_enabled", &v) && v) g_cfg.album_bg_enabled = json_object_get_boolean(v);
        if (json_object_object_get_ex(visual, "album_background_zoom_percent", &v) && v) g_cfg.album_bg_zoom_percent = clampi(json_object_get_int(v), 100, 600);
        if (json_object_object_get_ex(visual, "album_background_brightness_percent", &v) && v) g_cfg.album_bg_brightness_percent = clampi(json_object_get_int(v), 0, 100);
        if (json_object_object_get_ex(visual, "album_background_diagonal_fade_when_title_scrolls", &v) && v) g_cfg.album_bg_fade_when_title_scrolls = json_object_get_boolean(v);
        if (json_object_object_get_ex(visual, "album_background_diagonal_fade_start", &v) && v) g_cfg.album_bg_fade_start = clampi(json_object_get_int(v), 0, 255);
        if (json_object_object_get_ex(visual, "album_background_diagonal_fade_end", &v) && v) g_cfg.album_bg_fade_end = clampi(json_object_get_int(v), 0, 255);
    }
    if (json_object_object_get_ex(root, "ui", &ui) && ui) {
        read_json_int(ui, "padding", &g_cfg.ui.padding, 0, 80);
        read_json_int(ui, "footer_height", &g_cfg.ui.footer_height, 0, 80);
        read_json_int(ui, "header_height", &g_cfg.ui.header_height, 0, 80);
        read_json_int(ui, "progress_bar_height", &g_cfg.ui.progress_bar_height, 1, 40);
        read_json_int(ui, "volume_bar_width", &g_cfg.ui.volume_bar_width, 4, 80);
        read_json_int(ui, "volume_bar_height", &g_cfg.ui.volume_bar_height, 40, 400);
        read_json_int(ui, "volume_bar_right_margin", &g_cfg.ui.volume_bar_right_margin, 0, 120);
        read_json_int(ui, "volume_overlay_seconds", &g_cfg.ui.volume_overlay_seconds, 1, 20);
        read_json_int(ui, "album_art_size", &g_cfg.ui.album_art_size, 48, 260);
        read_json_int(ui, "generic_album_art_size", &g_cfg.ui.generic_album_art_size, 48, 260);
        read_json_int(ui, "title_font_size", &g_cfg.ui.title_font_size, 10, 64);
        read_json_int(ui, "artist_font_size", &g_cfg.ui.artist_font_size, 8, 48);
        read_json_int(ui, "album_font_size", &g_cfg.ui.album_font_size, 8, 48);
        read_json_int(ui, "source_font_size", &g_cfg.ui.source_font_size, 8, 36);
        read_json_int(ui, "footer_font_size", &g_cfg.ui.footer_font_size, 8, 32);
        read_json_int(ui, "small_clock_font_size", &g_cfg.ui.small_clock_font_size, 8, 48);
        read_json_int(ui, "idle_clock_font_size", &g_cfg.ui.idle_clock_font_size, 40, 220);
        read_json_int(ui, "idle_date_font_size", &g_cfg.ui.idle_date_font_size, 8, 48);
        read_json_int(ui, "idle_ip_font_size", &g_cfg.ui.idle_ip_font_size, 8, 40);
        read_json_int(ui, "airplay_icon_width", &g_cfg.ui.airplay_icon_width, 48, 260);
        read_json_int(ui, "airplay_icon_height", &g_cfg.ui.airplay_icon_height, 32, 180);
    }
    if (json_object_object_get_ex(root, "colors", &colors) && colors) {
        read_json_color(colors, "background", &g_cfg.ui.color_background);
        read_json_color(colors, "text_main", &g_cfg.ui.color_text_main);
        read_json_color(colors, "text_dim", &g_cfg.ui.color_text_dim);
        read_json_color(colors, "text_muted", &g_cfg.ui.color_text_muted);
        read_json_color(colors, "panel", &g_cfg.ui.color_panel);
        read_json_color(colors, "panel_line", &g_cfg.ui.color_panel_line);
        read_json_color(colors, "progress_bg", &g_cfg.ui.color_progress_bg);
        read_json_color(colors, "progress_fg", &g_cfg.ui.color_progress_fg);
        read_json_color(colors, "volume_bar", &g_cfg.ui.color_volume_bar);
        read_json_color(colors, "album_placeholder", &g_cfg.ui.color_album_placeholder);
    }
    if (g_cfg.colon_alpha_min > g_cfg.colon_alpha_max) {
        int t = g_cfg.colon_alpha_min;
        g_cfg.colon_alpha_min = g_cfg.colon_alpha_max;
        g_cfg.colon_alpha_max = t;
    }
    if (g_cfg.album_bg_fade_start >= g_cfg.album_bg_fade_end) {
        g_cfg.album_bg_fade_start = 96;
        g_cfg.album_bg_fade_end = 128;
    }
    json_object_put(root);
}

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    MemBuf *b = (MemBuf *)ud;
    size_t add = size * nmemb;
    if (!b || !ptr || add == 0) return 0;
    if (b->len + add + 1 > HTTP_MAX_BYTES) return 0;
    if (b->len + add + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 65536;
        while (nc < b->len + add + 1 && nc < HTTP_MAX_BYTES) nc *= 2;
        if (nc > HTTP_MAX_BYTES) nc = HTTP_MAX_BYTES;
        if (nc < b->len + add + 1) return 0;
        uint8_t *p = realloc(b->data, nc);
        if (!p) return 0;
        b->data = p;
        b->cap = nc;
    }
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = 0;
    return add;
}

static uint8_t *http_get(const char *url, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!url || !*url || !g_running) return NULL;
    if (!g_curl) g_curl = curl_easy_init();
    if (!g_curl) return NULL;
    MemBuf b = {0};
    curl_easy_reset(g_curl);
    curl_easy_setopt(g_curl, CURLOPT_URL, url);
    curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(g_curl, CURLOPT_USERAGENT, "volumio_fbd_truth_rebuild/1.0");
    curl_easy_setopt(g_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(g_curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(g_curl, CURLOPT_CONNECTTIMEOUT_MS, CURL_CONNECT_TIMEOUT_MS);
    curl_easy_setopt(g_curl, CURLOPT_TIMEOUT_MS, CURL_TOTAL_TIMEOUT_MS);
    curl_easy_setopt(g_curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(g_curl, CURLOPT_ACCEPT_ENCODING, "");
    CURLcode rc = curl_easy_perform(g_curl);
    long code = 0;
    curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &code);
    if (rc != CURLE_OK || code < 200 || code >= 300 || b.len == 0) {
        free(b.data);
        return NULL;
    }
    if (out_len) *out_len = b.len;
    return b.data;
}

static bool is_plain_albumart(const char *s) {
    if (!s) return true;
    return strcmp(s, "/albumart") == 0 || strcmp(s, "albumart") == 0 || strcmp(s, "http://127.0.0.1:3000/albumart") == 0;
}

static bool art_is_specific(const char *s) {
    if (empty_value(s) || is_plain_albumart(s)) return false;
    if (strstr(s, "cacheid=") || strstr(s, "web=") || strstr(s, "path=")) return true;
    if (strstr(s, "i.scdn.co/image/")) return true;
    if (strstr(s, ".jpg") || strstr(s, ".jpeg") || strstr(s, ".png") || strstr(s, ".webp")) return true;
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

static void resolve_albumart(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (empty_value(src)) return;
    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
        safe_copy(dst, dst_sz, src);
        return;
    }
    if (src[0] == '/') snprintf(dst, dst_sz, "http://127.0.0.1:3000%s", src);
    else snprintf(dst, dst_sz, "http://127.0.0.1:3000/%s", src);
}

static void json_get_string(json_object *root, const char *key, char *dst, size_t dst_sz) {
    json_object *v = NULL;
    const char *s = "";
    if (root && json_object_object_get_ex(root, key, &v) && v) s = json_object_get_string(v);
    safe_copy(dst, dst_sz, empty_value(s) ? "" : s);
}

static bool fetch_volumio(PlayerState *s) {
    if (!s) return false;
    size_t len = 0;
    uint8_t *body = http_get(VOLUMIO_STATE_URL, &len);
    if (!body) return false;
    json_object *root = json_tokener_parse((const char *)body);
    free(body);
    if (!root) return false;

    PlayerState n = *s;
    char raw_status[32];
    json_get_string(root, "status", raw_status, sizeof(raw_status));
    normalize_state(n.state, sizeof(n.state), raw_status);
    json_get_string(root, "service", n.service, sizeof(n.service));
    json_get_string(root, "trackType", n.track_type, sizeof(n.track_type));
    json_get_string(root, "title", n.title, sizeof(n.title));
    json_get_string(root, "artist", n.artist, sizeof(n.artist));
    json_get_string(root, "album", n.album, sizeof(n.album));
    json_get_string(root, "albumart", n.albumart, sizeof(n.albumart));
    json_get_string(root, "samplerate", n.samplerate, sizeof(n.samplerate));
    json_get_string(root, "bitdepth", n.bitdepth, sizeof(n.bitdepth));
    resolve_albumart(n.art_url, sizeof(n.art_url), n.albumart);

    json_object *v = NULL;
    n.volume = -1;
    if (json_object_object_get_ex(root, "volume", &v) && v && json_object_get_type(v) != json_type_null) n.volume = json_object_get_int(v);
    n.seek = 0.0;
    if (json_object_object_get_ex(root, "seek", &v) && v && json_object_get_type(v) != json_type_null) {
        double raw = json_object_get_double(v);
        n.seek = raw > 10000.0 ? raw / 1000.0 : raw;
    }
    n.duration = 0.0;
    if (json_object_object_get_ex(root, "duration", &v) && v && json_object_get_type(v) != json_type_null) n.duration = json_object_get_double(v);
    json_object_put(root);
    *s = n;
    return true;
}

static bool file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) { ok = true; break; }
    }
    fclose(f);
    return ok;
}

static bool alsa_running(void) {
    glob_t g;
    bool running = false;
    if (glob("/proc/asound/card*/pcm*/sub*/status", 0, NULL, &g) != 0) return false;
    for (size_t i = 0; i < g.gl_pathc && !running; i++) {
        running = file_contains(g.gl_pathv[i], "state: RUNNING");
    }
    globfree(&g);
    return running;
}

static void friendly_rate(char *dst, size_t dst_sz, int rate) {
    if (!dst || dst_sz == 0) return;
    if (rate <= 0) { dst[0] = 0; return; }
    if (rate % 1000 == 0) snprintf(dst, dst_sz, "%d kHz", rate / 1000);
    else snprintf(dst, dst_sz, "%.1f kHz", (double)rate / 1000.0);
}

static void friendly_format(char *dst, size_t dst_sz, const char *fmt) {
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (!fmt || !*fmt) return;
    if (strstr(fmt, "S16")) safe_copy(dst, dst_sz, "16-bit");
    else if (strstr(fmt, "S24")) safe_copy(dst, dst_sz, "24-bit");
    else if (strstr(fmt, "S32")) safe_copy(dst, dst_sz, "32-bit");
    else if (strstr(fmt, "FLOAT")) safe_copy(dst, dst_sz, "Float");
    else safe_copy(dst, dst_sz, fmt);
}

static void friendly_channels(char *dst, size_t dst_sz, int ch) {
    if (!dst || dst_sz == 0) return;
    if (ch == 1) safe_copy(dst, dst_sz, "Mono");
    else if (ch == 2) safe_copy(dst, dst_sz, "Stereo");
    else if (ch == 6) safe_copy(dst, dst_sz, "5.1");
    else if (ch == 8) safe_copy(dst, dst_sz, "7.1");
    else if (ch > 0) snprintf(dst, dst_sz, "%dch", ch);
    else dst[0] = 0;
}

static bool parse_hw_file(const char *path, AudioInfo *a) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256], fmt[64] = "";
    int rate = 0, ch = 0;
    bool open = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "closed")) continue;
        if (sscanf(line, "format: %63s", fmt) == 1) open = true;
        if (sscanf(line, "channels: %d", &ch) == 1) open = true;
        if (sscanf(line, "rate: %d", &rate) == 1) open = true;
    }
    fclose(f);
    if (!open || !fmt[0] || rate <= 0 || ch <= 0) return false;
    friendly_rate(a->rate, sizeof(a->rate), rate);
    friendly_format(a->bits, sizeof(a->bits), fmt);
    friendly_channels(a->channels, sizeof(a->channels), ch);
    snprintf(a->line, sizeof(a->line), "%s / %s / %s", a->rate, a->bits, a->channels);
    return true;
}

static void read_alsa(AudioInfo *a) {
    memset(a, 0, sizeof(*a));
    a->running = alsa_running();
    glob_t g;
    if (glob("/proc/asound/card*/pcm*/sub*/hw_params", 0, NULL, &g) != 0) return;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        if (parse_hw_file(g.gl_pathv[i], a)) break;
    }
    globfree(&g);
}

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t pr = ((uint32_t)r * ((1u << g_vinfo.red.length) - 1u) + 127u) / 255u;
    uint32_t pg = ((uint32_t)g * ((1u << g_vinfo.green.length) - 1u) + 127u) / 255u;
    uint32_t pb = ((uint32_t)b * ((1u << g_vinfo.blue.length) - 1u) + 127u) / 255u;
    return (pr << g_vinfo.red.offset) | (pg << g_vinfo.green.offset) | (pb << g_vinfo.blue.offset);
}

static bool fb_init(void) {
    g_fb_fd = open(g_cfg.fb_path, O_RDWR | O_CLOEXEC);
    if (g_fb_fd < 0) { perror("open fb"); return false; }
    if (ioctl(g_fb_fd, FBIOGET_FSCREENINFO, &g_finfo) != 0 || ioctl(g_fb_fd, FBIOGET_VSCREENINFO, &g_vinfo) != 0) {
        perror("fb ioctl"); return false;
    }
    int fb_w = (int)g_vinfo.xres;
    int fb_h = (int)g_vinfo.yres;
    g_w = clampi(g_cfg.display_width > 0 ? g_cfg.display_width : fb_w, 1, fb_w);
    g_h = clampi(g_cfg.display_height > 0 ? g_cfg.display_height : fb_h, 1, fb_h);
    g_bpp = (int)g_vinfo.bits_per_pixel;
    g_stride = (int)g_finfo.line_length;
    g_fb_bytes = (size_t)g_stride * (size_t)fb_h;
    g_fb = mmap(NULL, g_fb_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb == MAP_FAILED) { perror("mmap fb"); g_fb = NULL; return false; }
    g_img = calloc((size_t)g_w * (size_t)g_h * 3u, 1);
    g_prev_img = calloc((size_t)g_w * (size_t)g_h * 3u, 1);
    g_dirty_spans = calloc((size_t)g_h, sizeof(DirtySpan));
    return g_img != NULL && g_prev_img != NULL && g_dirty_spans != NULL;
}

static void fb_flush(void) {
    if (!g_fb || !g_img || !g_prev_img || !g_dirty_spans) return;

    for (int y = 0; y < g_h; y++) {
        DirtySpan *spn = &g_dirty_spans[y];
        spn->dirty = false;
        spn->min_x = g_w;
        spn->max_x = -1;

        uint8_t *cur = g_img + (size_t)y * (size_t)g_w * 3u;
        uint8_t *prev = g_prev_img + (size_t)y * (size_t)g_w * 3u;

        if (!g_prev_valid) {
            spn->dirty = true;
            spn->min_x = 0;
            spn->max_x = g_w - 1;
            memcpy(prev, cur, (size_t)g_w * 3u);
            continue;
        }

        for (int x = 0; x < g_w; x++) {
            size_t i = (size_t)x * 3u;
            if (cur[i + 0] != prev[i + 0] || cur[i + 1] != prev[i + 1] || cur[i + 2] != prev[i + 2]) {
                if (!spn->dirty) {
                    spn->dirty = true;
                    spn->min_x = x;
                }
                spn->max_x = x;
                prev[i + 0] = cur[i + 0];
                prev[i + 1] = cur[i + 1];
                prev[i + 2] = cur[i + 2];
            }
        }
    }

    for (int y = 0; y < g_h; y++) {
        DirtySpan *spn = &g_dirty_spans[y];
        if (!spn->dirty || spn->max_x < spn->min_x) continue;

        int x0 = clampi(spn->min_x, 0, g_w - 1);
        int x1 = clampi(spn->max_x, 0, g_w - 1);
        uint8_t *dst = g_fb + (size_t)y * (size_t)g_stride;
        uint8_t *src = g_img + (size_t)y * (size_t)g_w * 3u;

        for (int x = x0; x <= x1; x++) {
            uint8_t r = src[x * 3 + 0], gg = src[x * 3 + 1], b = src[x * 3 + 2];
            uint32_t pix = pack_rgb(r, gg, b);
            if (g_bpp == 16) ((uint16_t *)dst)[x] = (uint16_t)pix;
            else if (g_bpp == 24) {
                dst[x * 3 + 0] = (uint8_t)(pix & 0xff);
                dst[x * 3 + 1] = (uint8_t)((pix >> 8) & 0xff);
                dst[x * 3 + 2] = (uint8_t)((pix >> 16) & 0xff);
            } else if (g_bpp == 32) ((uint32_t *)dst)[x] = pix;
        }
    }

    g_prev_valid = true;
}

static void clear_screen(uint32_t color) {
    uint8_t r = (uint8_t)((color >> 16) & 0xff), g = (uint8_t)((color >> 8) & 0xff), b = (uint8_t)(color & 0xff);
    for (int i = 0; i < g_w * g_h; i++) { g_img[i * 3] = r; g_img[i * 3 + 1] = g; g_img[i * 3 + 2] = b; }
}

static void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    size_t i = ((size_t)y * (size_t)g_w + (size_t)x) * 3u;
    g_img[i + 0] = (uint8_t)((color >> 16) & 0xff);
    g_img[i + 1] = (uint8_t)((color >> 8) & 0xff);
    g_img[i + 2] = (uint8_t)(color & 0xff);
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = clampi(x, 0, g_w), y0 = clampi(y, 0, g_h), x1 = clampi(x + w, 0, g_w), y1 = clampi(y + h, 0, g_h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) put_pixel(xx, yy, color);
}

static void blend_pixel(int x, int y, uint32_t color, int alpha) {
    if (x < 0 || y < 0 || x >= g_w || y >= g_h || alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    size_t i = ((size_t)y * (size_t)g_w + (size_t)x) * 3u;
    int sr = (color >> 16) & 0xff, sg = (color >> 8) & 0xff, sb = color & 0xff;
    g_img[i + 0] = (uint8_t)((sr * alpha + g_img[i + 0] * (255 - alpha)) / 255);
    g_img[i + 1] = (uint8_t)((sg * alpha + g_img[i + 1] * (255 - alpha)) / 255);
    g_img[i + 2] = (uint8_t)((sb * alpha + g_img[i + 2] * (255 - alpha)) / 255);
}

static void fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha) {
    int x0 = clampi(x, 0, g_w), y0 = clampi(y, 0, g_h), x1 = clampi(x + w, 0, g_w), y1 = clampi(y + h, 0, g_h);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            blend_pixel(xx, yy, color, alpha);
        }
    }
}

static bool fonts_init(void) {
    if (FT_Init_FreeType(&g_ft) != 0) return false;
    if (FT_New_Face(g_ft, FONT_BOLD, 0, &g_font_bold) != 0) return false;
    if (FT_New_Face(g_ft, FONT_REG, 0, &g_font_reg) != 0) return false;
    return true;
}

static unsigned int glyph_hash(FT_Face face, int px, unsigned int cp) {
    uintptr_t f = (uintptr_t)face;
    return (unsigned int)((f >> 4) ^ (uintptr_t)f ^ ((unsigned int)px * 131u) ^ (cp * 2654435761u));
}

static GlyphCacheEntry *glyph_cache_get(FT_Face face, int px, unsigned int cp) {
    if (!face || px <= 0) return NULL;
    unsigned int start = glyph_hash(face, px, cp) % GLYPH_CACHE_SLOTS;

    for (unsigned int probe = 0; probe < GLYPH_CACHE_SLOTS; probe++) {
        unsigned int idx = (start + probe) % GLYPH_CACHE_SLOTS;
        GlyphCacheEntry *e = &g_glyph_cache[idx];

        if (e->valid && e->face == face && e->px == px && e->codepoint == cp) return e;

        if (!e->valid) {
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0) return NULL;
            FT_GlyphSlot g = face->glyph;

            memset(e, 0, sizeof(*e));
            e->valid = true;
            e->face = face;
            e->px = px;
            e->codepoint = cp;
            e->width = (int)g->bitmap.width;
            e->height = (int)g->bitmap.rows;
            e->advance = (int)(g->advance.x >> 6);
            e->left = g->bitmap_left;
            e->top = g->bitmap_top;

            if (e->width > 0 && e->height > 0) {
                e->alpha = malloc((size_t)e->width * (size_t)e->height);
                if (!e->alpha) {
                    e->valid = false;
                    return NULL;
                }
                for (int row = 0; row < e->height; row++) {
                    memcpy(e->alpha + (size_t)row * e->width,
                           g->bitmap.buffer + (size_t)row * g->bitmap.pitch,
                           (size_t)e->width);
                }
            }
            return e;
        }
    }

    /* Cache is full. Reuse a deterministic slot. */
    GlyphCacheEntry *e = &g_glyph_cache[start];
    free(e->alpha);
    memset(e, 0, sizeof(*e));
    return glyph_cache_get(face, px, cp);
}

static int text_width(FT_Face face, int px, const char *s) {
    if (!face || !s) return 0;
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        GlyphCacheEntry *g = glyph_cache_get(face, px, *p);
        if (g) w += g->advance;
    }
    return w;
}

static void draw_text_clip(FT_Face face, int px, int x, int y, const char *s, uint32_t color, int alpha, int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    if (!face || !s || !*s || alpha <= 0) return;
    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        GlyphCacheEntry *g = glyph_cache_get(face, px, *p);
        if (!g) continue;

        int gx = pen_x + g->left;
        int gy = y - g->top;
        for (int row = 0; row < g->height; row++) {
            int yy = gy + row;
            if (yy < clip_y0 || yy >= clip_y1) continue;
            for (int col = 0; col < g->width; col++) {
                int xx = gx + col;
                if (xx < clip_x0 || xx >= clip_x1) continue;
                int a = (g->alpha[(size_t)row * g->width + col] * alpha) / 255;
                blend_pixel(xx, yy, color, a);
            }
        }
        pen_x += g->advance;
    }
}

static void draw_text(FT_Face face, int px, int x, int y, const char *s, uint32_t color) {
    draw_text_clip(face, px, x, y, s, color, 255, 0, 0, g_w, g_h);
}

static int colon_alpha(double now) {
    if (!g_cfg.colon_alpha_enabled) return 255;
    double phase = fmod(now * g_cfg.colon_alpha_hz, 1.0);
    if (phase < 0) phase += 1.0;
    double wave = (sin(phase * 2.0 * M_PI - M_PI / 2.0) + 1.0) * 0.5;
    return clampi((int)(g_cfg.colon_alpha_min + wave * (g_cfg.colon_alpha_max - g_cfg.colon_alpha_min)), 0, 255);
}

static void format_time(char *dst, size_t dst_sz) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    if (g_cfg.clock_24h) strftime(dst, dst_sz, "%H:%M", &tmv);
    else {
        strftime(dst, dst_sz, "%I:%M", &tmv);
        if (dst[0] == '0') memmove(dst, dst + 1, strlen(dst));
    }
}

static void draw_clock_text(int cx, int baseline, int px, uint32_t color, double now) {
    char t[32];
    format_time(t, sizeof(t));
    char *c = strchr(t, ':');
    if (!c) { draw_text(g_font_bold, px, cx - text_width(g_font_bold, px, t) / 2, baseline, t, color); return; }
    char left[16], right[16];
    size_t ln = (size_t)(c - t);
    memcpy(left, t, ln); left[ln] = 0;
    safe_copy(right, sizeof(right), c + 1);
    int wl = text_width(g_font_bold, px, left);
    int wc = text_width(g_font_bold, px, ":");
    int wr = text_width(g_font_bold, px, right);
    int x = cx - (wl + wc + wr) / 2;
    draw_text(g_font_bold, px, x, baseline, left, color);
    draw_text_clip(g_font_bold, px, x + wl, baseline, ":", color, colon_alpha(now), 0, 0, g_w, g_h);
    draw_text(g_font_bold, px, x + wl + wc, baseline, right, color);
}

static void free_scaled_art(void) {
    free(g_art_scaled_rgb);
    g_art_scaled_rgb = NULL;
    g_art_scaled_w = 0;
    g_art_scaled_h = 0;
    g_art_scaled_generation = 0;
}

static bool ensure_scaled_art(int dw, int dh) {
    if (!g_art_rgba || g_art_w <= 0 || g_art_h <= 0 || dw <= 0 || dh <= 0) return false;
    if (g_art_scaled_rgb && g_art_scaled_w == dw && g_art_scaled_h == dh && g_art_scaled_generation == g_art_generation) return true;

    free_scaled_art();
    g_art_scaled_rgb = malloc((size_t)dw * (size_t)dh * 3u);
    if (!g_art_scaled_rgb) return false;
    g_art_scaled_w = dw;
    g_art_scaled_h = dh;
    g_art_scaled_generation = g_art_generation;

    for (int y = 0; y < dh; y++) {
        int syy = (int)((long long)y * g_art_h / dh);
        for (int x = 0; x < dw; x++) {
            int sxx = (int)((long long)x * g_art_w / dw);
            uint8_t *sp = g_art_rgba + ((size_t)syy * g_art_w + sxx) * 4u;
            uint8_t *dp = g_art_scaled_rgb + ((size_t)y * dw + x) * 3u;
            int a = sp[3];
            dp[0] = (uint8_t)(sp[0] * a / 255);
            dp[1] = (uint8_t)(sp[1] * a / 255);
            dp[2] = (uint8_t)(sp[2] * a / 255);
        }
    }
    return true;
}

static void draw_cached_album_art(int dx, int dy, int dw, int dh) {
    if (!ensure_scaled_art(dw, dh)) return;
    for (int y = 0; y < dh; y++) {
        int yy = dy + y;
        if (yy < 0 || yy >= g_h) continue;
        for (int x = 0; x < dw; x++) {
            int xx = dx + x;
            if (xx < 0 || xx >= g_w) continue;
            uint8_t *sp = g_art_scaled_rgb + ((size_t)y * dw + x) * 3u;
            size_t di = ((size_t)yy * g_w + xx) * 3u;
            g_img[di + 0] = sp[0];
            g_img[di + 1] = sp[1];
            g_img[di + 2] = sp[2];
        }
    }
}

static bool load_album_art(const char *url) {
    if (!url || !*url || !art_is_specific(url)) return false;
    if (strcmp(url, g_art_url) == 0 && g_art_rgba) return true;
    size_t len = 0;
    uint8_t *buf = http_get(url, &len);
    if (!buf) return false;
    int w = 0, h = 0, comp = 0;
    uint8_t *img = stbi_load_from_memory(buf, (int)len, &w, &h, &comp, 4);
    free(buf);
    if (!img || w <= 0 || h <= 0) { if (img) stbi_image_free(img); return false; }
    if (g_art_rgba) stbi_image_free(g_art_rgba);
    g_art_rgba = img;
    g_art_w = w;
    g_art_h = h;
    safe_copy(g_art_url, sizeof(g_art_url), url);
    g_art_generation++;
    if (g_art_generation == 0) g_art_generation = 1;
    free_scaled_art();
    g_bg_generation = 0;
    return true;
}

static void clear_art(void) {
    if (g_art_rgba) stbi_image_free(g_art_rgba);
    g_art_rgba = NULL;
    g_art_w = g_art_h = 0;
    g_art_url[0] = 0;
    g_art_generation++;
    free_scaled_art();
    g_bg_generation = 0;
}

static void update_art_for_state(const PlayerState *s, bool screen_clock) {
    bool is_airplay = strcmp(s->service, "airplay_emulation") == 0 || strcmp(s->track_type, "airplay") == 0;
    if (screen_clock || is_airplay) { clear_art(); return; }
    if (art_is_specific(s->art_url)) load_album_art(s->art_url);
}

static void build_album_background(bool fade) {
    if (!g_art_rgba || !g_cfg.album_bg_enabled) return;
    if (g_bg_rgb && g_bg_w == g_w && g_bg_h == g_h && g_bg_generation == g_art_generation && g_bg_fade == fade && g_bg_brightness == g_cfg.album_bg_brightness_percent && g_bg_zoom == g_cfg.album_bg_zoom_percent) return;
    free(g_bg_rgb);
    g_bg_rgb = calloc((size_t)g_w * (size_t)g_h * 3u, 1);
    if (!g_bg_rgb) return;
    g_bg_w = g_w; g_bg_h = g_h; g_bg_fade = fade; g_bg_generation = g_art_generation;
    g_bg_brightness = g_cfg.album_bg_brightness_percent; g_bg_zoom = g_cfg.album_bg_zoom_percent;

    int base = max_i(g_w, g_h);
    int dst_size = base * g_cfg.album_bg_zoom_percent / 100;
    int dx = (g_w - dst_size) / 2;
    int dy = (g_h - dst_size) / 2;
    int width_denom = max_i(1, g_w - 1);
    int height_denom = max_i(1, g_h - 1);
    int fade_start = g_cfg.album_bg_fade_start;
    int fade_end = g_cfg.album_bg_fade_end;
    int bright = clampi(g_cfg.album_bg_brightness_percent, 0, 100);

    for (int y = 0; y < g_h; y++) {
        for (int x = 0; x < g_w; x++) {
            int sx0 = (int)((long long)(x - dx) * g_art_w / dst_size);
            int sy0 = (int)((long long)(y - dy) * g_art_h / dst_size);
            if (sx0 < 0 || sy0 < 0 || sx0 >= g_art_w || sy0 >= g_art_h) continue;
            uint8_t *sp = g_art_rgba + ((size_t)sy0 * g_art_w + sx0) * 4u;
            int br = sp[0] * bright / 100;
            int bg = sp[1] * bright / 100;
            int bb = sp[2] * bright / 100;
            int keep = 255;
            if (fade) {
                int nx255 = x * 255 / width_denom;
                int invy255 = (g_h - 1 - y) * 255 / height_denom;
                int diag = (nx255 + invy255) / 2;
                if (diag <= fade_start) keep = 255;
                else if (diag >= fade_end) keep = 0;
                else keep = 255 - (255 * (diag - fade_start) / max_i(1, fade_end - fade_start));
            }
            uint8_t *dp = g_bg_rgb + ((size_t)y * g_w + x) * 3u;
            dp[0] = (uint8_t)(br * keep / 255);
            dp[1] = (uint8_t)(bg * keep / 255);
            dp[2] = (uint8_t)(bb * keep / 255);
        }
    }
}

static void draw_album_background(bool fade) {
    if (!g_art_rgba || !g_cfg.album_bg_enabled) { clear_screen(g_cfg.ui.color_background); return; }
    build_album_background(fade);
    if (g_bg_rgb) memcpy(g_img, g_bg_rgb, (size_t)g_w * g_h * 3u);
    else clear_screen(g_cfg.ui.color_background);
}

static bool is_spotify(const PlayerState *s) { return strcmp(s->service, "spop") == 0 || strcmp(s->track_type, "spotify") == 0; }
static bool is_airplay(const PlayerState *s) { return strcmp(s->service, "airplay_emulation") == 0 || strcmp(s->track_type, "airplay") == 0; }
static bool is_real_idle(const PlayerState *s) { return strcmp(s->state, "stop") == 0 && strcmp(s->service, "mpd") == 0 && s->title[0] == 0 && s->duration <= 0.0; }

static void format_source_truth(const PlayerState *s, char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;
    const char *src = "Volumio";
    if (is_spotify(s)) src = "Spotify";
    else if (is_airplay(s)) src = "AirPlay";
    else if (s->service[0]) src = s->service;
    else if (s->track_type[0]) src = s->track_type;

    const char *state = s->state[0] ? s->state : "unknown";
    if (is_real_idle(s)) snprintf(dst, dst_sz, "Clock");
    else snprintf(dst, dst_sz, "%s | %s", src, state);
}

static void compact_audio_format(const AudioInfo *a, char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';

    if (!a || !a->line[0]) {
        snprintf(dst, dst_sz, "closed");
        return;
    }

    char rate[24] = "";
    char bits[24] = "";
    char chans[24] = "";
    safe_copy(rate, sizeof(rate), a->rate);
    safe_copy(bits, sizeof(bits), a->bits);
    safe_copy(chans, sizeof(chans), a->channels);

    char *p = strstr(rate, " kHz");
    if (p) *p = '\0';

    p = strstr(bits, "-bit");
    if (p) *p = '\0';

    if (strcmp(chans, "Stereo") == 0) safe_copy(chans, sizeof(chans), "St");
    else if (strcmp(chans, "Mono") == 0) safe_copy(chans, sizeof(chans), "Mo");

    if (rate[0] && bits[0] && chans[0]) snprintf(dst, dst_sz, "%s/%s/%s", rate, bits, chans);
    else safe_copy(dst, dst_sz, a->line);
}

static void format_clock_countdown(double now, char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';

    if (g_audio_stopped_at >= 0.0 && g_cfg.return_to_clock_seconds > 0.0) {
        double left = g_cfg.return_to_clock_seconds - (now - g_audio_stopped_at);
        if (left < 0.0) left = 0.0;
        snprintf(dst, dst_sz, "Clock %.0fs", ceil(left));
    }
}

static bool get_primary_ipv4(char *dst, size_t dst_sz) {
    if (!dst || dst_sz == 0) return false;
    safe_copy(dst, dst_sz, "No IP");

    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) return false;

    int best_score = 0;
    char best_ip[INET_ADDRSTRLEN] = "";

    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        int score = 50;
        if (strncmp(ifa->ifa_name, "eth", 3) == 0) score = 100;
        else if (strncmp(ifa->ifa_name, "en", 2) == 0) score = 95;
        else if (strncmp(ifa->ifa_name, "wlan", 4) == 0) score = 90;
        else if (strncmp(ifa->ifa_name, "wl", 2) == 0) score = 85;
        else if (strncmp(ifa->ifa_name, "lo", 2) == 0 || strncmp(ifa->ifa_name, "docker", 6) == 0 ||
                 strncmp(ifa->ifa_name, "br-", 3) == 0 || strncmp(ifa->ifa_name, "veth", 4) == 0 ||
                 strncmp(ifa->ifa_name, "tun", 3) == 0 || strncmp(ifa->ifa_name, "tap", 3) == 0) score = 0;
        if (score <= best_score) continue;

        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        char ip[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
        if (strncmp(ip, "127.", 4) == 0 || strncmp(ip, "169.254.", 8) == 0) continue;

        best_score = score;
        safe_copy(best_ip, sizeof(best_ip), ip);
    }

    freeifaddrs(ifaddr);

    if (best_ip[0]) {
        safe_copy(dst, dst_sz, best_ip);
        return true;
    }
    return false;
}

static void update_volume_overlay(const PlayerState *s, double now) {
    if (!s || s->volume < 0 || s->volume > 100) return;

    if (g_last_observed_volume == -1000) {
        g_last_observed_volume = s->volume;
        return;
    }

    if (s->volume != g_last_observed_volume) {
        g_volume_overlay_direction = (s->volume > g_last_observed_volume) ? 1 : -1;
        g_volume_overlay_volume = s->volume;
        g_volume_overlay_started_at = now;
        g_last_observed_volume = s->volume;
    }
}

static bool volume_overlay_active(double now) {
    return g_volume_overlay_started_at >= 0.0 &&
           (now - g_volume_overlay_started_at) < (double)g_cfg.ui.volume_overlay_seconds &&
           g_volume_overlay_volume >= 0;
}

static void draw_volume_overlay(double now, bool clock_mode) {
    if (clock_mode || !volume_overlay_active(now)) return;

    double age = now - g_volume_overlay_started_at;
    int alpha = (int)(255.0 * (1.0 - (age / (double)g_cfg.ui.volume_overlay_seconds)));
    alpha = clampi(alpha, 0, 255);
    if (alpha <= 0) return;

    int vol = clampi(g_volume_overlay_volume, 0, 100);
    int bar_w = sx(g_cfg.ui.volume_bar_width);
    int bar_h = sy(g_cfg.ui.volume_bar_height);
    if (bar_h > g_h - sy(40)) bar_h = g_h - sy(40);
    int bar_x = g_w - sx(g_cfg.ui.volume_bar_right_margin);
    int bar_y = (g_h - bar_h) / 2;
    int fill_h = bar_h * vol / 100;
    int fill_y = bar_y + bar_h - fill_h;

    /* Right-side vertical volume indicator. White only. */
    fill_rect_alpha(bar_x - sx(5), bar_y - sy(8), bar_w + sx(10), bar_h + sy(16), g_cfg.ui.color_background, (alpha * 120) / 255);
    fill_rect_alpha(bar_x, bar_y, bar_w, bar_h, g_cfg.ui.color_volume_bar, (alpha * 42) / 255);
    fill_rect_alpha(bar_x, fill_y, bar_w, fill_h, g_cfg.ui.color_volume_bar, alpha);

    /* Small direction and value hints, kept beside the right-edge bar. */
    const char *dir = g_volume_overlay_direction > 0 ? "+" : "-";
    int dir_w = text_width(g_font_bold, sf(18), dir);
    draw_text_clip(g_font_bold, sf(18), bar_x - sx(3) + (bar_w - dir_w) / 2, bar_y - sy(14), dir, g_cfg.ui.color_text_main, alpha,
                   bar_x - sx(20), 0, g_w, g_h);

    char label[16];
    snprintf(label, sizeof(label), "%d", vol);
    int lw = text_width(g_font_bold, sf(14), label);
    draw_text_clip(g_font_bold, sf(14), bar_x - sx(3) + (bar_w - lw) / 2, bar_y + bar_h + sy(20), label, g_cfg.ui.color_text_main, alpha,
                   bar_x - sx(28), 0, g_w, g_h);
}

static void draw_truth_footer(const PlayerState *s, const AudioInfo *a, bool clock_mode, double now) {
    int h = sy(g_cfg.ui.footer_height);
    int y = g_h - h;
    int px = sf(g_cfg.ui.footer_font_size);
    int by = y + sy(16);

    char left[96];
    char mid[64];
    char right[96];

    format_source_truth(s, left, sizeof(left));
    if (clock_mode) mid[0] = '\0';
    else format_clock_countdown(now, mid, sizeof(mid));
    compact_audio_format(a, right, sizeof(right));

    fill_rect(0, y, g_w, h, g_cfg.ui.color_panel);
    fill_rect(0, y, g_w, 1, g_cfg.ui.color_panel_line);

    draw_text_clip(g_font_reg, px, sx(8), by, left, g_cfg.ui.color_text_dim, 255, sx(6), y, g_w / 2 - sx(4), g_h);

    if (mid[0]) {
        int mw = text_width(g_font_reg, px, mid);
        draw_text_clip(g_font_reg, px, g_w / 2 - mw / 2, by, mid, g_cfg.ui.color_text_muted, 255, g_w / 3, y, (2 * g_w) / 3, g_h);
    }

    int rw = text_width(g_font_reg, px, right);
    draw_text_clip(g_font_reg, px, g_w - sx(8) - rw, by, right, g_cfg.ui.color_text_muted, 255, g_w / 2, y, g_w - sx(6), g_h);
}

static void draw_progress(int x, int y, int w, int h, double seek, double dur) {
    fill_rect(x, y, w, h, g_cfg.ui.color_progress_bg);
    if (dur > 0.1) fill_rect(x, y, clampi((int)(w * seek / dur), 0, w), h, g_cfg.ui.color_progress_fg);
}

/*
 * Intelligent text bounds.
 *
 * Text should be clipped to the real usable layout region, not to magic
 * coordinates. The right edge preserves normal padding and also reserves a
 * slim safety lane for the vertical volume overlay so long titles never draw
 * under the right-side bar when volume changes.
 */
static int ui_text_right_edge(void) {
    int p = sx(g_cfg.ui.padding);
    int vol_lane = sx(g_cfg.ui.volume_bar_width + g_cfg.ui.volume_bar_right_margin + 14);
    int reserve = max_i(p, vol_lane);
    int right = g_w - reserve;
    if (right < p + sx(24)) right = g_w - p;
    return clampi(right, p + 1, g_w - p);
}

static int ui_text_width_from(int x) {
    int p = sx(g_cfg.ui.padding);
    int left = clampi(x, p, max_i(p, g_w - p));
    int right = ui_text_right_edge();
    if (right <= left + sx(24)) right = g_w - p;
    return max_i(1, right - left);
}

static void format_elapsed(char *dst, size_t dst_sz, double s) {
    int v = (int)(s + 0.5);
    snprintf(dst, dst_sz, "%d:%02d", v / 60, v % 60);
}

static void free_scroll_strip(void) {
    free(g_scroll_strip_alpha);
    g_scroll_strip_alpha = NULL;
    g_scroll_strip_w = 0;
    g_scroll_strip_h = 0;
    g_scroll_strip_cycle_w = 0;
    g_scroll_strip_text[0] = 0;
}

static void strip_blend_glyph(uint8_t *strip, int sw, int sh, FT_Face face, int px, int x, int baseline, const char *text) {
    int pen_x = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        GlyphCacheEntry *g = glyph_cache_get(face, px, *p);
        if (!g) continue;
        int gx = pen_x + g->left;
        int gy = baseline - g->top;
        for (int row = 0; row < g->height; row++) {
            int yy = gy + row;
            if (yy < 0 || yy >= sh) continue;
            for (int col = 0; col < g->width; col++) {
                int xx = gx + col;
                if (xx < 0 || xx >= sw) continue;
                uint8_t a = g->alpha[(size_t)row * g->width + col];
                uint8_t *dp = strip + (size_t)yy * sw + xx;
                if (a > *dp) *dp = a;
            }
        }
        pen_x += g->advance;
    }
}

static bool ensure_scroll_strip(FT_Face face, int px, const char *title, int clip_w, int clip_h, uint32_t color) {
    if (!face || !title || !*title || px <= 0 || clip_w <= 0 || clip_h <= 0) return false;
    if (g_scroll_strip_alpha && g_scroll_strip_face == face && g_scroll_strip_px == px &&
        g_scroll_strip_clip_w == clip_w && g_scroll_strip_h == clip_h &&
        g_scroll_strip_color == color && strcmp(g_scroll_strip_text, title) == 0) return true;

    free_scroll_strip();
    int gap = sx(60);
    int title_w = text_width(face, px, title);
    int cycle = title_w + gap;
    int strip_w = cycle + title_w;
    if (strip_w < clip_w + cycle) strip_w = clip_w + cycle;
    if (strip_w <= 0) return false;

    g_scroll_strip_alpha = calloc((size_t)strip_w * (size_t)clip_h, 1);
    if (!g_scroll_strip_alpha) return false;

    g_scroll_strip_w = strip_w;
    g_scroll_strip_h = clip_h;
    g_scroll_strip_cycle_w = cycle;
    g_scroll_strip_px = px;
    g_scroll_strip_clip_w = clip_w;
    g_scroll_strip_color = color;
    g_scroll_strip_face = face;
    safe_copy(g_scroll_strip_text, sizeof(g_scroll_strip_text), title);

    int baseline = px;
    strip_blend_glyph(g_scroll_strip_alpha, strip_w, clip_h, face, px, 0, baseline, title);
    strip_blend_glyph(g_scroll_strip_alpha, strip_w, clip_h, face, px, cycle, baseline, title);
    return true;
}

static void draw_scrolling_title(int x, int y, int w, int h, const char *title, bool scroll, bool moving, double now) {
    int px = sf(g_cfg.ui.title_font_size);
    if (!scroll) {
        draw_text_clip(g_font_bold, px, x, y, title, g_cfg.ui.color_text_main, 255, x, y - px, x + w, y + h);
        return;
    }

    int strip_h = h + px;
    if (!ensure_scroll_strip(g_font_bold, px, title, w, strip_h, g_cfg.ui.color_text_main)) return;
    double speed = sx(42);
    int off = moving ? (int)fmod(now * speed, (double)max_i(1, g_scroll_strip_cycle_w)) : 0;
    int target_y = y - px;

    for (int row = 0; row < strip_h; row++) {
        int yy = target_y + row;
        if (yy < 0 || yy >= g_h) continue;
        for (int col = 0; col < w; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= g_w) continue;
            int src_x = off + col;
            if (src_x >= g_scroll_strip_w) src_x %= max_i(1, g_scroll_strip_cycle_w);
            uint8_t a = g_scroll_strip_alpha[(size_t)row * g_scroll_strip_w + src_x];
            if (a) blend_pixel(xx, yy, g_cfg.ui.color_text_main, a);
        }
    }
}

static void draw_spotify_screen(const PlayerState *s, const AudioInfo *a, bool audio_running, double now) {
    int p = sx(g_cfg.ui.padding);
    int header_h = sy(g_cfg.ui.header_height);
    int art = sy(g_cfg.ui.album_art_size);
    int ax = p;
    int ay = header_h + sy(26);
    int tx = ax + art + p;
    int tw = ui_text_width_from(tx);
    int title_px = sf(g_cfg.ui.title_font_size);
    bool title_scrolls = text_width(g_font_bold, title_px, s->title) > tw;
    bool fade_bg = g_cfg.album_bg_fade_when_title_scrolls && title_scrolls;

    draw_album_background(fade_bg);
    fill_rect(0, 0, g_w, header_h, g_cfg.ui.color_background);
    draw_clock_text(g_w - sx(52), header_h - sy(9), sf(g_cfg.ui.small_clock_font_size), g_cfg.ui.color_text_dim, now);
    draw_text(g_font_reg, sf(g_cfg.ui.source_font_size), sx(14), header_h - sy(10), "Spotify", g_cfg.ui.color_text_dim);

    if (g_art_rgba) draw_cached_album_art(ax, ay, art, art);
    else {
        fill_rect(ax, ay, art, art, g_cfg.ui.color_album_placeholder);
        draw_text(g_font_reg, sf(g_cfg.ui.source_font_size), ax + sx(25), ay + art / 2, "No Art", g_cfg.ui.color_text_muted);
    }

    draw_scrolling_title(tx, sy(82), tw, sy(34), s->title[0] ? s->title : "Spotify", title_scrolls, audio_running, now);
    draw_text_clip(g_font_reg, sf(g_cfg.ui.artist_font_size), tx, sy(118), s->artist, g_cfg.ui.color_text_dim, 255, tx, 0, tx + tw, g_h);
    draw_text_clip(g_font_reg, sf(g_cfg.ui.album_font_size), tx, sy(145), s->album, g_cfg.ui.color_text_muted, 255, tx, 0, tx + tw, g_h);

    int py = sy(225);
    int bar_h = sy(g_cfg.ui.progress_bar_height);
    draw_progress(p + sx(4), py, g_w - (p + sx(4)) * 2, bar_h, s->seek, s->duration);

    char left[32], right[32];
    format_elapsed(left, sizeof(left), s->seek);
    format_elapsed(right, sizeof(right), s->duration);
    int time_px = sf(g_cfg.ui.footer_font_size + 2);
    draw_text(g_font_reg, time_px, p + sx(4), py + sy(24), left, g_cfg.ui.color_text_dim);
    draw_text(g_font_reg, time_px, g_w - p - sx(4) - text_width(g_font_reg, time_px, right), py + sy(24), right, g_cfg.ui.color_text_dim);
    draw_truth_footer(s, a, false, now);
}

static void draw_airplay_screen(const PlayerState *s, const AudioInfo *a, double now) {
    (void)now;
    clear_screen(g_cfg.ui.color_background);
    int cx = g_w / 2;
    int cy = g_h / 2 - sy(18);

    int screen_w = sx(g_cfg.ui.airplay_icon_width);
    int screen_h = sy(g_cfg.ui.airplay_icon_height);
    int sx0 = cx - screen_w / 2;
    int sy0 = cy - screen_h / 2;

    fill_rect(sx0, sy0, screen_w, screen_h, g_cfg.ui.color_album_placeholder);
    fill_rect(sx0 + sx(6), sy0 + sy(6), screen_w - sx(12), screen_h - sy(12), g_cfg.ui.color_background);
    fill_rect(sx0 + sx(6), sy0 + sy(6), screen_w - sx(12), sy(2), g_cfg.ui.color_panel_line);

    int tri_top = sy0 + screen_h + sy(8);
    int tri_h = sy(42);
    for (int row = 0; row < tri_h; row++) {
        int half = row * sx(38) / max_i(1, tri_h);
        fill_rect(cx - half, tri_top + row, half * 2 + 1, 1, g_cfg.ui.color_text_main);
    }

    char fmt[96];
    compact_audio_format(a, fmt, sizeof(fmt));
    if (fmt[0] && strcmp(fmt, "closed") != 0) {
        int px = sf(g_cfg.ui.source_font_size);
        draw_text(g_font_reg, px, cx - text_width(g_font_reg, px, fmt) / 2, cy + sy(112), fmt, g_cfg.ui.color_text_muted);
    }

    (void)s;
}

static void draw_generic_screen(const PlayerState *s, const AudioInfo *a, bool audio_running, double now) {
    int p = sx(g_cfg.ui.padding);
    int header_h = sy(g_cfg.ui.header_height);
    clear_screen(g_cfg.ui.color_background);
    draw_clock_text(g_w - sx(52), header_h - sy(9), sf(g_cfg.ui.small_clock_font_size), g_cfg.ui.color_text_dim, now);
    const char *src = s->service[0] ? s->service : "Volumio";
    draw_text(g_font_reg, sf(g_cfg.ui.source_font_size), sx(14), header_h - sy(10), src, g_cfg.ui.color_text_dim);

    int art = sy(g_cfg.ui.generic_album_art_size);
    int ax = p;
    int ay = header_h + sy(32);
    if (g_art_rgba) draw_cached_album_art(ax, ay, art, art);

    int tx = ax + art + p;
    int tw = ui_text_width_from(tx);
    int title_px = sf(g_cfg.ui.title_font_size);
    bool scroll = text_width(g_font_bold, title_px, s->title) > tw;
    draw_scrolling_title(tx, sy(92), tw, sy(36), s->title[0] ? s->title : src, scroll, audio_running, now);
    draw_text_clip(g_font_reg, sf(g_cfg.ui.artist_font_size), tx, sy(126), s->artist, g_cfg.ui.color_text_dim, 255, tx, 0, tx + tw, g_h);
    draw_text_clip(g_font_reg, sf(g_cfg.ui.album_font_size), tx, sy(153), s->album, g_cfg.ui.color_text_muted, 255, tx, 0, tx + tw, g_h);
    draw_progress(p + sx(4), sy(225), g_w - (p + sx(4)) * 2, sy(g_cfg.ui.progress_bar_height), s->seek, s->duration);
    draw_truth_footer(s, a, false, now);
}

static void draw_idle(double now) {
    clear_screen(g_cfg.ui.color_background);

    draw_clock_text(g_w / 2, g_h / 2 + sy(26), sf(g_cfg.ui.idle_clock_font_size), g_cfg.ui.color_text_main, now);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);

    char date[64];
    strftime(date, sizeof(date), "%A, %B %d", &tmv);
    int date_px = sf(g_cfg.ui.idle_date_font_size);
    draw_text(g_font_reg, date_px,
              g_w / 2 - text_width(g_font_reg, date_px, date) / 2,
              g_h / 2 + sy(70), date, g_cfg.ui.color_text_dim);

    char ip[64];
    get_primary_ipv4(ip, sizeof(ip));
    int ip_px = sf(g_cfg.ui.idle_ip_font_size);
    draw_text(g_font_reg, ip_px,
              g_w / 2 - text_width(g_font_reg, ip_px, ip) / 2,
              g_h / 2 + sy(102), ip, g_cfg.ui.color_text_muted);
}

static void cleanup(void) {
    clear_art();
    free(g_bg_rgb);
    free_scroll_strip();
    for (int i = 0; i < GLYPH_CACHE_SLOTS; i++) free(g_glyph_cache[i].alpha);
    if (g_curl) curl_easy_cleanup(g_curl);
    if (g_font_bold) FT_Done_Face(g_font_bold);
    if (g_font_reg) FT_Done_Face(g_font_reg);
    if (g_ft) FT_Done_FreeType(g_ft);
    if (g_fb) munmap(g_fb, g_fb_bytes);
    if (g_fb_fd >= 0) close(g_fb_fd);
    free(g_img);
    free(g_prev_img);
    free(g_dirty_spans);
}

int main(void) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_config();
    memset(&g_state, 0, sizeof(g_state));
    safe_copy(g_state.state, sizeof(g_state.state), "stop");
    safe_copy(g_state.service, sizeof(g_state.service), "mpd");

    if (!fb_init()) return 1;
    if (!fonts_init()) return 1;

    while (g_running) {
        double n = now_seconds();
        if (n - g_last_volumio_fetch >= 0.50) {
            fetch_volumio(&g_state);
            g_last_volumio_fetch = n;
        }
        if (n - g_last_alsa_fetch >= 0.20) {
            read_alsa(&g_audio);
            g_last_alsa_fetch = n;
        }

        update_volume_overlay(&g_state, n);

        bool real_idle = is_real_idle(&g_state);
        bool clock_mode = false;
        if (real_idle) {
            g_audio_stopped_at = -1.0;
            clock_mode = true;
        } else if (g_audio.running) {
            g_audio_stopped_at = -1.0;
            clock_mode = false;
        } else {
            if (g_audio_stopped_at < 0.0) g_audio_stopped_at = n;
            clock_mode = (n - g_audio_stopped_at) >= g_cfg.return_to_clock_seconds;
        }

        update_art_for_state(&g_state, clock_mode);

        if (clock_mode) draw_idle(n);
        else if (is_airplay(&g_state)) draw_airplay_screen(&g_state, &g_audio, n);
        else if (is_spotify(&g_state)) draw_spotify_screen(&g_state, &g_audio, g_audio.running, n);
        else draw_generic_screen(&g_state, &g_audio, g_audio.running, n);

        draw_volume_overlay(n, clock_mode);

        fb_flush();

        bool scroll = false;
        if (!clock_mode && !is_airplay(&g_state) && g_audio.running) {
            int p = sx(g_cfg.ui.padding);
            int art = sy(is_spotify(&g_state) ? g_cfg.ui.album_art_size : g_cfg.ui.generic_album_art_size);
            int tx = p + art + p;
            int tw = ui_text_width_from(tx);
            scroll = text_width(g_font_bold, sf(g_cfg.ui.title_font_size), g_state.title) > tw;
        }

        bool animating = scroll || g_cfg.colon_alpha_enabled || volume_overlay_active(n);
        double delay;
        if (animating) delay = 1.0 / 30.0;
        else if (!clock_mode || g_audio.running) delay = 1.0;
        else delay = 5.0;
        sleep_seconds(delay);
    }

    cleanup();
    curl_global_cleanup();
    return 0;
}
