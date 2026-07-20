#pragma once
#include <cstdint>

constexpr int kWallpaperSize = 528 * 792 / 8; // EInkDisplay::LOGICAL_WIDTH * LOGICAL_HEIGHT / 8
extern bool g_wallpaperValid;

void initWallpaper();

void saveWallpaper(const uint8_t* fb);
void loadWallpaper(uint8_t* fb);
