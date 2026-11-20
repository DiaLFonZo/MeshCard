#ifdef USE_FRAMEBUFFER

#include "graphics/driver/FBDriver.h"
#include "src/core/lv_global.h"
#include "util/ILog.h"
#include <cstdlib>
#include <cstring>

LV_IMG_DECLARE(mouse_cursor_icon);

FBDriver *FBDriver::fbDriver = nullptr;
lv_display_t *FBDriver::display = nullptr;

FBDriver &FBDriver::create(uint16_t width, uint16_t height)
{
    if (!fbDriver)
        fbDriver = new FBDriver(width, height);
    return *fbDriver;
}

FBDriver &FBDriver::create(const DisplayDriverConfig &cfg)
{
    if (!fbDriver)
        fbDriver = new FBDriver(cfg.width(), cfg.height(), toRotation(cfg._panel.offset_rotation));
    return *fbDriver;
}

FBDriver::FBDriver(uint16_t width, uint16_t height, lv_display_rotation_t rotation)
    : DisplayDriver(width, height), rotation(rotation)
{
}

lv_display_rotation_t FBDriver::toRotation(uint8_t offsetRotation)
{
    switch (offsetRotation) {
    case 0:
        return LV_DISPLAY_ROTATION_0;
    case 1:
        return LV_DISPLAY_ROTATION_90;
    case 2:
        return LV_DISPLAY_ROTATION_180;
    case 3:
        return LV_DISPLAY_ROTATION_270;
    default:
        ILOG_WARN("FBDriver: unsupported rotation %d, using 0 degrees", offsetRotation);
        return LV_DISPLAY_ROTATION_0;
    }
}

#ifdef FB_SHRINK
/* U115 / Cardputer Zero: the UI is drawn at its own size (e.g. 320x240) into a shadow
 * buffer, then shrunk onto the smaller panel (/dev/fb0, 320x170 RGB565).
 * MUI_STRETCH=1 fills the whole panel (squashed vertically); default keeps the shape. */
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t *shSrc;       // UI picture, ARGB8888, srcW x srcH
static int srcW, srcH;
static uint8_t *fbMem;        // panel memory
static int fbW, fbH, fbStride, outW, outH, outX, outY;
static int *colFrom, *colTo, *rowFrom, *rowTo;

static void shrinkToPanel(void)
{
    for (int oy = 0; oy < outH; oy++) {
        uint16_t *dst = (uint16_t *)(fbMem + (oy + outY) * fbStride) + outX;
        for (int ox = 0; ox < outW; ox++) {
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int y = rowFrom[oy]; y < rowTo[oy]; y++) {
                const uint32_t *s = shSrc + y * srcW;
                for (int x = colFrom[ox]; x < colTo[ox]; x++) {
                    uint32_t p = s[x];
                    r += (p >> 16) & 0xff;
                    g += (p >> 8) & 0xff;
                    b += p & 0xff;
                    n++;
                }
            }
            r /= n;
            g /= n;
            b /= n;
            dst[ox] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

static void shrinkFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    int w = lv_area_get_width(area);
    uint32_t stride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(disp));
    for (int y = area->y1; y <= area->y2; y++)
        memcpy(shSrc + y * srcW + area->x1, px + (y - area->y1) * stride, w * 4);
    static unsigned flushes;
    flushes++;
    if (lv_display_flush_is_last(disp)) {
        shrinkToPanel();
        const char *dump = getenv("MUI_DUMP"); // debug: full UI picture as raw ARGB8888
        if (dump && *dump) {
            FILE *f = fopen(dump, "wb");
            if (f) {
                fwrite(shSrc, 4, srcW * srcH, f);
                fclose(f);
            }
            ILOG_INFO("FBDriver: frame done after %u flushes, last area %d,%d-%d,%d", flushes, area->x1, area->y1,
                      area->x2, area->y2);
        }
    }
    lv_display_flush_ready(disp);
}

static void spans(int src, int out, int *from, int *to)
{
    for (int i = 0; i < out; i++) {
        from[i] = i * src / out;
        to[i] = (i + 1) * src / out;
        if (to[i] <= from[i])
            to[i] = from[i] + 1;
    }
}

