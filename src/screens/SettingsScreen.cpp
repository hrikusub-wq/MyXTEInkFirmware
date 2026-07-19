#include "SettingsScreen.h"

#include <InputManager.h>
#include <SDCardManager.h>

#include <cstring>

#include "../gfx/FrameBufferOps.h"
#include "../gfx/XteinkBinFontImpl.h"

SettingsScreen::SettingsScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, RtcService& rtc,
                               BatteryService& battery, FileBrowserService& fileBrowser, AppSettings& settings)
    : rtc_(rtc),
      battery_(battery),
      fileBrowser_(fileBrowser),
      settings_(settings),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      statusBar_(Rect{0, 0, static_cast<int>(fbWidth), kStatusBarHeight}),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  statusBar_.setLeftText("SETTINGS");

  // UP/DOWN(行フォーカス移動)は側面ボタンのためフッターには表示できない。
  footerItems_[0] = {PhysicalButton::kBack, "HOME"};
  footerItems_[1] = {PhysicalButton::kConfirm, "", IconId::kCheck, true};
  footerItems_[2] = {PhysicalButton::kLeft, "", IconId::kChevronBackward, true};
  footerItems_[3] = {PhysicalButton::kRight, "", IconId::kChevronForward, true};
  footer_.setItems(footerItems_, 4);

  // SDアクセスを伴うフォント一覧の取得はここでは行わない(reloadAvailableFonts()
  // 参照、ヘッダのコメント参照)。行レイアウトと表示値だけは初期化しておく。
  layoutRows(font);
  refreshRowValues();
}

void SettingsScreen::reloadAvailableFonts() {
  refreshFontList();
  scanForCurrentFontSelection();
  refreshRowValues();
}

SettingsScreen::ItemKind SettingsScreen::kindForIndex(int index) {
  switch (index) {
    case 0: return ItemKind::kClock;
    case 1: return ItemKind::kTimezone;
    case 2: return ItemKind::kToggle;
    case 3: return ItemKind::kFontCycle;   // SYSTEM FONT
    case 4: return ItemKind::kFontCycle;   // BOOK FONT
    case 5: return ItemKind::kScaleCycle;
    case 6: return ItemKind::kMarkdownMenu;
    case 7: return ItemKind::kReadOnly;
    case 8: return ItemKind::kAction;
    case 9: return ItemKind::kNavigate;   // BLUETOOTH
    case 10: return ItemKind::kNavigate;  // FOLDER SYNC
    case 11: return ItemKind::kLongPress;
    case 12: return ItemKind::kStandbyGamma;
    case 13: return ItemKind::kNavigate;  // SYSTEM
    default: return ItemKind::kReadOnly;
  }
}

const char* SettingsScreen::labelForIndex(int index) {
  switch (index) {
    case 0: return "TIME";
    case 1: return "TIMEZONE";
    case 2: return "CLOCK IN STATUS BAR";
    case 3: return "SYSTEM FONT";
    case 4: return "BOOK FONT";
    case 5: return "TEXT SIZE";
    case 6: return "MARKDOWN";
    case 7: return "BATTERY";
    case 8: return "CLEAR CACHE";
    case 9: return "BLUETOOTH";
    case 10: return "FOLDER SYNC";
    case 11: return "LONG PRESS";
    case 12: return "PHOTO GAMMA";
    case 13: return "SYSTEM";
    default: return "";
  }
}

const char* SettingsScreen::markdownRoleLabel(MarkdownRole role) {
  switch (role) {
    case MarkdownRole::kHeading1: return "HEADING 1";
    case MarkdownRole::kHeading2: return "HEADING 2";
    case MarkdownRole::kHeading3: return "HEADING 3";
    case MarkdownRole::kList: return "LIST";
    case MarkdownRole::kBold: return "BOLD";
    default: return "";
  }
}

SettingsScreen::FontTarget SettingsScreen::fontTargetForIndex(int index) {
  return (index == 4) ? FontTarget::kReaderBody : FontTarget::kSystem;
}

void SettingsScreen::layoutRows(const Font& font) {
  const int rowH = font.lineHeight() + kRowPadding;
  for (int i = 0; i < kItemCount; i++) {
    rows_[i].setBounds(Rect{0, kStatusBarHeight + i * rowH, static_cast<int>(fbWidth_), rowH});
    rows_[i].setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  }
  // BLUETOOTH/FOLDER SYNCのみ既存のアイコン資産を流用する(他の行は従来通りアイコンなし)。
  rows_[9].setIcon(IconId::kBluetooth);
  rows_[10].setIcon(IconId::kCloud);
}

