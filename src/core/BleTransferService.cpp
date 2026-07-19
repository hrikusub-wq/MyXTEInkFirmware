#include "BleTransferService.h"

#include <cstring>

namespace {
// PCアプリ側と完全一致させる必要があるGATT UUID群(README/フェーズ6.5参照)。
constexpr const char* kServiceUuid = "c8ce940b-aed3-47fd-8106-24624957a2fb";
constexpr const char* kControlCharUuid = "7463ab64-254f-40af-adde-e7d7b67a576a";
constexpr const char* kDataCharUuid = "3a929860-03a7-44d7-a21e-d82e224c903c";
constexpr const char* kStatusCharUuid = "dcb89437-64be-4102-b8e1-d55214154573";
// ユーザーファイルのルート。"S:"(単発ファイル送信)はここへファイル名で直接
// 書き込み、"F:"/"X:"(ローカルフォルダ同期)もこの配下を相対パスで読み書きする。
// 以前は"/received"(S:用)と"/sync"(F:/X:用)を別ルートにしていたが、
// どちらも「ユーザーが日常的に見るファイル」という点で意味が同じであり、
// SDカードをSystem(機材データ)/User(ユーザーファイル)に分割する再編に伴い
// 統合した。
constexpr const char* kUserRoot = "/User";

// dirPath配下の各階層を1つずつ確認・作成する。SdMan.mkdir(path, pFlag=true)は
// 中間ディレクトリを再帰作成する機能を持つが、SdFatの実装上「既に存在する
// ディレクトリ」に対して呼ぶとfalseを返す(pFlag=trueでも既存チェックはしない)。
// SDCardManager::ensureDirectoryExists()は単一階層のみだが「既存なら成功扱い」
// にする実装のため、パスを'/'区切りで分割してこれを繰り返し呼ぶことで、
// 既存の中間ディレクトリがあっても失敗しない再帰作成を実現する。
bool ensureDirPathExists(const String& dirPath) {
  int pos = 1;  // 先頭の'/'をスキップ
  while (true) {
    const int slash = dirPath.indexOf('/', pos);
    const String sub = (slash < 0) ? dirPath : dirPath.substring(0, slash);
    if (!SdMan.ensureDirectoryExists(sub.c_str())) return false;
    if (slash < 0) break;
    pos = slash + 1;
  }
  return true;
}
}  // namespace

bool BleTransferService::begin() {
  // デバイス名の計算だけを行い、NimBLEDevice::init()(BTコントローラ+ホストスタック
  // 一式の初期化)はここでは呼ばない。NimBLEの初期化はESP32-C3のヒープを恒常的に
  // 数十KB規模で消費する(一度確保すると本来的なdeinitを行わない限り解放されない)ため、
  // setup()で常に呼んでしまうとBluetooth/FolderSync/LiveTextを一度も開かないセッションでも
  // TXT/Markdown読書用のヒープ予算を常に圧迫してしまう(ヒープ逼迫でファイルが開けない
  // 不具合の主因の1つだった)。実際のスタック初期化はensureStackReady()に切り出し、
  // 最初にstartAdvertising()が呼ばれた時点(=ユーザーが実際にBluetooth系の画面を
  // 開いた時点)まで遅延させる。
  char nameBuf[24];
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(nameBuf, sizeof(nameBuf), "XteinkX3-%04X", static_cast<unsigned>(chipId & 0xFFFFu));
  deviceName_ = nameBuf;
  return true;
}

void BleTransferService::ensureStackReady() {
  if (server_) return;  // 2回目以降の呼び出しは何もしない(初回のみ初期化)

  NimBLEDevice::init(deviceName_.c_str());
  NimBLEDevice::setMTU(517);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(&serverCallbacks_);

  NimBLEService* service = server_->createService(kServiceUuid);

  controlChar_ = service->createCharacteristic(
      kControlCharUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, kControlBufSize);
  controlChar_->setCallbacks(&charCallbacks_);

  dataChar_ = service->createCharacteristic(kDataCharUuid, NIMBLE_PROPERTY::WRITE_NR, kDataBufSize);
  dataChar_->setCallbacks(&charCallbacks_);

  statusChar_ = service->createCharacteristic(kStatusCharUuid, NIMBLE_PROPERTY::NOTIFY);
  statusChar_->setCallbacks(&charCallbacks_);

  // 128bit UUID+デバイス名を1つのアドバタイズパケットに収めると31バイト制限を
  // 超えるため、名前はスキャンレスポンス側に載せる。
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  // NimBLEAdvertising::setName()は「呼ばれた時点で」scanResponseが有効かどうかを見て
  // 名前の格納先(メイン広告パケット or スキャンレスポンス)を決める(NimBLEAdvertising.cpp
  // 参照)。enableScanResponse()を先に呼ばないと、128bit UUID(18バイト)で埋まった
  // メイン広告パケット側に名前を追加しようとして31バイト上限を超え、setName()自体が
  // 失敗して名前が一切広告されない(PCアプリ側のスキャンで発見できなくなる)。
  advertising->enableScanResponse(true);
  advertising->setName(deviceName_.c_str());

  if (Serial) Serial.printf("[BLE] stack initialized on first use (free heap=%u)\n", ESP.getFreeHeap());
}

