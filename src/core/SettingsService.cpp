#include "SettingsService.h"

#include <SDCardManager.h>

namespace {
constexpr const char* kSettingsPath = "/.settings.bin";
constexpr uint32_t kMagic = 0x54455343;  // "CSET" (little-endian表記)
constexpr uint8_t kVersion = 1;
}  // namespace

bool SettingsService::load(AppSettings& out) {
  FsFile f = SdMan.open(kSettingsPath, O_RDONLY);
  if (!f) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  AppSettings loaded;
  const bool ok = f.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) == sizeof(magic) && magic == kMagic &&
                  f.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) == sizeof(version) &&
                  version == kVersion &&
                  f.read(reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded)) == sizeof(loaded);
  f.close();
  if (!ok) return false;

  // パス系フィールドがnull終端されていない壊れたデータを描画/open()に渡さないための保険。
  loaded.cjkFontPath[sizeof(loaded.cjkFontPath) - 1] = '\0';
  loaded.binFontPath[sizeof(loaded.binFontPath) - 1] = '\0';
  loaded.mdListFontPath[sizeof(loaded.mdListFontPath) - 1] = '\0';
  loaded.mdHeading1FontPath[sizeof(loaded.mdHeading1FontPath) - 1] = '\0';
  loaded.mdHeading2FontPath[sizeof(loaded.mdHeading2FontPath) - 1] = '\0';
  loaded.mdHeading3FontPath[sizeof(loaded.mdHeading3FontPath) - 1] = '\0';
  loaded.mdBoldFontPath[sizeof(loaded.mdBoldFontPath) - 1] = '\0';
  loaded.readerBodyCjkFontPath[sizeof(loaded.readerBodyCjkFontPath) - 1] = '\0';
  loaded.readerBodyBinFontPath[sizeof(loaded.readerBodyBinFontPath) - 1] = '\0';
  out = loaded;
  return true;
}

bool SettingsService::save(const AppSettings& settings) {
  FsFile f = SdMan.open(kSettingsPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;

  bool ok = f.write(reinterpret_cast<const uint8_t*>(&kMagic), sizeof(kMagic)) == sizeof(kMagic);
  ok = ok && f.write(reinterpret_cast<const uint8_t*>(&kVersion), sizeof(kVersion)) == sizeof(kVersion);
  ok = ok && f.write(reinterpret_cast<const uint8_t*>(&settings), sizeof(settings)) == sizeof(settings);
  f.close();
  return ok;
}
