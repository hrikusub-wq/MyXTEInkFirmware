#include "Wallpaper.h"
#include <SDCardManager.h>

bool g_wallpaperValid = false;
static const char* kWallpaperPath = "/System/wallpaper.bin";

void initWallpaper() {
  g_wallpaperValid = SdMan.exists(kWallpaperPath);
}

void saveWallpaper(const uint8_t* fb) {
  FsFile f = SdMan.open(kWallpaperPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (f) {
    f.write(fb, kWallpaperSize);
    f.close();
  }
}

void loadWallpaper(uint8_t* fb) {
  FsFile f = SdMan.open(kWallpaperPath, O_RDONLY);
  if (f) {
    f.read(fb, kWallpaperSize);
    f.close();
  }
}