void BleTransferService::startAdvertising() {
  ensureStackReady();
  wantAdvertising_ = true;
  NimBLEDevice::getAdvertising()->start();
}

void BleTransferService::stopAdvertising() {
  wantAdvertising_ = false;
  NimBLEDevice::getAdvertising()->stop();
}

void BleTransferService::ServerCallbacks::onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/) {
  owner_.connected_ = true;
  if (Serial) Serial.println("[BLE] connected");
}

void BleTransferService::ServerCallbacks::onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*connInfo*/,
                                                        int reason) {
  owner_.connected_ = false;
  owner_.resetTransferState();
  if (Serial) Serial.printf("[BLE] disconnected (reason=%d)\n", reason);
  // NimBLEは切断後にアドバタイズを自動再開しないため、画面がまだ開いている
  // (wantAdvertising_==true)なら明示的に再開し、再接続を受け付けられるようにする。
  if (owner_.wantAdvertising_) {
    NimBLEDevice::getAdvertising()->start();
  }
}

void BleTransferService::CharCallbacks::onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& /*connInfo*/) {
  const NimBLEAttValue value = characteristic->getValue();
  if (characteristic == owner_.controlChar_) {
    owner_.enqueueControlCommand(value.data(), value.length());
  } else if (characteristic == owner_.dataChar_) {
    owner_.enqueueDataChunk(value.data(), value.length());
  }
}

void BleTransferService::CharCallbacks::onSubscribe(NimBLECharacteristic* characteristic,
                                                     NimBLEConnInfo& /*connInfo*/, uint16_t subValue) {
  // Statusの購読(Notify有効化)が完了した時点で初めてREADYを送る。接続直後に
  // 送ると、クライアントがまだCCCDを書き込んでおらず取りこぼす可能性があるため。
  if (characteristic == owner_.statusChar_ && subValue != 0) {
    owner_.sendStatus("READY");
  }
}

void BleTransferService::enqueueControlCommand(const uint8_t* data, size_t len) {
  if (len >= kControlBufSize) len = kControlBufSize - 1;
  portENTER_CRITICAL(&mux_);
  memcpy(pendingControlBuf_, data, len);
  pendingControlBuf_[len] = '\0';
  pendingControlFlag_ = true;
  portEXIT_CRITICAL(&mux_);
}

void BleTransferService::enqueueDataChunk(const uint8_t* data, size_t len) {
  if (len > kDataBufSize) len = kDataBufSize;
  portENTER_CRITICAL(&mux_);
  // 直前のチャンクが未処理のまま次が来るのはPC側のフロー制御違反("OK:"受信を
  // 待たずに送信した場合)。ここで足踏みするとBLEスタックのタスクをブロック
  // してしまうため、古いチャンクは諦めて新しいものに置き換える。
  memcpy(pendingChunkBuf_, data, len);
  pendingChunkLen_ = len;
  pendingChunkFlag_ = true;
  portEXIT_CRITICAL(&mux_);
}

void BleTransferService::update() {
  char controlCmd[kControlBufSize];
  bool hasControl = false;
  portENTER_CRITICAL(&mux_);
  if (pendingControlFlag_) {
    memcpy(controlCmd, pendingControlBuf_, kControlBufSize);
    hasControl = true;
    pendingControlFlag_ = false;
  }
  portEXIT_CRITICAL(&mux_);
  if (hasControl) processControlCommand(controlCmd);

  static uint8_t chunk[kDataBufSize];
  size_t chunkLen = 0;
  bool hasChunk = false;
  portENTER_CRITICAL(&mux_);
  if (pendingChunkFlag_) {
    chunkLen = pendingChunkLen_;
    memcpy(chunk, pendingChunkBuf_, chunkLen);
    hasChunk = true;
    pendingChunkFlag_ = false;
  }
  portEXIT_CRITICAL(&mux_);
  if (hasChunk) processDataChunk(chunk, chunkLen);
}