static lv_display_t *createShrinkDisplay(const char *device, int w, int h)
{
    int fd = open(device, O_RDWR);
    if (fd < 0)
        return nullptr;
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) || ioctl(fd, FBIOGET_FSCREENINFO, &fi) || vi.bits_per_pixel != 16) {
        close(fd);
        return nullptr;
    }
    fbW = vi.xres;
    fbH = vi.yres;
    fbStride = fi.line_length;
    fbMem = (uint8_t *)mmap(nullptr, fbStride * fbH, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbMem == MAP_FAILED)
        return nullptr;
    memset(fbMem, 0, fbStride * fbH);

    srcW = w;
    srcH = h;
    const char *st = getenv("MUI_STRETCH");
    if (st && *st == '1') {
        outW = fbW;
        outH = fbH;
    } else if (srcW * fbH > fbW * srcH) { // wider than the panel: fit width
        outW = fbW;
        outH = srcH * fbW / srcW;
    } else {                               // fit height
        outH = fbH;
        outW = srcW * fbH / srcH;
    }
    outX = (fbW - outW) / 2;
    outY = (fbH - outH) / 2;
    colFrom = new int[outW];
    colTo = new int[outW];
    rowFrom = new int[outH];
    rowTo = new int[outH];
    spans(srcW, outW, colFrom, colTo);
    spans(srcH, outH, rowFrom, rowTo);
    shSrc = new uint32_t[srcW * srcH]();
    ILOG_INFO("FBDriver: UI %dx%d shrunk to %dx%d at %d,%d on %dx%d panel", srcW, srcH, outW, outH, outX, outY, fbW, fbH);

    lv_display_t *disp = lv_display_create(srcW, srcH);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888); // XRGB8888 is not enabled in lv_conf.h (draws nothing)
    uint32_t bufSize = srcW * 4 * 60;
    lv_display_set_buffers(disp, malloc(bufSize), nullptr, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, shrinkFlush);
    return disp;
}
#endif

void FBDriver::init(DeviceGUI *gui)
{
    ILOG_DEBUG("FBDriver::init...");
    // Initialize LVGL
    DisplayDriver::init(gui);

#ifdef FB_SHRINK
    {
        const char *dev = getenv("LV_LINUX_FBDEV_DEVICE");
        dev = (dev != nullptr && strlen(dev) > 0) ? dev : "/dev/fb0";
        display = createShrinkDisplay(dev, screenWidth, screenHeight);
        if (display == nullptr)
            ILOG_CRIT("FBDriver: cannot open %s as a 16-bit framebuffer", dev);
        return;
    }
#endif

    // Linux frame buffer device init
    const char *device = getenv("LV_LINUX_FBDEV_DEVICE");
    device = (device != nullptr && strlen(device) > 0) ? device : "/dev/fb0";
    display = lv_linux_fbdev_create();

    if (display == nullptr) {
        ILOG_CRIT("Failed to initialize %d", device);
        return;
    }
    lv_linux_fbdev_set_file(display, device);
    // config driven rotation, e.g. from the YAML (Display.Rotate + Display.OffsetRotate) of meshtasticd
    if (rotation != LV_DISPLAY_ROTATION_0)
        lv_display_set_rotation(display, rotation);

#if LV_USE_EVDEV
    // discover input devices
    lv_evdev_discovery_start(discovery_cb, display);
#endif
}

#if LV_USE_EVDEV
void FBDriver::discovery_cb(lv_indev_t *indev, lv_evdev_type_t type, void *user_data)
{
    ILOG_INFO("'%s' device discovered", type == LV_EVDEV_TYPE_REL   ? "REL"
                                        : type == LV_EVDEV_TYPE_ABS ? "ABS"
                                        : type == LV_EVDEV_TYPE_KEY ? "KEY"
                                                                    : "???");

    lv_display_t *disp = (lv_display_t *)user_data;
    lv_indev_set_display(indev, disp);

    if (type == LV_EVDEV_TYPE_REL) {
        set_mouse_cursor_icon(indev, disp);
    }
}

void FBDriver::set_mouse_cursor_icon(lv_indev_t *indev, lv_display_t *display)
{
    // Set the cursor icon
    LV_IMAGE_DECLARE(mouse_cursor_icon);
    lv_obj_t *cursor_obj = lv_image_create(lv_display_get_screen_active(display));
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(indev, cursor_obj);

    // delete the mouse cursor icon if the device is removed
    lv_indev_add_event_cb(indev, indev_deleted_cb, LV_EVENT_DELETE, cursor_obj);
}

void FBDriver::indev_deleted_cb(lv_event_t *e)
{
    if (LV_GLOBAL_DEFAULT()->deinit_in_progress)
        return;
    lv_obj_t *cursor_obj = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_del(cursor_obj);
}
#endif

#endif