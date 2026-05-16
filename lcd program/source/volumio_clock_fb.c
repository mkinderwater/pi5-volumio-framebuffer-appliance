#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <json-c/json.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// ---------------- Configuration ----------------

#define FB_PATH "/dev/fb0"
#define WIDTH 480
#define HEIGHT 320
#define FB_BYTES (WIDTH * HEIGHT * 2)

#define VOLUMIO_HOST "127.0.0.1"
#define VOLUMIO_PORT 3000
#define VOLUMIO_PATH "/api/v1/getState"

#define DRAW_INTERVAL_IDLE 1.0
#define DRAW_INTERVAL_PLAYING 1.0
#define DRAW_INTERVAL_SCROLLING 0.14
#define DRAW_INTERVAL_ANIMATING 0.04

#define LAYOUT_ANIM_SECONDS 0.45
#define TRANSITION_FEEDBACK_SECONDS 0.65

#define VOLUMIO_REFRESH_IDLE 1.0
#define VOLUMIO_REFRESH_ACTIVE 0.5
#define IP_REFRESH 60.0
#define CURSOR_HIDE_REFRESH 5.0
#define HTTP_TIMEOUT_MS 1000L
#define HTTP_MAX_BYTES 65536

#define SCROLL_SPEED 40.0
#define SCROLL_GAP 120

#define FONT_BOLD "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
#define FONT_REG  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

// ---------------- Palette ----------------

typedef struct { uint8_t r, g, b; } Color;

static const Color ACCENT      = {30, 215, 96};
static const Color BG_COLOR    = {18, 18, 18};
static const Color TEXT_MAIN   = {255, 255, 255};
static const Color TEXT_DIM    = {90, 90, 90};
static const Color DIVIDER     = {35, 35, 35};
static const Color RED_ALERT   = {255, 82, 82};
static const Color COLON_DIM   = {55, 55, 55};
static const Color BAR_BG      = {40, 40, 40};

// ---------------- Runtime State ----------------

static volatile sig_atomic_t g_running = 1;

static int g_fb_fd = -1;
static uint16_t *g_fb = NULL;

static uint8_t g_img[HEIGHT][WIDTH][3];
static uint16_t g_rgb565[HEIGHT][WIDTH];

static FT_Library g_ft;
static FT_Face g_font_bold;
static FT_Face g_font_reg;

static char g_last_ip[64] = "No Network";
static double g_last_ip_check = -9999.0;

static double g_last_status_check = -9999.0;
static double g_last_status_rx = -9999.0;
static double g_last_cursor_hide = -9999.0;

static bool g_status_ok = false;
static char g_status_state[32] = "offline";
static char g_status_title[512] = "";
static char g_status_artist[512] = "";
static double g_status_seek_ms = 0.0;
static double g_status_duration = 0.0;

static char g_display_title[512] = "";
static char g_display_artist[512] = "";

static double g_layout_anim = 0.0;
static double g_last_layout_anim_update = -9999.0;
static bool g_layout_target_playing = false;
static bool g_layout_target_seen = false;
static double g_transition_started = -9999.0;
static bool g_transition_to_playing = false;

// ---------------- Time Helpers ----------------

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void sleep_seconds(double seconds) {
    if (seconds < 0.001) seconds = 0.001;

    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1000000000.0);

    while (nanosleep(&req, &req) == -1 && errno == EINTR && g_running) {}
}

static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static double ease_smoothstep(double v) {
    v = clamp01(v);
    return v * v * (3.0 - (2.0 * v));
}

static double lerp_double(double a, double b, double t) {
    return a + ((b - a) * clamp01(t));
}

static int lerp_int(int a, int b, double t) {
    return (int)(lerp_double((double)a, (double)b, t) + 0.5);
}

static Color color_mix(Color a, Color b, double t) {
    t = clamp01(t);

    Color out;
    out.r = (uint8_t)lerp_int(a.r, b.r, t);
    out.g = (uint8_t)lerp_int(a.g, b.g, t);
    out.b = (uint8_t)lerp_int(a.b, b.b, t);
    return out;
}

static bool update_layout_animation(bool target_playing, double now) {
    if (!g_layout_target_seen) {
        g_layout_target_seen = true;
        g_layout_target_playing = target_playing;
        g_transition_to_playing = target_playing;
        g_transition_started = now;
    } else if (target_playing != g_layout_target_playing) {
        g_layout_target_playing = target_playing;
        g_transition_to_playing = target_playing;
        g_transition_started = now;
    }

    if (g_last_layout_anim_update < 0.0) {
        g_last_layout_anim_update = now;
    }

    double dt = now - g_last_layout_anim_update;
    g_last_layout_anim_update = now;

    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;

    double step = dt / LAYOUT_ANIM_SECONDS;

    if (target_playing) {
        g_layout_anim = clamp01(g_layout_anim + step);
    } else {
        g_layout_anim = clamp01(g_layout_anim - step);
    }

    return g_layout_anim > 0.001 && g_layout_anim < 0.999;
}