void BleTransferService::processControlCommand(const char* cmd) {
  const size_t len = strlen(cmd);
  if (Serial) Serial.printf("[BLE] recv control: \"%s\" (state=%d)\n", cmd, static_cast<int>(state_));

  if (len >= 2 && cmd[0] == 'S' && cmd[1] == ':') {
    // 真に転送中(kReceiving)の場合のみ拒否する。kErrorは「前回の転送が
    // エラーで終わった」状態であり、新しい転送開始コマンドが来た時点でその
    // 役目は終わっているとみなし、暗黙的にクリアして受理する(そうしないと、
    // 一度何かの理由でエラーになった後、二度と転送できなくなってしまう。
    // clearError()はBluetoothScreen/FolderSyncScreen遷移時にしか呼ばれず、
    // ライブテキストやフォルダ同期はデバイス側の画面遷移を経由しないため)。
    if (state_ == State::kReceiving) {
      if (Serial) Serial.println("[BLE] S: rejected, already receiving another file");
      failTransfer(ErrorCode::kState, false);
      return;
    }

    // "S:<filename>:<filesize>" — ファイル名自体に':'を含む可能性は考慮せず、
    // 末尾の':'をサイズとの区切りとみなす(PCアプリ側の実装と合わせた仕様)。
    const String rest = String(cmd + 2);
    const int lastColon = rest.lastIndexOf(':');
    if (lastColon <= 0 || lastColon == static_cast<int>(rest.length()) - 1) {
      if (Serial) Serial.println("[BLE] S: parse error (missing size separator)");
      sendError(ErrorCode::kSize);
      return;
    }
    const String name = rest.substring(0, lastColon);
    const long size = rest.substring(lastColon + 1).toInt();
    if (name.length() == 0 || size <= 0) {
      if (Serial) Serial.printf("[BLE] S: invalid name/size (name=\"%s\" size=%ld)\n", name.c_str(), size);
      sendError(ErrorCode::kSize);
      return;
    }

    if (!SdMan.ready()) {
      if (Serial) Serial.println("[BLE] S: SD not ready");
      sendError(ErrorCode::kIo);
      return;
    }
    if (!SdMan.ensureDirectoryExists(kUserRoot)) {
      if (Serial) Serial.printf("[BLE] S: failed to ensure dir %s\n", kUserRoot);
      sendError(ErrorCode::kIo);
      return;
    }
    const String path = String(kUserRoot) + "/" + name;
    // 念のため既存のハンドルを確実に閉じてから開き直す(過去のエラーパスで
    // currentFile_が正しくクローズされずに残っていた場合、SdFatの同時オープン
    // 上限に達して以降のopen()がすべて失敗する不具合を防ぐための防御コード)。
    if (currentFile_) currentFile_.close();
    currentFile_ = SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!currentFile_) {
      if (Serial) {
        Serial.printf("[BLE] S: failed to open \"%s\" for write (exists=%d)\n", path.c_str(),
                      SdMan.exists(path.c_str()) ? 1 : 0);
      }
      sendError(ErrorCode::kIo);
      return;
    }

    currentKind_ = TransferKind::kReceived;
    fileName_ = name;
    currentFilePath_ = path;
    fileSize_ = static_cast<uint32_t>(size);
    receivedBytes_ = 0;
    state_ = State::kReceiving;
    if (Serial) Serial.printf("[BLE] S: start receiving \"%s\" (%ld bytes)\n", path.c_str(), size);
    return;
  }

  if (len >= 2 && cmd[0] == 'F' && cmd[1] == ':') {
    // S:と同じ理由でkErrorは拒否しない(上記コメント参照)。
    if (state_ == State::kReceiving) {
      if (Serial) Serial.println("[BLE] F: rejected, already receiving another file");
      failTransfer(ErrorCode::kState, false);
      return;
    }

    // "F:<relpath>:<filesize>" — S:と同じ末尾':'区切り規則。relpathは"/"区切りの
    // サブフォルダを含み得る(例: "notes/todo.txt")。
    const String rest = String(cmd + 2);
    const int lastColon = rest.lastIndexOf(':');
    if (lastColon <= 0 || lastColon == static_cast<int>(rest.length()) - 1) {
      if (Serial) Serial.println("[BLE] F: parse error (missing size separator)");
      sendError(ErrorCode::kSize);
      return;
    }
    const String relPath = rest.substring(0, lastColon);
    const long size = rest.substring(lastColon + 1).toInt();
    if (relPath.length() == 0 || relPath.startsWith("/") || relPath.indexOf("..") >= 0 || size <= 0) {
      if (Serial) {
        Serial.printf("[BLE] F: invalid relpath/size (relpath=\"%s\" size=%ld)\n", relPath.c_str(), size);
      }
      sendError(ErrorCode::kSize);
      return;
    }

    if (!SdMan.ready()) {
      if (Serial) Serial.println("[BLE] F: SD not ready");
      sendError(ErrorCode::kIo);
      return;
    }
    const String path = String(kUserRoot) + "/" + relPath;
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
      // 中間ディレクトリを1階層ずつ確認・作成する(既存でも失敗しないよう
      // ensureDirPathExists()を使う、上記コメント参照)。
      if (!ensureDirPathExists(path.substring(0, lastSlash))) {
        if (Serial) Serial.printf("[BLE] F: failed to ensure dir \"%s\"\n", path.substring(0, lastSlash).c_str());
        sendError(ErrorCode::kIo);
        return;
      }
    }
    if (currentFile_) currentFile_.close();  // S:と同じ防御コード(上記コメント参照)
    currentFile_ = SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!currentFile_) {
      if (Serial) {
        Serial.printf("[BLE] F: failed to open \"%s\" for write (exists=%d)\n", path.c_str(),
                      SdMan.exists(path.c_str()) ? 1 : 0);
      }
      sendError(ErrorCode::kIo);
      return;
    }

    currentKind_ = TransferKind::kFolderSync;
    fileName_ = relPath;
    currentFilePath_ = path;
    fileSize_ = static_cast<uint32_t>(size);
    receivedBytes_ = 0;
    state_ = State::kReceiving;
    if (Serial) Serial.printf("[BLE] F: start receiving \"%s\" (%ld bytes)\n", path.c_str(), size);
    return;
  }

  if (len >= 2 && cmd[0] == 'P' && cmd[1] == ':') {
    // S:/F:と同じ理由でkErrorは拒否しない(上記コメント参照)。
    if (state_ == State::kReceiving) {
      if (Serial) Serial.println("[BLE] P: rejected, already receiving another file");
      failTransfer(ErrorCode::kState, false);
      return;
    }

    // "P:<abspath>:<filesize>" — PC側でツリー選択した任意の絶対パスへ保存する
    // (ライブテキストの保存先選択機能用)。S:/F:と同じ末尾':'区切り規則。
    const String rest = String(cmd + 2);
    const int lastColon = rest.lastIndexOf(':');
    if (lastColon <= 0 || lastColon == static_cast<int>(rest.length()) - 1) {
      if (Serial) Serial.println("[BLE] P: parse error (missing size separator)");
      sendError(ErrorCode::kSize);
      return;
    }
    const String path = rest.substring(0, lastColon);
    const long size = rest.substring(lastColon + 1).toInt();
    if (path.length() == 0 || !path.startsWith("/") || path.indexOf("..") >= 0 || size <= 0) {
      if (Serial) Serial.printf("[BLE] P: invalid path/size (path=\"%s\" size=%ld)\n", path.c_str(), size);
      sendError(ErrorCode::kSize);
      return;
    }

    if (!SdMan.ready()) {
      if (Serial) Serial.println("[BLE] P: SD not ready");
      sendError(ErrorCode::kIo);
      return;
    }
    const int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
      if (!ensureDirPathExists(path.substring(0, lastSlash))) {
        if (Serial) Serial.printf("[BLE] P: failed to ensure dir \"%s\"\n", path.substring(0, lastSlash).c_str());
        sendError(ErrorCode::kIo);
        return;
      }
    }
    if (currentFile_) currentFile_.close();  // S:/F:と同じ防御コード(上記コメント参照)
    currentFile_ = SdMan.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!currentFile_) {
      if (Serial) {
        Serial.printf("[BLE] P: failed to open \"%s\" for write (exists=%d)\n", path.c_str(),
                      SdMan.exists(path.c_str()) ? 1 : 0);
      }
      sendError(ErrorCode::kIo);
      return;
    }

    currentKind_ = TransferKind::kReceived;
    fileName_ = path;
    currentFilePath_ = path;
    fileSize_ = static_cast<uint32_t>(size);
    receivedBytes_ = 0;
    state_ = State::kReceiving;
    if (Serial) Serial.printf("[BLE] P: start receiving \"%s\" (%ld bytes)\n", path.c_str(), size);
    return;
  }

  if (len >= 3 && cmd[0] == 'L' && cmd[1] == 'S' && cmd[2] == ':') {
    // "LS:<path>" — 指定ディレクトリ直下の一覧をStatus経由で返す
    // (PC側の保存先ツリー選択ダイアログ用)。転送状態には影響しない
    // (kIdle/kReceiving/kErrorのいずれの最中でも実行できる、読み取り専用の
    // 操作のため)。
    const String path = String(cmd + 3);
    sendDirectoryListing(path);
    return;
  }

  if (len >= 2 && cmd[0] == 'X' && cmd[1] == ':') {
    // "X:<relpath>" — 同期ルート配下の該当ファイルを削除する。
    const String relPath = String(cmd + 2);
    if (relPath.length() == 0 || relPath.startsWith("/") || relPath.indexOf("..") >= 0) {
      if (Serial) Serial.printf("[BLE] X: invalid relpath \"%s\"\n", relPath.c_str());
      sendError(ErrorCode::kSize);
      return;
    }
    const String path = String(kUserRoot) + "/" + relPath;
    SdMan.remove(path.c_str());
    deletedPath_ = relPath;
    deletedPending_ = true;
    if (Serial) Serial.printf("[BLE] X: deleted \"%s\"\n", path.c_str());
    sendStatus("DONE");
    return;
  }

  if (len == 1 && cmd[0] == 'E') {
    if (state_ != State::kReceiving) {
      if (Serial) Serial.println("[BLE] E: rejected, not currently receiving");
      failTransfer(ErrorCode::kState, false);
      return;
    }
    currentFile_.close();
    state_ = State::kIdle;
    if (Serial) {
      Serial.printf("[BLE] E: done, %lu bytes written to \"%s\"\n", static_cast<unsigned long>(receivedBytes_),
                    currentFilePath_.c_str());
    }
    // LiveTextScreenが開いている間(liveWatchActive_)に完了した単独ファイル転送
    // ("S:"/"P:"、TransferKind::kReceived)は、通常のfileDonePending_
    // (BluetoothScreen/FolderSyncScreen用)とは独立して、LiveTextScreen専用の
    // 完了通知も立てる。LiveTextは常に単一の固定パスだけを扱うため、書き込み先
    // パスの記録・比較は不要。
    if (liveWatchActive_ && currentKind_ == TransferKind::kReceived) {
      liveTextUpdatedPending_ = true;
    }
    fileDonePending_ = true;
    sendStatus("DONE");
    return;
  }

  if (len == 1 && cmd[0] == 'C') {
    if (state_ == State::kReceiving) {
      currentFile_.close();
      SdMan.remove(currentFilePath_.c_str());
    }
    if (Serial) Serial.println("[BLE] C: transfer cancelled");
    resetTransferState();
    return;
  }

  if (len >= 2 && cmd[0] == 'Y' && cmd[1] == ':') {
    // "Y:"は新しい同期シーケンスの開始を示すコマンド。BluetoothScreen/
    // FolderSyncScreenを開いたときのclearError()はUI画面遷移時にしか呼ばれないが、
    // PC側のローカルフォルダ同期はデバイス側の画面遷移(SYNC:REQUEST)を経由せず
    // 自発的にY:を送ってくる設計のため、ここでも保険として前回のエラー状態を
    // クリアしておく(転送進行中でなければの話。進行中に届くのは想定外だが、
    // その状態を勝手に破棄しないよう念のため対象外にする)。
    if (state_ != State::kReceiving) {
      state_ = State::kIdle;
      lastError_ = ErrorCode::kNone;
    }
    syncCount_ = atoi(cmd + 2);
    syncCountPending_ = true;
    if (Serial) Serial.printf("[BLE] Y: sync count = %d\n", syncCount_);
    return;
  }

  if (Serial) Serial.printf("[BLE] unrecognized control command: \"%s\"\n", cmd);
}

