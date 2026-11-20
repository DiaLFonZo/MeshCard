/* SPDX-License-Identifier: GPL-3.0-only */
/* LVGL configuration for MeshCard on the Cardputer ADV (ESP32-S3, 240x135 ST7789).
 * Based on meshcard/src/ui/config/lv_conf.h; no Linux display drivers, smaller caches. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 33
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE

#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (4 * 1024)

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
/* Default is an endless loop: the screen would freeze 90 s before the watchdog restarts.
 * Restart at once instead (the restart log then says CRASH). */
#if !defined(__ASSEMBLY__) && !defined(__ASSEMBLER__)
#include <stdlib.h>
#endif
#define LV_ASSERT_HANDLER abort();

#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_12
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* No TrueType at run time: fonts are pre-drawn into flash (meshcard/src/ui/fonts/adv). */
#define LV_USE_TINY_TTF 0

#define LV_USE_QRCODE 1
#define LV_USE_CANVAS 1

#define LV_USE_LINUX_FBDEV 0
#define LV_USE_SDL 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0

#endif /* LV_CONF_H */
