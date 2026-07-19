#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <SDCardManager.h>

#include "FileBrowserService.h"

// BLEファイル転送・クラウド同期用のGATTサーバー。UUID・コマンドプロトコルは
// PCアプリ側(Python)と完全一致させる必要がある(BleTransferService.cppの
// 無名namespace内の定数参照)。
//
// 設計方針: NimBLEのコールバック(onWrite等)はBLEスタックの内部タスクから
// 呼ばれ、Arduinoのloop()とは別のFreeRTOSタスクコンテキストで実行される。
// EPD(E-ink)とSDカードはSPIバスを共有しており、loop()側のE-ink描画と同時に
// BLEコールバックからSDへ書き込むとバスの競合が起きうる(FileBrowserService.cpp
// 参照)。これを避けるため、コールバック側では受信データをいったん小さな
// バッファへコピーするだけに留め、実際のSD書き込み・状態遷移はすべて
// update()(main.cppのloop()から毎回呼ぶ)側、つまりloop()と同じタスク
// コンテキストで行う。コールバック↔update()間の受け渡しはportMUX_TYPEで保護する。
class BleTransferService {
 public:
  enum class State { kIdle, kReceiving, kError };
  enum class ErrorCode { kNone, kSize, kIo, kFull, kState };

  // fileBrowserは"LS:"(フォルダ一覧取得)コマンドの実装に使う。呼び出し側
  // (main.cpp)がFileBrowserServiceより後にBleTransferServiceを構築すること。
  explicit BleTransferService(FileBrowserService& fileBrowser)
      : serverCallbacks_(*this), charCallbacks_(*this), fileBrowser_(fileBrowser) {}

  // GATTサーバー・サービス・キャラクタリスティックを構築する(setup()で1回だけ
  // 呼ぶ)。アドバタイズはここでは開始しない(startAdvertising()参照)。
  bool begin();

  // BluetoothScreen/CloudSyncScreenを開いている間だけ呼ぶ想定
  // (常時アドバタイズはしない、画面を離れたらstopAdvertising()を呼ぶこと)。
  void startAdvertising();
  void stopAdvertising();

  bool isConnected() const { return connected_; }
  const String& deviceName() const { return deviceName_; }

  // main.cppのloop()から毎回呼ぶ。BLEコールバックが溜めた受信データ・コマンドを
  // 実際に処理する(SD書き込み・状態遷移はここでのみ行う)。
  void update();

  State state() const { return state_; }
  const String& currentFileName() const { return fileName_; }
  uint32_t currentFileSize() const { return fileSize_; }
  uint32_t receivedBytes() const { return receivedBytes_; }
  ErrorCode lastError() const { return lastError_; }
  static const char* errorCodeLabel(ErrorCode code);

  // BluetoothScreen/CloudSyncScreenへ再入したときに直前のエラー表示を
  // 持ち越さないようにする(受信中でなければkErrorをkIdleへ戻すだけ)。
  void clearError();

  // FolderSyncScreenに入った時点でBLE接続中なら呼ぶ。Status "SYNC:REQUEST"を送る。
  bool sendSyncRequest();
  // "Y:<count>"受信を1回だけ消費する(FolderSyncScreen用、今回の同期操作
  // (ファイル転送+削除)の総数を意味する)。
  bool consumeSyncCountReceived(int& count);
  // ファイル受信完了("E"処理・DONE送信済み)を1回だけ消費する。
  bool consumeFileDone();
  // "X:<relpath>"によるファイル削除完了を1回だけ消費する(FolderSyncScreen用)。
  bool consumeDeletedFile(String& relPath);
  // 転送エラーを1回だけ消費する。
  bool consumeError(ErrorCode& code);

  // LiveTextScreenを開いている間だけ呼ぶ(main.cpp/LiveTextScreen参照)。有効な間に
  // 単独ファイル転送("S:"/"P:"、TransferKind::kReceived。フォルダ同期"F:"は含めない)
  // が完了すると、consumeLiveTextUpdated()がtrueを返すようになる(通常の
  // consumeFileDone()とは独立させ、BluetoothScreen等の他画面のポーリングと
  // 混線しないようにするため)。LiveTextは常に単一の固定パス(LiveTextScreen::
  // kDefaultPath)だけを扱う一時ファイル方式のため、書き込み先パスの事前一致
  // チェックや呼び出し元への通知は不要(常に「今表示すべきファイル」とみなす)。
  void beginLiveWatch();
  void endLiveWatch();
  bool consumeLiveTextUpdated();

