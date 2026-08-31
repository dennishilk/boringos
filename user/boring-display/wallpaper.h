#ifndef BORING_DISPLAY_WALLPAPER_H
#define BORING_DISPLAY_WALLPAPER_H
#include "core.h"

#define BORING_WALLPAPER_WIDTH 800U
#define BORING_WALLPAPER_HEIGHT 600U

bool display_wallpaper_compose(const struct boring_display_core *core,
                               uint8_t *output, size_t size);

#endif