void SettingsScreen::relayout(const Font& font) {
  layoutRows(font);
  refreshRowValues();
}

void SettingsScreen::refreshFontList() {
  availableCjkFonts_.clear();
  availableBinFonts_.clear();
  for (const DirEntry& e : fileBrowser_.listDirectory("/System/fonts")) {
    if (e.isDirectory) continue;
    String lower = e.name;
    lower.toLowerCase();
    if (lower.endsWith(".cpfont")) {
      availableCjkFonts_.push_back(e.name);
    } else if (lower.endsWith(".bin")) {
      int w = 0, h = 0;
      // ファイル名から幅・高さを解析できない.bin(将来別用途に使われた場合など)は
      // フォントとして開けないため一覧に出さない。
      if (XteinkBinFontImpl::parseDimensions(e.name, w, h)) {
        availableBinFonts_.push_back(BinFontEntry{e.name, w, h});
      }
    }
  }
}

void SettingsScreen::scanForCurrentFontSelection() {
  scanFontSelectionForTarget(FontTarget::kSystem, settings_.systemFontKind, settings_.cjkFontPath,
                             settings_.binFontPath);

  // 読書本文はkCjkFont+パス未設定の場合、main.cppがkDefaultReaderBodyFontPathへ
  // フォールバックして読み込む(AppSettingsのコメント参照)。UI上の選択表示も
  // それに合わせるため、パスが空ならその既定パスで一覧から探す。
  const char* readerCjkPath = (settings_.readerBodyCjkFontPath[0] != '\0') ? settings_.readerBodyCjkFontPath
                                                                            : kDefaultReaderBodyFontPath;
  scanFontSelectionForTarget(FontTarget::kReaderBody, settings_.readerBodyFontKind, readerCjkPath,
                             settings_.readerBodyBinFontPath);

  scanMarkdownRoleSelections();
}

void SettingsScreen::scanFontSelectionForTarget(FontTarget target, SystemFontKind kind, const char* cjkPath,
                                                const char* binPath) {
  int& selection = fontSelectionIndex_[static_cast<int>(target)];
  selection = 0;
  const int cjkCount = static_cast<int>(availableCjkFonts_.size());

  if (kind == SystemFontKind::kCjkFont) {
    const String currentPath = cjkPath;
    for (int i = 0; i < cjkCount; i++) {
      if (String("/System/fonts/") + availableCjkFonts_[i] == currentPath) {
        selection = i + 1;
        return;
      }
    }
  } else if (kind == SystemFontKind::kBinFont) {
    const String currentPath = binPath;
    for (size_t i = 0; i < availableBinFonts_.size(); i++) {
      if (String("/System/fonts/") + availableBinFonts_[i].name == currentPath) {
        selection = 1 + cjkCount + static_cast<int>(i);
        return;
      }
    }
  }
}

void SettingsScreen::scanMarkdownRoleSelections() {
  const char* paths[kMarkdownRoleCount] = {settings_.mdHeading1FontPath, settings_.mdHeading2FontPath,
                                           settings_.mdHeading3FontPath, settings_.mdListFontPath,
                                           settings_.mdBoldFontPath};
  for (int role = 0; role < kMarkdownRoleCount; role++) {
    mdRoleSelectionIndex_[role] = 0;
    if (paths[role][0] == '\0') continue;
    const String currentPath = paths[role];
    for (size_t i = 0; i < availableBinFonts_.size(); i++) {
      if (String("/System/fonts/") + availableBinFonts_[i].name == currentPath) {
        mdRoleSelectionIndex_[role] = static_cast<int>(i) + 1;
        break;
      }
    }
  }
}

String SettingsScreen::fontLabelFor(int selectionIndex) const {
  if (selectionIndex <= 0) return "MINIFONT (ASCII)";
  int i = selectionIndex - 1;
  if (i < static_cast<int>(availableCjkFonts_.size())) return availableCjkFonts_[i];
  i -= static_cast<int>(availableCjkFonts_.size());
  if (i >= 0 && i < static_cast<int>(availableBinFonts_.size())) return availableBinFonts_[i].name;
  return "MINIFONT (ASCII)";
}