void BleTransferService::processDataChunk(const uint8_t* data, size_t len) {
  if (state_ != State::kReceiving) {
    if (Serial) {
      Serial.printf("[BLE] data chunk (%u bytes) rejected, not currently receiving (state=%d)\n",
                    static_cast<unsigned>(len), static_cast<int>(state_));
    }
    failTransfer(ErrorCode::kState, false);
    return;
  }

  const size_t written = currentFile_.write(data, len);
  if (written != len) {
    // SDカードの空き容量不足が最も典型的な原因のためFULLとして報告する。
    if (Serial) {
      Serial.printf("[BLE] write failed: wrote %u of %u bytes to \"%s\"\n", static_cast<unsigned>(written),
                    static_cast<unsigned>(len), currentFilePath_.c_str());
    }
    failTransfer(ErrorCode::kFull, true);
    return;
  }

  receivedBytes_ += static_cast<uint32_t>(written);
  char buf[24];
  snprintf(buf, sizeof(buf), "OK:%lu", static_cast<unsigned long>(receivedBytes_));
  sendStatus(buf);
}

void BleTransferService::failTransfer(ErrorCode code, bool removePartialFile) {
  if (removePartialFile && state_ == State::kReceiving) {
    currentFile_.close();
    SdMan.remove(currentFilePath_.c_str());
  }
  resetTransferState();
  sendError(code);
}