static void on_signal(int signo) {
    (void)signo;
    g_running = 0;
}

// ---------------- Text Helpers ----------------

static void str_lower_copy(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;

    size_t i = 0;
    for (; src && src[i] && i + 1 < dst_sz; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static bool is_unknown_value(const char *s) {
    if (!s) return true;

    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return true;

    char tmp[128];
    str_lower_copy(tmp, sizeof(tmp), s);

    char *end = tmp + strlen(tmp);
    while (end > tmp && isspace((unsigned char)*(end - 1))) {
        *(--end) = '\0';
    }

    return strcmp(tmp, "unknown") == 0 ||
           strcmp(tmp, "unknown track") == 0 ||
           strcmp(tmp, "unknown artist") == 0 ||
           strcmp(tmp, "null") == 0 ||
           strcmp(tmp, "none") == 0;
}

static void safe_text(char *dst, size_t dst_sz, const char *src, const char *fallback) {
    if (!dst || dst_sz == 0) return;

    if (is_unknown_value(src)) {
        snprintf(dst, dst_sz, "%s", fallback ? fallback : "");
        return;
    }

    snprintf(dst, dst_sz, "%s", src ? src : "");
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

static void normalize_playback_state(char *dst, size_t dst_sz, const char *raw_state) {
    char tmp[64];
    safe_text(tmp, sizeof(tmp), raw_state, "stop");
    trim_in_place(tmp);

    for (char *p = tmp; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }

    if (strcmp(tmp, "play") == 0 || strcmp(tmp, "playing") == 0) {
        snprintf(dst, dst_sz, "play");
    } else if (strcmp(tmp, "pause") == 0 || strcmp(tmp, "paused") == 0) {
        snprintf(dst, dst_sz, "pause");
    } else if (strcmp(tmp, "stop") == 0 || strcmp(tmp, "stopped") == 0 ||
               strcmp(tmp, "idle") == 0 || strcmp(tmp, "stopping") == 0) {
        snprintf(dst, dst_sz, "stop");
    } else {
        snprintf(dst, dst_sz, "%s", tmp[0] ? tmp : "stop");
    }
}

static void uppercase_ascii(char *s) {
    if (!s) return;
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static void format_duration(char *dst, size_t dst_sz, double seconds) {
    int s = (int)(seconds < 0 ? 0 : seconds);
    snprintf(dst, dst_sz, "%d:%02d", s / 60, s % 60);
}

static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)(*p);

    if (*s == 0) return 0;

    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *p += 1;
        return cp;
    }

    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *p += 2;
        return cp;
    }

    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) |
             ((uint32_t)(s[1] & 0x3F) << 6) |
             (uint32_t)(s[2] & 0x3F);
        *p += 3;
        return cp;
    }

    if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) |
             ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) |
             (uint32_t)(s[3] & 0x3F);
        *p += 4;
        return cp;
    }

    *p += 1;
    return '?';
}

// ---------------- Framebuffer ----------------

static bool open_framebuffer(void) {
    if (g_fb_fd >= 0 && g_fb) return true;

    g_fb_fd = open(FB_PATH, O_RDWR | O_CLOEXEC);
    if (g_fb_fd < 0) {
        perror("open framebuffer");
        return false;
    }

    g_fb = mmap(NULL, FB_BYTES, PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
    if (g_fb == MAP_FAILED) {
        perror("mmap framebuffer");
        g_fb = NULL;
        close(g_fb_fd);
        g_fb_fd = -1;
        return false;
    }

    return true;
}

static void close_framebuffer(void) {
    if (g_fb && g_fb != MAP_FAILED) {
        munmap(g_fb, FB_BYTES);
        g_fb = NULL;
    }

    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
}

static void write_framebuffer(void) {
    if (!g_fb && !open_framebuffer()) return;

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            uint8_t r = g_img[y][x][0];
            uint8_t g = g_img[y][x][1];
            uint8_t b = g_img[y][x][2];

            g_rgb565[y][x] = (uint16_t)(((r & 0xF8) << 8) |
                                        ((g & 0xFC) << 3) |
                                        (b >> 3));
        }
    }

    memcpy(g_fb, g_rgb565, FB_BYTES);
}

// ---------------- Drawing Primitives ----------------

static inline void put_pixel(int x, int y, Color c) {
    if ((unsigned)x >= WIDTH || (unsigned)y >= HEIGHT) return;
    g_img[y][x][0] = c.r;
    g_img[y][x][1] = c.g;
    g_img[y][x][2] = c.b;
}