String SettingsScreen::markdownRoleFontLabelFor(MarkdownRole role, int selectionIndex) const {
  // HEADING1のみ「未設定=内蔵の既定見出しフォント」なのでDEFAULT、それ以外(HEADING2/3
  // は1つ上のレベルへカスケード、LIST/BOLDは本文フォントで代用)はOFFと表示する。
  const char* fallbackLabel = (role == MarkdownRole::kHeading1) ? "DEFAULT" : "OFF";
  if (selectionIndex <= 0) return fallbackLabel;
  const int i = selectionIndex - 1;
  if (i < 0 || i >= static_cast<int>(availableBinFonts_.size())) return fallbackLabel;
  return availableBinFonts_[i].name;
}

void SettingsScreen::refreshRowValues() {
  for (int i = 0; i < kItemCount; i++) {
    rows_[i].setLabel(labelForIndex(i));

    switch (kindForIndex(i)) {
      case ItemKind::kClock: {
        RtcDateTime dt;
        if (rtc_.ready() && rtc_.readDateTime(dt)) {
          const RtcDateTime local = addHoursToDateTime(dt, settings_.timezoneOffsetHours);
          char buf[24];
          snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u", local.year, local.month, local.day, local.hour,
                   local.minute);
          rowValues_[i] = buf;
        } else {
          rowValues_[i] = "RTC N/A";
        }
        break;
      }
      case ItemKind::kTimezone: {
        char buf[8];
        snprintf(buf, sizeof(buf), "UTC%+d", settings_.timezoneOffsetHours);
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kToggle:
        rowValues_[i] = settings_.showClockInStatusBar ? "ON" : "OFF";
        break;
      case ItemKind::kFontCycle:
        rowValues_[i] = fontLabelFor(fontSelectionIndex_[static_cast<int>(fontTargetForIndex(i))]);
        break;
      case ItemKind::kScaleCycle: {
        char buf[8];
        snprintf(buf, sizeof(buf), "x%u", settings_.miniFontScale);
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kMarkdownMenu: {
        int configured = 0;
        for (int role = 0; role < kMarkdownRoleCount; role++) {
          if (mdRoleSelectionIndex_[role] > 0) configured++;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%d/%d SET", configured, kMarkdownRoleCount);
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kReadOnly: {
        const int percent = battery_.readPercent();
        const int mv = battery_.readMillivolts();
        char buf[24];
        if (percent >= 0) {
          snprintf(buf, sizeof(buf), "%d%% (%dmV)", percent, mv);
        } else {
          snprintf(buf, sizeof(buf), "N/A");
        }
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kAction:
        rowValues_[i] = "CONFIRM";
        break;
      case ItemKind::kLongPress: {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ums", settings_.longPressMs);
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kStandbyGamma: {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", settings_.standbyGammaPercent);
        rowValues_[i] = buf;
        break;
      }
      case ItemKind::kNavigate:
        break;  // BLUETOOTH/FOLDER SYNC/SYSTEMは遷移専用行で値表示なし(rowValues_[i]は前回のまま未使用)
    }

    rows_[i].setValue(rowValues_[i].c_str());
    rows_[i].setSelected(i == focusIndex_);
  }
}

bool SettingsScreen::consumeFontSettingsChanged() {
  const bool v = fontSettingsChanged_;
  fontSettingsChanged_ = false;
  return v;
}

void SettingsScreen::commitFontSelection(FontTarget target) {
  const int cjkCount = static_cast<int>(availableCjkFonts_.size());
  const int selection = fontSelectionIndex_[static_cast<int>(target)];

  char* cjkPathTarget = (target == FontTarget::kSystem) ? settings_.cjkFontPath : settings_.readerBodyCjkFontPath;
  char* binPathTarget = (target == FontTarget::kSystem) ? settings_.binFontPath : settings_.readerBodyBinFontPath;
  // cjkFontPath/binFontPathとreaderBodyCjkFontPath/readerBodyBinFontPathは
  // いずれも同じchar[64]で宣言されているため、sizeofはtargetによらず共通でよい。
  const size_t pathSize = sizeof(settings_.cjkFontPath);

  SystemFontKind kind;
  if (selection == 0) {
    kind = SystemFontKind::kMiniFont;
    cjkPathTarget[0] = '\0';
    binPathTarget[0] = '\0';
  } else if (selection <= cjkCount) {
    kind = SystemFontKind::kCjkFont;
    const String path = "/System/fonts/" + availableCjkFonts_[selection - 1];
    strncpy(cjkPathTarget, path.c_str(), pathSize - 1);
    cjkPathTarget[pathSize - 1] = '\0';
    binPathTarget[0] = '\0';
  } else {
    kind = SystemFontKind::kBinFont;
    const int binIdx = selection - 1 - cjkCount;
    const String path = "/System/fonts/" + availableBinFonts_[binIdx].name;
    strncpy(binPathTarget, path.c_str(), pathSize - 1);
    binPathTarget[pathSize - 1] = '\0';
    cjkPathTarget[0] = '\0';
  }

  if (target == FontTarget::kSystem) {
    settings_.systemFontKind = kind;
  } else {
    settings_.readerBodyFontKind = kind;
  }

  SettingsService::save(settings_);
  fontSettingsChanged_ = true;
  refreshRowValues();
}

void SettingsScreen::commitMarkdownRoleSelection(MarkdownRole role) {
  char* target = nullptr;
  size_t targetSize = 0;
  switch (role) {
    case MarkdownRole::kHeading1:
      target = settings_.mdHeading1FontPath;
      targetSize = sizeof(settings_.mdHeading1FontPath);
      break;
    case MarkdownRole::kHeading2:
      target = settings_.mdHeading2FontPath;
      targetSize = sizeof(settings_.mdHeading2FontPath);
      break;
    case MarkdownRole::kHeading3:
      target = settings_.mdHeading3FontPath;
      targetSize = sizeof(settings_.mdHeading3FontPath);
      break;
    case MarkdownRole::kList:
      target = settings_.mdListFontPath;
      targetSize = sizeof(settings_.mdListFontPath);
      break;
    case MarkdownRole::kBold:
      target = settings_.mdBoldFontPath;
      targetSize = sizeof(settings_.mdBoldFontPath);
      break;
  }

  const int idx = mdRoleSelectionIndex_[static_cast<int>(role)];
  if (idx <= 0 || idx > static_cast<int>(availableBinFonts_.size())) {
    target[0] = '\0';
  } else {
    const String path = "/System/fonts/" + availableBinFonts_[idx - 1].name;
    strncpy(target, path.c_str(), targetSize - 1);
    target[targetSize - 1] = '\0';
  }

  SettingsService::save(settings_);
  fontSettingsChanged_ = true;
  refreshRowValues();
}

void SettingsScreen::applyScaleDelta(int delta) {
  // CONFIRMを押すたびに呼ばれる単一方向の循環操作なので、上限で止まらずラップする
  // (1→2→3→4→1→…)。
  const int scale = ((static_cast<int>(settings_.miniFontScale) - 1 + delta) % 4 + 4) % 4 + 1;
  settings_.miniFontScale = static_cast<uint8_t>(scale);

  SettingsService::save(settings_);
  fontSettingsChanged_ = true;
  refreshRowValues();
}

void SettingsScreen::enterClockEdit() {
  RtcDateTime dt;
  if (rtc_.ready() && rtc_.readDateTime(dt)) {
    // 編集・表示するのは常にタイムゾーンオフセット適用後のローカル時刻。
    clockDraft_ = addHoursToDateTime(dt, settings_.timezoneOffsetHours);
  } else {
    clockDraft_ = RtcDateTime{};
  }
  clockFieldIndex_ = 0;
  editingClock_ = true;
}

void SettingsScreen::adjustClockField(int delta) {
  switch (clockFieldIndex_) {
    case 0: {  // year: 2000-2099の範囲でクランプ(ラップしない)
      int y = static_cast<int>(clockDraft_.year) + delta;
      if (y < 2000) y = 2000;
      if (y > 2099) y = 2099;
      clockDraft_.year = static_cast<uint16_t>(y);
      break;
    }
    case 1: {  // month: 1-12でラップ
      const int m = ((static_cast<int>(clockDraft_.month) - 1 + delta) % 12 + 12) % 12 + 1;
      clockDraft_.month = static_cast<uint8_t>(m);
      break;
    }
    case 2: {  // day: 1-31でラップ(月ごとの日数上限は簡略化のため厳密にチェックしない)
      const int d = ((static_cast<int>(clockDraft_.day) - 1 + delta) % 31 + 31) % 31 + 1;
      clockDraft_.day = static_cast<uint8_t>(d);
      break;
    }
    case 3: {  // hour: 0-23でラップ
      const int h = ((static_cast<int>(clockDraft_.hour) + delta) % 24 + 24) % 24;
      clockDraft_.hour = static_cast<uint8_t>(h);
      break;
    }
    case 4: {  // minute: 0-59でラップ
      const int mi = ((static_cast<int>(clockDraft_.minute) + delta) % 60 + 60) % 60;
      clockDraft_.minute = static_cast<uint8_t>(mi);
      break;
    }
    default: break;
  }
}

void SettingsScreen::commitClockEdit() {
  clockDraft_.second = 0;
  // 表示・編集はローカル時刻なので、RTCの生値に書き戻す前にオフセットを差し引く。
  const RtcDateTime rawDt = addHoursToDateTime(clockDraft_, -settings_.timezoneOffsetHours);
  rtc_.writeDateTime(rawDt);
}

void SettingsScreen::enterFontPicker(FontTarget target) {
  fontPickerTarget_ = target;
  fontPickerFocusIndex_ = fontSelectionIndex_[static_cast<int>(target)];
  editingFont_ = true;
}

void SettingsScreen::enterMarkdownMenu() {
  markdownMenuFocusIndex_ = 0;
  editingMarkdownMenu_ = true;
}

void SettingsScreen::enterMarkdownFontPicker(MarkdownRole role) {
  markdownFontPickerRole_ = role;
  markdownFontPickerFocusIndex_ = mdRoleSelectionIndex_[static_cast<int>(role)];
  editingMarkdownFontPicker_ = true;
}

void SettingsScreen::enterTimezoneEdit() {
  timezoneDraft_ = settings_.timezoneOffsetHours;
  editingTimezone_ = true;
}

void SettingsScreen::adjustTimezoneDraft(int delta) {
  int v = static_cast<int>(timezoneDraft_) + delta;
  if (v < -9) v = -9;
  if (v > 9) v = 9;
  timezoneDraft_ = static_cast<int8_t>(v);
}

void SettingsScreen::commitTimezoneEdit() {
  settings_.timezoneOffsetHours = timezoneDraft_;
  SettingsService::save(settings_);
}

void SettingsScreen::enterLongPressEdit() {
  longPressDraft_ = settings_.longPressMs;
  editingLongPress_ = true;
}

void SettingsScreen::adjustLongPressDraft(int delta) {
  int v = static_cast<int>(longPressDraft_) + delta * 100;
  if (v < 200) v = 200;
  if (v > 1500) v = 1500;
  longPressDraft_ = static_cast<uint16_t>(v);
}

void SettingsScreen::commitLongPressEdit() {
  settings_.longPressMs = longPressDraft_;
  SettingsService::save(settings_);
}

void SettingsScreen::enterStandbyGammaEdit() {
  standbyGammaDraft_ = settings_.standbyGammaPercent;
  editingStandbyGamma_ = true;
}

void SettingsScreen::adjustStandbyGammaDraft(int delta) {
  int v = static_cast<int>(standbyGammaDraft_) + delta * 5;
  if (v < 20) v = 20;
  if (v > 100) v = 100;
  standbyGammaDraft_ = static_cast<uint8_t>(v);
}

void SettingsScreen::commitStandbyGammaEdit() {
  settings_.standbyGammaPercent = standbyGammaDraft_;
  SettingsService::save(settings_);
}

void SettingsScreen::clearReaderCache() {
  for (const DirEntry& e : fileBrowser_.listDirectory("/.reader_cache")) {
    if (e.isDirectory) continue;
    const String path = "/.reader_cache/" + e.name;
    SdMan.remove(path.c_str());
  }
}

void SettingsScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  statusBar_.render(fb, fbWidth, fbHeight, font);

  if (editingClock_) {
    drawClockEdit(fb, fbWidth, fbHeight, font);
  } else if (editingFont_) {
    drawFontPicker(fb, fbWidth, fbHeight, font);
  } else if (editingMarkdownFontPicker_) {
    drawMarkdownFontPicker(fb, fbWidth, fbHeight, font);
  } else if (editingMarkdownMenu_) {
    drawMarkdownMenu(fb, fbWidth, fbHeight, font);
  } else if (editingTimezone_) {
    drawTimezoneEdit(fb, fbWidth, fbHeight, font);
  } else if (editingLongPress_) {
    drawLongPressEdit(fb, fbWidth, fbHeight, font);
  } else if (editingStandbyGamma_) {
    drawStandbyGammaEdit(fb, fbWidth, fbHeight, font);
  } else {
    // 電池残量・時刻など毎回変わりうる値を描画のたびに最新化する
    // (main.cppからの明示的なpushに頼らず、参照を持っているサービスから直接読む)。
    refreshRowValues();
    for (int i = 0; i < kItemCount; i++) rows_[i].render(fb, fbWidth, fbHeight, font);
  }

  footer_.render(fb, fbWidth, fbHeight, font);

  if (showClearCacheConfirm_) {
    drawClearCacheOverlay(fb, fbWidth, fbHeight, font);
  }
}

void SettingsScreen::drawClockEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 24;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, "SET TIME");

  char fieldBufs[5][8];
  snprintf(fieldBufs[0], sizeof(fieldBufs[0]), "%04u", clockDraft_.year);
  snprintf(fieldBufs[1], sizeof(fieldBufs[1]), "%02u", clockDraft_.month);
  snprintf(fieldBufs[2], sizeof(fieldBufs[2]), "%02u", clockDraft_.day);
  snprintf(fieldBufs[3], sizeof(fieldBufs[3]), "%02u", clockDraft_.hour);
  snprintf(fieldBufs[4], sizeof(fieldBufs[4]), "%02u", clockDraft_.minute);
  static const char* kSeparators[5] = {"", "-", "-", "  ", ":"};

  const int fieldY = titleY + lineH + 16;
  int x = 16;
  for (int i = 0; i < 5; i++) {
    if (kSeparators[i][0] != '\0') {
      font.drawText(fb, fbWidth, fbHeight, x, fieldY, kSeparators[i]);
      x += font.measureText(kSeparators[i]);
    }

    const int w = font.measureText(fieldBufs[i]);
    font.drawText(fb, fbWidth, fbHeight, x, fieldY, fieldBufs[i]);
    if (i == clockFieldIndex_) {
      // SettingRowと同じ手法: 白背景に黒文字を描いてから矩形ごと反転させる。
      FrameBufferOps::invertRect(fb, fbWidth, fbHeight, x - 2, fieldY - 2, w + 4, lineH + 4);
    }
    x += w;
  }

  font.drawText(fb, fbWidth, fbHeight, 16, fieldY + lineH + 24, "LEFT/RIGHT=FIELD  UP/DOWN=CHANGE");
  font.drawText(fb, fbWidth, fbHeight, 16, fieldY + lineH * 2 + 24, "CONFIRM=SAVE  BACK=CANCEL");
}

void SettingsScreen::drawFontPicker(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 16;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY,
                (fontPickerTarget_ == FontTarget::kSystem) ? "SELECT SYSTEM FONT" : "SELECT BOOK FONT");

  const int rowH = lineH + kRowPadding;
  const int listTop = titleY + lineH + 12;
  const int total = 1 + static_cast<int>(availableCjkFonts_.size()) + static_cast<int>(availableBinFonts_.size());

  for (int i = 0; i < total; i++) {
    // fontLabelFor()が返す一時Stringはrender()呼び出しの間だけ生存させればよいので、
    // ループの各反復内でローカル変数として保持する(SettingRowはconst char*を
    // コピーせずポインタで保持するため、寿命がrender()呼び出しをまたいではいけない)。
    const String label = fontLabelFor(i);
    SettingRow row(Rect{0, listTop + i * rowH, static_cast<int>(fbWidth), rowH}, label.c_str(), "");
    row.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
    row.setSelected(i == fontPickerFocusIndex_);
    row.render(fb, fbWidth, fbHeight, font);
  }
}

void SettingsScreen::drawMarkdownMenu(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 16;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, "MARKDOWN FONTS");

  const int rowH = lineH + kRowPadding;
  const int listTop = titleY + lineH + 12;

  for (int i = 0; i < kMarkdownRoleCount; i++) {
    const auto role = static_cast<MarkdownRole>(i);
    const String label = markdownRoleFontLabelFor(role, mdRoleSelectionIndex_[i]);
    SettingRow row(Rect{0, listTop + i * rowH, static_cast<int>(fbWidth), rowH}, markdownRoleLabel(role),
                  label.c_str());
    row.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
    row.setSelected(i == markdownMenuFocusIndex_);
    row.render(fb, fbWidth, fbHeight, font);
  }
}