void BleTransferService::resetTransferState() {
  if (currentFile_) currentFile_.close();
  state_ = State::kIdle;
  currentKind_ = TransferKind::kNone;
  fileName_ = "";
  currentFilePath_ = "";
  fileSize_ = 0;
  receivedBytes_ = 0;
}

void BleTransferService::clearError() {
  if (state_ == State::kError) {
    state_ = State::kIdle;
    lastError_ = ErrorCode::kNone;
  }
}

void BleTransferService::sendStatus(const char* text) {
  if (!statusChar_) return;
  statusChar_->setValue(reinterpret_cast<const uint8_t*>(text), strlen(text));
  statusChar_->notify();
}

void BleTransferService::sendError(ErrorCode code) {
  lastError_ = code;
  state_ = State::kError;
  errorPending_ = true;
  char buf[16];
  snprintf(buf, sizeof(buf), "ERR:%s", errorCodeLabel(code));
  sendStatus(buf);
}

bool BleTransferService::sendSyncRequest() {
  if (!connected_) return false;
  sendStatus("SYNC:REQUEST");
  return true;
}

bool BleTransferService::consumeSyncCountReceived(int& count) {
  if (!syncCountPending_) return false;
  count = syncCount_;
  syncCountPending_ = false;
  return true;
}

bool BleTransferService::consumeFileDone() {
  if (!fileDonePending_) return false;
  fileDonePending_ = false;
  return true;
}