static inline void blend_pixel(int x, int y, Color c, uint8_t alpha) {
    if ((unsigned)x >= WIDTH || (unsigned)y >= HEIGHT || alpha == 0) return;

    if (alpha == 255) {
        put_pixel(x, y, c);
        return;
    }

    uint8_t *p = g_img[y][x];
    uint16_t inv = (uint16_t)(255 - alpha);

    p[0] = (uint8_t)(((uint16_t)c.r * alpha + (uint16_t)p[0] * inv) / 255);
    p[1] = (uint8_t)(((uint16_t)c.g * alpha + (uint16_t)p[1] * inv) / 255);
    p[2] = (uint8_t)(((uint16_t)c.b * alpha + (uint16_t)p[2] * inv) / 255);
}

static void clear_image(Color c) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            g_img[y][x][0] = c.r;
            g_img[y][x][1] = c.g;
            g_img[y][x][2] = c.b;
        }
    }
}

static void fill_rect(int x0, int y0, int x1, int y1, Color c) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > WIDTH) x1 = WIDTH;
    if (y1 > HEIGHT) y1 = HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            put_pixel(x, y, c);
        }
    }
}

static void fill_rect_alpha(int x0, int y0, int x1, int y1, Color c, uint8_t alpha) {
    if (alpha == 0) return;
    if (alpha == 255) {
        fill_rect(x0, y0, x1, y1, c);
        return;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > WIDTH) x1 = WIDTH;
    if (y1 > HEIGHT) y1 = HEIGHT;
    if (x1 <= x0 || y1 <= y0) return;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            blend_pixel(x, y, c, alpha);
        }
    }
}

static void hline(int x0, int x1, int y, Color c) {
    if ((unsigned)y >= HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 > WIDTH) x1 = WIDTH;
    if (x1 <= x0) return;

    for (int x = x0; x < x1; x++) put_pixel(x, y, c);
}

static void rounded_rect_fill(int x0, int y0, int x1, int y1, int r, Color c) {
    if (x1 <= x0 || y1 <= y0) return;

    int w = x1 - x0;
    int h = y1 - y0;

    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r == 0) {
        fill_rect(x0, y0, x1, y1, c);
        return;
    }

    for (int y = y0; y < y1; y++) {
        int left = x0;
        int right = x1;

        int dy_top = y - y0;
        int dy_bottom = y1 - 1 - y;
        int dy = -1;

        if (dy_top < r) dy = r - 1 - dy_top;
        else if (dy_bottom < r) dy = r - 1 - dy_bottom;

        if (dy >= 0) {
            int dx = r - (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
            left += dx;
            right -= dx;
        }

        hline(left, right, y, c);
    }
}

static void rounded_rect_fill_alpha(int x0, int y0, int x1, int y1, int r, Color c, uint8_t alpha) {
    if (alpha == 0) return;
    if (alpha == 255) {
        rounded_rect_fill(x0, y0, x1, y1, r, c);
        return;
    }

    if (x1 <= x0 || y1 <= y0) return;

    int w = x1 - x0;
    int h = y1 - y0;

    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r == 0) {
        fill_rect_alpha(x0, y0, x1, y1, c, alpha);
        return;
    }

    for (int y = y0; y < y1; y++) {
        int left = x0;
        int right = x1;

        int dy_top = y - y0;
        int dy_bottom = y1 - 1 - y;
        int dy = -1;

        if (dy_top < r) dy = r - 1 - dy_top;
        else if (dy_bottom < r) dy = r - 1 - dy_bottom;

        if (dy >= 0) {
            int dx = r - (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
            left += dx;
            right -= dx;
        }

        fill_rect_alpha(left, y, right, y + 1, c, alpha);
    }
}

// ---------------- FreeType Text ----------------

static FT_Face pick_face(bool bold) {
    return bold ? g_font_bold : g_font_reg;
}

static int text_width_px(const char *text, bool bold, int size_px) {
    if (!text || !*text) return 0;

    FT_Face face = pick_face(bold);
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size_px);

    int width = 0;
    const char *p = text;

    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;

        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT) == 0) {
            width += (int)(face->glyph->advance.x >> 6);
        }
    }

    return width;
}