void SettingsScreen::drawMarkdownFontPicker(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight,
                                            const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 16;
  char titleBuf[32];
  snprintf(titleBuf, sizeof(titleBuf), "SELECT %s FONT", markdownRoleLabel(markdownFontPickerRole_));
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, titleBuf);

  const int rowH = lineH + kRowPadding;
  const int listTop = titleY + lineH + 12;
  const int total = 1 + static_cast<int>(availableBinFonts_.size());

  for (int i = 0; i < total; i++) {
    const String label = markdownRoleFontLabelFor(markdownFontPickerRole_, i);
    SettingRow row(Rect{0, listTop + i * rowH, static_cast<int>(fbWidth), rowH}, label.c_str(), "");
    row.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
    row.setSelected(i == markdownFontPickerFocusIndex_);
    row.render(fb, fbWidth, fbHeight, font);
  }
}

void SettingsScreen::drawTimezoneEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 24;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, "SET TIMEZONE");

  char buf[8];
  snprintf(buf, sizeof(buf), "UTC%+d", timezoneDraft_);
  const int valueY = titleY + lineH + 16;
  font.drawText(fb, fbWidth, fbHeight, 16, valueY, buf);

  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH + 24, "LEFT/RIGHT=CHANGE (-9..+9)");
  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH * 2 + 24, "CONFIRM=SAVE  BACK=CANCEL");
}