bool BleTransferService::consumeDeletedFile(String& relPath) {
  if (!deletedPending_) return false;
  relPath = deletedPath_;
  deletedPending_ = false;
  return true;
}

bool BleTransferService::consumeError(ErrorCode& code) {
  if (!errorPending_) return false;
  code = lastError_;
  errorPending_ = false;
  return true;
}

void BleTransferService::beginLiveWatch() { liveWatchActive_ = true; }

void BleTransferService::endLiveWatch() {
  liveWatchActive_ = false;
  liveTextUpdatedPending_ = false;
}

bool BleTransferService::consumeLiveTextUpdated() {
  if (!liveTextUpdatedPending_) return false;
  liveTextUpdatedPending_ = false;
  return true;
}

void BleTransferService::sendDirectoryListing(const String& path) {
  if (!fileBrowser_.ready()) {
    if (Serial) Serial.println("[BLE] LS: SD not ready");
    sendStatus("LERR:IO");
    return;
  }

  const std::vector<DirEntry> entries = fileBrowser_.listDirectory(path.c_str());
  if (Serial) Serial.printf("[BLE] LS: \"%s\" -> %u entries\n", path.c_str(), (unsigned)entries.size());

  int count = 0;
  char buf[kControlBufSize];
  for (const DirEntry& entry : entries) {
    if (count >= kMaxListEntries) break;
    if (entry.isDirectory) {
      snprintf(buf, sizeof(buf), "LD:%s", entry.name.c_str());
    } else {
      snprintf(buf, sizeof(buf), "LF:%s:%lu", entry.name.c_str(), static_cast<unsigned long>(entry.size));
    }
    sendStatus(buf);
    // NimBLEの内部送信キューが溢れて通知が失われないよう、各エントリの送信後に
    // 短く待つ(フォルダ一覧取得は低頻度の操作なのでブロッキングの影響は小さい)。
    delay(15);
    count++;
  }
  sendStatus("LEND");
}

const char* BleTransferService::errorCodeLabel(ErrorCode code) {
  switch (code) {
    case ErrorCode::kSize: return "SIZE";
    case ErrorCode::kIo: return "IO";
    case ErrorCode::kFull: return "FULL";
    case ErrorCode::kState: return "STATE";
    default: return "NONE";
  }
}