static void text_bounds_px(const char *text, bool bold, int size_px,
                           int *width_out, int *top_out, int *bottom_out) {
    if (!text || !*text) {
        if (width_out) *width_out = 0;
        if (top_out) *top_out = 0;
        if (bottom_out) *bottom_out = size_px;
        return;
    }

    FT_Face face = pick_face(bold);
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size_px);

    int pen_x = 0;
    int baseline_y = size_px;
    int min_y = 1000000;
    int max_y = -1000000;

    const char *p = text;

    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;

        if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot g = face->glyph;
            FT_Bitmap *bm = &g->bitmap;

            int gy = baseline_y - g->bitmap_top;
            int gb = gy + (int)bm->rows;

            if (bm->rows > 0) {
                if (gy < min_y) min_y = gy;
                if (gb > max_y) max_y = gb;
            }

            pen_x += (int)(g->advance.x >> 6);
        }
    }

    if (min_y == 1000000 || max_y < min_y) {
        min_y = 0;
        max_y = size_px;
    }

    if (width_out) *width_out = pen_x;
    if (top_out) *top_out = min_y;
    if (bottom_out) *bottom_out = max_y;
}

static int fit_clock_size_px(const char *clock_text,
                             int max_w, int max_h,
                             int min_size, int max_size) {
    int low = min_size;
    int high = max_size;
    int best = min_size;

    while (low <= high) {
        int mid = (low + high) / 2;

        int w = 0;
        int top = 0;
        int bottom = 0;

        text_bounds_px(clock_text, true, mid, &w, &top, &bottom);

        int h = bottom - top;

        if (w <= max_w && h <= max_h) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return best;
}

static void draw_text_px(int x, int y, const char *text, bool bold, int size_px, Color c) {
    if (!text || !*text) return;

    FT_Face face = pick_face(bold);
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size_px);

    int pen_x = x;
    int baseline_y = y + size_px;

    const char *p = text;

    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0) break;

        if (FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0) continue;

        FT_GlyphSlot g = face->glyph;
        FT_Bitmap *bm = &g->bitmap;

        int gx = pen_x + g->bitmap_left;
        int gy = baseline_y - g->bitmap_top;

        for (int row = 0; row < (int)bm->rows; row++) {
            for (int col = 0; col < (int)bm->width; col++) {
                uint8_t alpha = bm->buffer[row * bm->pitch + col];
                blend_pixel(gx + col, gy + row, c, alpha);
            }
        }

        pen_x += (int)(g->advance.x >> 6);
    }
}

static bool draw_scrolling_text(const char *raw_text, int y, bool bold, int size_px, Color fill, int margin_x) {
    char text[512];
    safe_text(text, sizeof(text), raw_text, "");

    if (!text[0]) return false;

    int max_w = WIDTH - (margin_x * 2);
    int tw = text_width_px(text, bold, size_px);

    if (tw <= max_w) {
        draw_text_px(margin_x, y, text, bold, size_px, fill);
        return false;
    }

    int total_cycle_w = tw + SCROLL_GAP;
    int offset = (int)fmod(monotonic_seconds() * SCROLL_SPEED, (double)total_cycle_w);

    draw_text_px(margin_x - offset, y, text, bold, size_px, fill);
    draw_text_px(margin_x - offset + total_cycle_w, y, text, bold, size_px, fill);

    int mask_h = 42;
    fill_rect(0, y, margin_x, y + mask_h, BG_COLOR);
    fill_rect(WIDTH - margin_x, y, WIDTH, y + mask_h, BG_COLOR);

    return true;
}

static bool draw_scrolling_text_centered(const char *raw_text, int y, bool bold, int size_px, Color fill, int margin_x) {
    char text[512];
    safe_text(text, sizeof(text), raw_text, "");

    if (!text[0]) return false;

    int max_w = WIDTH - (margin_x * 2);
    int tw = text_width_px(text, bold, size_px);

    if (tw <= max_w) {
        draw_text_px((WIDTH - tw) / 2, y, text, bold, size_px, fill);
        return false;
    }

    int total_cycle_w = tw + SCROLL_GAP;
    int offset = (int)fmod(monotonic_seconds() * SCROLL_SPEED, (double)total_cycle_w);

    draw_text_px(margin_x - offset, y, text, bold, size_px, fill);
    draw_text_px(margin_x - offset + total_cycle_w, y, text, bold, size_px, fill);

    int mask_h = size_px + 12;
    fill_rect(0, y, margin_x, y + mask_h, BG_COLOR);
    fill_rect(WIDTH - margin_x, y, WIDTH, y + mask_h, BG_COLOR);

    return true;
}

