#include "FileBrowserService.h"

#include <SDCardManager.h>
#include <SPI.h>

#include <algorithm>

namespace {
// X3はSDカードスロットの電源をGPIO13で制御している(README.mdのGPIO割り当て参照)。
// SDCardManager(SDK)側はこのピンを一切扱わないため、SDCardManager::begin()を
// 呼ぶ前にここで明示的に電源を入れる。
constexpr int kSdPowerPin = 13;

// EPD/SD共有SPIバスのピン(README.mdのGPIO割り当て参照)。
// EInkDisplay::begin()は内部でSPI.begin(sclk, -1, mosi, cs)を呼んでおり、
// MISOピンを設定しない(EPDは書き込み専用でMISOを使わないため)。そのままだと
// SDカードの読み込み(MISO必須)ができずSD.begin()が検出失敗するため、ここで
// MISOを含めてSPIバスを明示的に再初期化する。
constexpr int kSpiSclk = 8;
constexpr int kSpiMiso = 7;
constexpr int kSpiMosi = 10;
}  // namespace

bool FileBrowserService::begin() {
  // 実機解析情報によれば、このピンは"OUTPUT with pullup"として扱われている
  // (単純なpush-pull OUTPUTではなくオープンドレイン+プルアップの可能性がある)。
  pinMode(kSdPowerPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kSdPowerPin, HIGH);
  delay(200);  // 電源安定待ち(SDカードの起動には数百ms要することがある)

  // SPIClass::begin()は「既にバスが初期化済みならピン設定を変えずtrueを返すだけ」
  // という実装のため、EInkDisplay::begin()が先にMISO抜きで初期化した状態のままだと
  // ここでMISOを指定し直しても反映されない。end()で一度切断してから再初期化する。
  SPI.end();
  SPI.begin(kSpiSclk, kSpiMiso, kSpiMosi, -1);

  return SdMan.begin();
}

bool FileBrowserService::ready() const {
  return SdMan.ready();
}

std::vector<DirEntry> FileBrowserService::listDirectory(const char* path) {
  std::vector<DirEntry> entries;
  if (!SdMan.ready()) return entries;

  FsFile dir = SdMan.open(path, O_RDONLY);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return entries;
  }

  char name[128];
  for (FsFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
    f.getName(name, sizeof(name));
    // "."始まりの隠しファイル・システムファイル(macOSの._*等)は表示しない
    if (name[0] == '.') {
      f.close();
      continue;
    }

    DirEntry entry;
    entry.name = String(name);
    entry.isDirectory = f.isDirectory();
    entry.size = entry.isDirectory ? 0 : static_cast<uint32_t>(f.fileSize());
    entries.push_back(entry);
    f.close();
  }
  dir.close();

  std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
    if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;  // ディレクトリ優先
    return a.name < b.name;
  });

  return entries;
}