void SettingsScreen::drawLongPressEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 24;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, "SET LONG PRESS TIME");

  char buf[8];
  snprintf(buf, sizeof(buf), "%ums", longPressDraft_);
  const int valueY = titleY + lineH + 16;
  font.drawText(fb, fbWidth, fbHeight, 16, valueY, buf);

  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH + 24, "LEFT/RIGHT=CHANGE (200..1500)");
  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH * 2 + 24, "CONFIRM=SAVE  BACK=CANCEL");
}

void SettingsScreen::drawStandbyGammaEdit(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int lineH = font.lineHeight();
  const int titleY = kStatusBarHeight + 24;
  font.drawText(fb, fbWidth, fbHeight, 16, titleY, "SET PHOTO GAMMA");

  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", standbyGammaDraft_);
  const int valueY = titleY + lineH + 16;
  font.drawText(fb, fbWidth, fbHeight, 16, valueY, buf);

  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH + 24, "LEFT/RIGHT=CHANGE (20..100)");
  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH * 2 + 24, "SMALLER=BRIGHTER");
  font.drawText(fb, fbWidth, fbHeight, 16, valueY + lineH * 3 + 24, "CONFIRM=SAVE  BACK=CANCEL");
}

void SettingsScreen::drawClearCacheOverlay(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  const int boxW = static_cast<int>(fbWidth) - 64;
  const int lineH = font.lineHeight();
  const int boxH = lineH * 4 + 40;
  const int boxX = (static_cast<int>(fbWidth) - boxW) / 2;
  const int boxY = (static_cast<int>(fbHeight) - boxH) / 2;

  FrameBufferOps::fillRect(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, false);
  FrameBufferOps::drawRectOutline(fb, fbWidth, fbHeight, boxX, boxY, boxW, boxH, 2);

  const int textY = boxY + 16;
  font.drawText(fb, fbWidth, fbHeight, boxX + 16, textY, "CLEAR READING CACHE?");

  SettingRow confirmRow(Rect{boxX + 16, textY + lineH + 12, boxW - 32, lineH + 10}, "CLEAR", "");
  confirmRow.setSelectionStyle(SettingRow::SelectionStyle::kInvert);
  confirmRow.setSelected(true);
  confirmRow.render(fb, fbWidth, fbHeight, font);

  font.drawText(fb, fbWidth, fbHeight, boxX + 16, boxY + boxH - lineH - 12, "CONFIRM=OK  BACK=CANCEL");
}