static void draw_transition_feedback(double anim_eased, double now) {
    double age = now - g_transition_started;

    if (age < 0.0 || age > TRANSITION_FEEDBACK_SECONDS) return;

    double t = clamp01(age / TRANSITION_FEEDBACK_SECONDS);
    double pulse = sin(t * 3.14159265358979323846);

    if (pulse <= 0.001) return;

    uint8_t alpha = (uint8_t)(190.0 * pulse);
    Color c = g_transition_to_playing ? ACCENT : TEXT_DIM;

    int y = lerp_int(HEIGHT - 36, 174, anim_eased);
    int w = 28 + (int)(155.0 * pulse);
    int h = 5;
    int x0 = (WIDTH - w) / 2;

    rounded_rect_fill_alpha(x0, y, x0 + w, y + h, h / 2, c, alpha);

    int dot_y = y + 15;
    int dot_gap = 16;
    int dot_size = 4;

    for (int i = -1; i <= 1; i++) {
        int dot_x = (WIDTH / 2) + (i * dot_gap);
        uint8_t dot_alpha = (uint8_t)((double)alpha * (i == 0 ? 0.95 : 0.55));
        rounded_rect_fill_alpha(dot_x - dot_size / 2, dot_y,
                                dot_x + dot_size / 2 + 1, dot_y + dot_size,
                                dot_size / 2, c, dot_alpha);
    }
}

// ---------------- Cursor ----------------

static void hide_console_cursor(bool force) {
    double now = monotonic_seconds();

    if (!force && now - g_last_cursor_hide < CURSOR_HIDE_REFRESH) return;
    g_last_cursor_hide = now;

    int fd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, "0\n", 2);
        close(fd);
    }

    const char *ttys[] = {"/dev/tty0", "/dev/tty1", "/dev/tty2"};

    for (size_t i = 0; i < sizeof(ttys) / sizeof(ttys[0]); i++) {
        fd = open(ttys[i], O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            (void)write(fd, "\033[?25l", 6);
            close(fd);
        }
    }
}

// ---------------- Network / Volumio ----------------

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} HttpBuffer;

static bool http_buffer_append(HttpBuffer *b, const void *ptr, size_t n) {
    if (!b || !ptr || n == 0) return true;

    if (b->len + n + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;

        while (new_cap < b->len + n + 1) new_cap *= 2;
        if (new_cap > HTTP_MAX_BYTES) return false;

        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return false;

        b->data = new_data;
        b->cap = new_cap;
    }

    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';

    return true;
}

static void http_buffer_free(HttpBuffer *b) {
    if (!b) return;

    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void set_socket_timeout_ms(int fd, long timeout_ms) {
    if (timeout_ms < 1) timeout_ms = 1;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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

static const char *find_crlf(const char *start, const char *end) {
    if (!start || !end || end <= start) return NULL;

    for (const char *p = start; p + 1 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n') return p;
    }

    return NULL;
}

static size_t parse_hex_chunk_size(const char *start, const char *end, bool *ok) {
    size_t value = 0;
    bool any = false;

    if (ok) *ok = false;

    for (const char *p = start; p < end; p++) {
        unsigned char ch = (unsigned char)*p;
        int digit = -1;

        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else if (ch == ';' || isspace(ch)) break;
        else return 0;

        any = true;
        value = (value * 16) + (size_t)digit;

        if (value > HTTP_MAX_BYTES) return 0;
    }

    if (ok) *ok = any;
    return value;
}

static char *decode_chunked_body(const char *body, size_t body_len, size_t *out_len) {
    const char *p = body;
    const char *end = body + body_len;

    char *out = malloc(body_len + 1);
    if (!out) return NULL;

    size_t used = 0;

    while (p < end) {
        const char *line_end = find_crlf(p, end);

        if (!line_end) {
            free(out);
            return NULL;
        }

        bool ok = false;
        size_t chunk_size = parse_hex_chunk_size(p, line_end, &ok);

        if (!ok) {
            free(out);
            return NULL;
        }

        p = line_end + 2;

        if (chunk_size == 0) break;

        if ((size_t)(end - p) < chunk_size + 2) {
            free(out);
            return NULL;
        }

        memcpy(out + used, p, chunk_size);
        used += chunk_size;
        p += chunk_size;

        if (p[0] != '\r' || p[1] != '\n') {
            free(out);
            return NULL;
        }

        p += 2;
    }

    out[used] = '\0';

    if (out_len) *out_len = used;
    return out;
}

static bool header_contains_token(const char *headers, const char *header_name, const char *token) {
    if (!headers || !header_name || !token) return false;

    const char *p = headers;
    size_t name_len = strlen(header_name);

    while ((p = strcasestr(p, header_name)) != NULL) {
        bool at_line_start = (p == headers) || (p[-1] == '\n');

        if (!at_line_start) {
            p += name_len;
            continue;
        }

        const char *line_end = strstr(p, "\r\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

        if (line_len > 511) line_len = 511;

        char line[512];
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        char line_lower[512];
        str_lower_copy(line_lower, sizeof(line_lower), line);

        char token_lower[128];
        str_lower_copy(token_lower, sizeof(token_lower), token);

        if (strstr(line_lower, token_lower)) return true;

        p += name_len;
    }

    return false;
}

static char *http_get_localhost_json(void) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;

    set_socket_timeout_ms(fd, HTTP_TIMEOUT_MS);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(VOLUMIO_PORT);

    if (inet_pton(AF_INET, VOLUMIO_HOST, &addr.sin_addr) != 1) {
        close(fd);
        return NULL;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }

    char request[512];

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n"
             "User-Agent: volumio-clock-fb/1.0\r\n"
             "\r\n",
             VOLUMIO_PATH, VOLUMIO_HOST, VOLUMIO_PORT);

    if (!send_all(fd, request, strlen(request))) {
        close(fd);
        return NULL;
    }

    HttpBuffer response = {0};
    char tmp[4096];

    while (g_running) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);

        if (n > 0) {
            if (!http_buffer_append(&response, tmp, (size_t)n)) {
                http_buffer_free(&response);
                close(fd);
                return NULL;
            }

            continue;
        }

        if (n == 0) break;

        if (errno == EINTR) continue;
        if ((errno == EAGAIN || errno == EWOULDBLOCK) && response.len > 0) break;

        http_buffer_free(&response);
        close(fd);
        return NULL;
    }

    close(fd);

    if (!response.data || response.len == 0) {
        http_buffer_free(&response);
        return NULL;
    }

    int status_code = 0;

    if (sscanf(response.data, "HTTP/%*s %d", &status_code) != 1 ||
        status_code < 200 || status_code >= 300) {
        http_buffer_free(&response);
        return NULL;
    }

    char *header_end = strstr(response.data, "\r\n\r\n");

    if (!header_end) {
        http_buffer_free(&response);
        return NULL;
    }

    char *body = header_end + 4;
    size_t body_len = response.len - (size_t)(body - response.data);

    *header_end = '\0';

    bool is_chunked = header_contains_token(response.data, "transfer-encoding:", "chunked");

    char *json_body = NULL;

    if (is_chunked) {
        json_body = decode_chunked_body(body, body_len, NULL);
    } else {
        json_body = malloc(body_len + 1);

        if (json_body) {
            memcpy(json_body, body, body_len);
            json_body[body_len] = '\0';
        }
    }

    http_buffer_free(&response);
    return json_body;
}