  // "LS:<path>"(フォルダ一覧取得)の応答("LD:"/"LF:"/"LEND"/"LERR:")は、
  // 通常の転送応答(OK:/ERR:/DONE)と混線しないよう、PC側が別のキューで
  // 受け取る設計になっている(protocol.py参照)。ファームウェア側では単に
  // Status特性への一連のNotify送信として処理するだけで、応答を待つ処理は
  // 持たない。

 private:
  class ServerCallbacks : public NimBLEServerCallbacks {
   public:
    explicit ServerCallbacks(BleTransferService& owner) : owner_(owner) {}
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override;

   private:
    BleTransferService& owner_;
  };

  class CharCallbacks : public NimBLECharacteristicCallbacks {
   public:
    explicit CharCallbacks(BleTransferService& owner) : owner_(owner) {}
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override;
    void onSubscribe(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override;

   private:
    BleTransferService& owner_;
  };

  // 転送開始コマンド("S:"/"F:"/"P:")の種別。"E"受信時、どの完了通知を立てるかの
  // 判定に使う(BluetoothScreen/FolderSyncScreen/LiveTextScreenのポーリングが
  // 互いに混線しないよう独立させるため)。liveWatchActive_が有効な間にkReceived
  // (S:/P:)が完了した場合はliveTextUpdatedPending_も追加で立てる
  // (processControlCommand()参照、専用のTransferKindは持たない)。
  enum class TransferKind { kNone, kReceived, kFolderSync };

  static constexpr size_t kControlBufSize = 128;
  static constexpr size_t kDataBufSize = 512;
  // 1回のLS:応答で列挙する最大エントリ数(応答時間・BLE帯域の安全上限)。
  static constexpr int kMaxListEntries = 200;

  // NimBLEDevice::init()(GATTサーバー・サービス・キャラクタリスティック構築を含む)は
  // ヒープを恒常的に(数十KB規模)消費するため、begin()では行わずstartAdvertising()から
  // 初回のみ呼ぶ(server_ != nullptrなら以降は何もしない)。Bluetooth/FolderSync/
  // LiveTextのいずれの画面も一度も開かなければ、この分のヒープはTXT/Markdown読書用に
  // 残ったままになる(BleTransferService.cppのbegin()コメント参照)。
  void ensureStackReady();

  void enqueueControlCommand(const uint8_t* data, size_t len);
  void enqueueDataChunk(const uint8_t* data, size_t len);
  void processControlCommand(const char* cmd);
  void processDataChunk(const uint8_t* data, size_t len);
  void sendStatus(const char* text);
  void sendError(ErrorCode code);
  void resetTransferState();
  void failTransfer(ErrorCode code, bool removePartialFile);
  void sendDirectoryListing(const String& path);

  ServerCallbacks serverCallbacks_;
  CharCallbacks charCallbacks_;
  FileBrowserService& fileBrowser_;

  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* controlChar_ = nullptr;
  NimBLECharacteristic* dataChar_ = nullptr;
  NimBLECharacteristic* statusChar_ = nullptr;

  String deviceName_;
  bool connected_ = false;
  bool wantAdvertising_ = false;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  // BLEコールバック→update()への受け渡し用(mux_で保護)。
  char pendingControlBuf_[kControlBufSize] = {0};
  bool pendingControlFlag_ = false;
  uint8_t pendingChunkBuf_[kDataBufSize] = {0};
  size_t pendingChunkLen_ = 0;
  bool pendingChunkFlag_ = false;

  // 転送状態(update()側のみが変更する)。
  State state_ = State::kIdle;
  TransferKind currentKind_ = TransferKind::kNone;
  String fileName_;       // 表示用(S:はファイル名のみ、F:/L:は相対/絶対パス)
  String currentFilePath_;  // 実際に開いている完全パス(E/C/失敗時の削除で使う)
  uint32_t fileSize_ = 0;
  uint32_t receivedBytes_ = 0;
  FsFile currentFile_;
  ErrorCode lastError_ = ErrorCode::kNone;

  bool fileDonePending_ = false;
  bool errorPending_ = false;
  int syncCount_ = 0;
  bool syncCountPending_ = false;

  bool deletedPending_ = false;
  String deletedPath_;

  bool liveWatchActive_ = false;  // LiveTextScreenが開いている間だけtrue
  bool liveTextUpdatedPending_ = false;
};