ScreenAction SettingsScreen::handleButton(uint8_t buttonIndex) {
  if (showClearCacheConfirm_) {
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      showClearCacheConfirm_ = false;
      clearReaderCache();
      refreshRowValues();
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      showClearCacheConfirm_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingClock_) {
    if (buttonIndex == InputManager::BTN_LEFT) {
      clockFieldIndex_ = (clockFieldIndex_ + 4) % 5;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT) {
      clockFieldIndex_ = (clockFieldIndex_ + 1) % 5;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_UP || buttonIndex == InputManager::BTN_DOWN) {
      adjustClockField(buttonIndex == InputManager::BTN_UP ? 1 : -1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      commitClockEdit();
      editingClock_ = false;
      refreshRowValues();
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingClock_ = false;  // 破棄(RTCへは書き込まない)
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingFont_) {
    const int total = 1 + static_cast<int>(availableCjkFonts_.size()) + static_cast<int>(availableBinFonts_.size());
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
      fontPickerFocusIndex_ = (fontPickerFocusIndex_ + total - 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
      fontPickerFocusIndex_ = (fontPickerFocusIndex_ + 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      editingFont_ = false;
      fontSelectionIndex_[static_cast<int>(fontPickerTarget_)] = fontPickerFocusIndex_;
      commitFontSelection(fontPickerTarget_);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingFont_ = false;  // 破棄(選択を変更しない)
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingMarkdownFontPicker_) {
    const int total = 1 + static_cast<int>(availableBinFonts_.size());
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
      markdownFontPickerFocusIndex_ = (markdownFontPickerFocusIndex_ + total - 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
      markdownFontPickerFocusIndex_ = (markdownFontPickerFocusIndex_ + 1) % total;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      editingMarkdownFontPicker_ = false;
      mdRoleSelectionIndex_[static_cast<int>(markdownFontPickerRole_)] = markdownFontPickerFocusIndex_;
      commitMarkdownRoleSelection(markdownFontPickerRole_);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingMarkdownFontPicker_ = false;  // 破棄(選択を変更しない)
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingMarkdownMenu_) {
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
      markdownMenuFocusIndex_ = (markdownMenuFocusIndex_ + kMarkdownRoleCount - 1) % kMarkdownRoleCount;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
      markdownMenuFocusIndex_ = (markdownMenuFocusIndex_ + 1) % kMarkdownRoleCount;
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      enterMarkdownFontPicker(static_cast<MarkdownRole>(markdownMenuFocusIndex_));
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingMarkdownMenu_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingTimezone_) {
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_DOWN) {
      adjustTimezoneDraft(-1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_UP) {
      adjustTimezoneDraft(1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      commitTimezoneEdit();
      editingTimezone_ = false;
      refreshRowValues();
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingTimezone_ = false;  // 破棄
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingLongPress_) {
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_DOWN) {
      adjustLongPressDraft(-1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_UP) {
      adjustLongPressDraft(1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      commitLongPressEdit();
      editingLongPress_ = false;
      refreshRowValues();
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingLongPress_ = false;  // 破棄
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  if (editingStandbyGamma_) {
    if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_DOWN) {
      adjustStandbyGammaDraft(-1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_UP) {
      adjustStandbyGammaDraft(1);
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      commitStandbyGammaEdit();
      editingStandbyGamma_ = false;
      refreshRowValues();
      return ScreenAction::kRedraw;
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      editingStandbyGamma_ = false;  // 破棄
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  // リスト内のフォーカス移動はLEFT/RIGHT・UP/DOWNのどちらでも同じ意味にする
  // (どちらの軸で操作しても迷わないように。値の変更・決定は必ずCONFIRM経由にする)。
  if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
    focusIndex_ = (focusIndex_ + kItemCount - 1) % kItemCount;
    refreshRowValues();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
    focusIndex_ = (focusIndex_ + 1) % kItemCount;
    refreshRowValues();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    switch (kindForIndex(focusIndex_)) {
      case ItemKind::kClock:
        enterClockEdit();
        return ScreenAction::kRedraw;
      case ItemKind::kTimezone:
        enterTimezoneEdit();
        return ScreenAction::kRedraw;
      case ItemKind::kToggle:
        settings_.showClockInStatusBar = !settings_.showClockInStatusBar;
        SettingsService::save(settings_);
        refreshRowValues();
        return ScreenAction::kRedraw;
      case ItemKind::kFontCycle:
        enterFontPicker(fontTargetForIndex(focusIndex_));
        return ScreenAction::kRedraw;
      case ItemKind::kMarkdownMenu:
        enterMarkdownMenu();
        return ScreenAction::kRedraw;
      case ItemKind::kScaleCycle:
        // 専用ウィンドウを設けるほどでもないため、押すたびに1→2→3→4→1…と循環させる。
        applyScaleDelta(1);
        return ScreenAction::kRedraw;
      case ItemKind::kAction:
        showClearCacheConfirm_ = true;
        return ScreenAction::kRedraw;
      case ItemKind::kNavigate:
        return ScreenAction::kNavigateForward;
      case ItemKind::kLongPress:
        enterLongPressEdit();
        return ScreenAction::kRedraw;
      case ItemKind::kStandbyGamma:
        enterStandbyGammaEdit();
        return ScreenAction::kRedraw;
      default:
        return ScreenAction::kNone;
    }
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