static void get_ip_cached(void) {
    double now = monotonic_seconds();

    if (now - g_last_ip_check < IP_REFRESH) return;
    g_last_ip_check = now;

    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        snprintf(g_last_ip, sizeof(g_last_ip), "Offline");
        return;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);

    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        snprintf(g_last_ip, sizeof(g_last_ip), "Offline");
        close(fd);
        return;
    }

    struct sockaddr_in local;
    socklen_t local_len = sizeof(local);

    if (getsockname(fd, (struct sockaddr *)&local, &local_len) == 0) {
        const char *ip = inet_ntop(AF_INET, &local.sin_addr, g_last_ip, sizeof(g_last_ip));

        if (!ip) snprintf(g_last_ip, sizeof(g_last_ip), "Offline");
    } else {
        snprintf(g_last_ip, sizeof(g_last_ip), "Offline");
    }

    close(fd);
}

static const char *json_get_string(json_object *obj, const char *key) {
    json_object *val = NULL;

    if (!json_object_object_get_ex(obj, key, &val) || !val) return "";

    const char *s = json_object_get_string(val);
    return s ? s : "";
}

static double json_get_double_default(json_object *obj, const char *key, double def) {
    json_object *val = NULL;

    if (!json_object_object_get_ex(obj, key, &val) || !val) return def;

    return json_object_get_double(val);
}

static void get_volumio_status_cached(void) {
    double now = monotonic_seconds();

    double refresh = VOLUMIO_REFRESH_IDLE;

    if (strcmp(g_status_state, "play") == 0 || strcmp(g_status_state, "pause") == 0) {
        refresh = VOLUMIO_REFRESH_ACTIVE;
    }

    if (now - g_last_status_check < refresh) return;
    g_last_status_check = now;

    char *body = http_get_localhost_json();

    if (!body) {
        g_status_ok = false;
        snprintf(g_status_state, sizeof(g_status_state), "offline");
        g_status_title[0] = '\0';
        g_status_artist[0] = '\0';
        g_status_seek_ms = 0.0;
        g_status_duration = 0.0;
        g_last_status_rx = now;
        return;
    }

    json_object *root = json_tokener_parse(body);
    free(body);

    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root) json_object_put(root);

        g_status_ok = false;
        snprintf(g_status_state, sizeof(g_status_state), "offline");
        g_last_status_rx = now;
        return;
    }

    normalize_playback_state(g_status_state, sizeof(g_status_state), json_get_string(root, "status"));

    safe_text(g_status_title, sizeof(g_status_title), json_get_string(root, "title"), "");
    safe_text(g_status_artist, sizeof(g_status_artist), json_get_string(root, "artist"), "");

    g_status_seek_ms = json_get_double_default(root, "seek", 0.0);
    g_status_duration = json_get_double_default(root, "duration", 0.0);

    if (strcmp(g_status_state, "play") != 0) {
        g_status_title[0] = '\0';
        g_status_artist[0] = '\0';
        g_status_seek_ms = 0.0;
        g_status_duration = 0.0;
    }

    g_status_ok = true;
    g_last_status_rx = monotonic_seconds();

    json_object_put(root);
}

static double estimated_seek_seconds(bool is_playing) {
    double seek = g_status_seek_ms / 1000.0;

    if (is_playing && g_status_duration > 0) {
        seek += fmax(0.0, monotonic_seconds() - g_last_status_rx);
    }

    if (g_status_duration > 0) {
        if (seek < 0.0) seek = 0.0;
        if (seek > g_status_duration) seek = g_status_duration;
    } else if (seek < 0.0) {
        seek = 0.0;
    }

    return seek;
}

// ---------------- Main Drawing ----------------

static bool draw_clock(bool *needs_scroll_out, bool *layout_animating_out) {
    hide_console_cursor(false);
    get_volumio_status_cached();
    get_ip_cached();

    bool needs_scroll = false;
    double now_mono = monotonic_seconds();

    time_t now_time = time(NULL);
    struct tm now_tm;
    localtime_r(&now_time, &now_tm);

    char state[32];

    if (g_status_ok) normalize_playback_state(state, sizeof(state), g_status_state);
    else snprintf(state, sizeof(state), "offline");

    bool has_track_details = g_status_title[0] || g_status_artist[0] || g_status_duration > 0.0;
    bool is_playing = strcmp(state, "play") == 0 && has_track_details;

    if (is_playing) {
        if (g_status_title[0]) snprintf(g_display_title, sizeof(g_display_title), "%s", g_status_title);
        if (g_status_artist[0]) snprintf(g_display_artist, sizeof(g_display_artist), "%s", g_status_artist);
    } else {
        g_display_title[0] = '\0';
        g_display_artist[0] = '\0';
    }

    bool layout_animating = update_layout_animation(is_playing, now_mono);
    double anim = ease_smoothstep(g_layout_anim);
    bool show_playing_ui = anim > 0.015 || is_playing;

    clear_image(BG_COLOR);

    if (show_playing_ui) {
        char date_str[64];
        strftime(date_str, sizeof(date_str), "%a, %b %e", &now_tm);
        uppercase_ascii(date_str);

        Color header_text = color_mix(BG_COLOR, TEXT_DIM, anim);
        Color header_ip = color_mix(BG_COLOR,
                                    strcmp(g_last_ip, "Offline") == 0 ? RED_ALERT : TEXT_DIM,
                                    anim);
        Color divider = color_mix(BG_COLOR, DIVIDER, anim);

        draw_text_px(25, 12, date_str, true, 11, header_text);

        int ip_w = text_width_px(g_last_ip, false, 11);

        draw_text_px(WIDTH - ip_w - 25, 12, g_last_ip, false, 11, header_ip);

        hline(25, WIDTH - 25, 32, divider);
    }

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%I:%M", &now_tm);

    if (time_str[0] == '0') {
        memmove(time_str, time_str + 1, strlen(time_str));
    }

    char hh[8] = {0};
    char mm[8] = {0};

    char *colon = strchr(time_str, ':');

    if (colon) {
        *colon = '\0';
        snprintf(hh, sizeof(hh), "%s", time_str);
        snprintf(mm, sizeof(mm), "%s", colon + 1);
    } else {
        snprintf(hh, sizeof(hh), "%s", time_str);
        snprintf(mm, sizeof(mm), "00");
    }

    char full_clock[16];
    snprintf(full_clock, sizeof(full_clock), "%s:%s", hh, mm);

    const int idle_margin_x = 4;
    const int idle_margin_y = 0;

    int idle_size = fit_clock_size_px(
        full_clock,
        WIDTH - (idle_margin_x * 2),
        HEIGHT - (idle_margin_y * 2),
        90,
        260
    );

    int box_w = 0;
    int box_top = 0;
    int box_bottom = 0;

    text_bounds_px(full_clock, true, idle_size, &box_w, &box_top, &box_bottom);

    int visual_h = box_bottom - box_top;
    int visual_top = idle_margin_y + ((HEIGHT - (idle_margin_y * 2) - visual_h) / 2);
    int idle_clock_y = visual_top - box_top;

    const int playing_time_size = 106;
    const int playing_clock_y = 48;
    const int playing_song_y = 178;

    int time_size = lerp_int(idle_size, playing_time_size, anim);
    int clock_y = lerp_int(idle_clock_y, playing_clock_y, anim);
    int song_y = lerp_int(HEIGHT + 8, playing_song_y, anim);

    int hh_w = text_width_px(hh, true, time_size);
    int col_w = text_width_px(":", true, time_size);
    int mm_w = text_width_px(mm, true, time_size);
    int clock_w = hh_w + col_w + mm_w;
    int start_x = (WIDTH - clock_w) / 2;

    bool colon_on = ((int)now_mono % 2) == 0;
    Color colon_color = colon_on ? TEXT_MAIN : COLON_DIM;

    draw_text_px(start_x, clock_y, hh, true, time_size, TEXT_MAIN);
    draw_text_px(start_x + hh_w, clock_y, ":", true, time_size, colon_color);
    draw_text_px(start_x + hh_w + col_w, clock_y, mm, true, time_size, TEXT_MAIN);

    draw_transition_feedback(anim, now_mono);

    if (show_playing_ui) {
        Color title_color = color_mix(BG_COLOR, ACCENT, anim);
        Color artist_color = color_mix(BG_COLOR, TEXT_MAIN, anim);
        Color progress_bg = color_mix(BG_COLOR, BAR_BG, anim);
        Color progress_fg = color_mix(BG_COLOR, ACCENT, anim);
        Color progress_text = color_mix(BG_COLOR, TEXT_DIM, anim);

        if (g_display_title[0]) {
            needs_scroll |= draw_scrolling_text_centered(g_display_title, song_y, true, 28, title_color, 25);
        }

        if (g_display_artist[0]) {
            needs_scroll |= draw_scrolling_text_centered(g_display_artist, song_y + 38, false, 19, artist_color, 25);
        }

        double duration = g_status_duration;
        double seek = estimated_seek_seconds(is_playing);

        if (is_playing && anim > 0.06) {
            int bar_y = HEIGHT - 50;
            int bar_w = WIDTH - 50;
            int bar_h = 6;

            rounded_rect_fill(25, bar_y, 25 + bar_w, bar_y + bar_h, 3, progress_bg);

            if (duration > 0.0) {
                double prog = seek / duration;

                if (prog < 0.0) prog = 0.0;
                if (prog > 1.0) prog = 1.0;

                int prog_w = (int)((double)bar_w * prog);

                if (prog_w > 0) {
                    rounded_rect_fill(25, bar_y, 25 + prog_w, bar_y + bar_h, 3, progress_fg);
                }

                char curr_t[32];
                char total_t[32];

                format_duration(curr_t, sizeof(curr_t), seek);
                format_duration(total_t, sizeof(total_t), duration);

                int time_y = bar_y + 12;

                draw_text_px(25, time_y, curr_t, false, 11, progress_text);

                int total_w = text_width_px(total_t, false, 11);
                draw_text_px(WIDTH - 25 - total_w, time_y, total_t, false, 11, progress_text);
            } else {
                int sweep_w = 95;
                int travel_w = bar_w + sweep_w;
                int offset = (int)fmod(now_mono * 120.0, (double)travel_w) - sweep_w;
                int x0 = 25 + offset;
                int x1 = x0 + sweep_w;

                if (x0 < 25) x0 = 25;
                if (x1 > 25 + bar_w) x1 = 25 + bar_w;

                if (x1 > x0) {
                    rounded_rect_fill(x0, bar_y, x1, bar_y + bar_h, 3, progress_fg);
                }
            }
        }
    }

    write_framebuffer();

    if (needs_scroll_out) *needs_scroll_out = needs_scroll;
    if (layout_animating_out) *layout_animating_out = layout_animating;

    return is_playing;
}

// ---------------- Init / Cleanup ----------------

static bool init_all(void) {
    if (FT_Init_FreeType(&g_ft) != 0) {
        fprintf(stderr, "Failed to initialize FreeType\n");
        return false;
    }

    if (FT_New_Face(g_ft, FONT_BOLD, 0, &g_font_bold) != 0) {
        fprintf(stderr, "Failed to load bold font: %s\n", FONT_BOLD);
        return false;
    }

    if (FT_New_Face(g_ft, FONT_REG, 0, &g_font_reg) != 0) {
        fprintf(stderr, "Failed to load regular font: %s\n", FONT_REG);
        return false;
    }

    if (!open_framebuffer()) return false;

    return true;
}

static void cleanup_all(void) {
    close_framebuffer();

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
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (!init_all()) {
        cleanup_all();
        return 1;
    }

    hide_console_cursor(true);

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

        if (sleep_for < 0.05) sleep_for = 0.05;

        sleep_seconds(sleep_for);
    }

    cleanup_all();
    return 0;
}
