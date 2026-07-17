# Xteink X3 カスタムファームウェア

Xteink X3（ESP32-C3搭載のE-inkリーダー）向けの自作リーダーファームウェア。
公式ファームウェアのUI/UXを参考に、UI・リーダーロジックをゼロから実装する。

## ハードウェア

| 項目 | 内容 |
|---|---|
| MCU | ESP32-C3 (シングルコアRISC-V、RAM約380KB、Flash 16MB) |
| ディスプレイ | E-ink 792x528 (SSD1677系コントローラ) |
| ストレージ | microSD (FAT32) |
| バッテリー監視 | BQ27220フューエルゲージ (I2C 0x55) |
| その他I2C | QMI8658 IMU (0x6B)、DS3231 RTC (0x68) |

### GPIO割り当て

| GPIO | 機能 |
|---|---|
| 0 | I2C SCL |
| 1 | ボタンADC1 (4ボタン抵抗ラダー) |
| 2 | ボタンADC2 (2ボタン抵抗ラダー) |
| 3 | 電源ボタン (アクティブLOW、スリープ復帰) |
| 4 / 5 / 6 | EPD DC / RST / BUSY |
| 7 / 8 / 10 | SPI MISO / SCLK / MOSI (EPD・SD共有) |
| 12 / 13 | SDカード CS / 電源制御 |
| 20 | I2C SDA |
| 21 | EPD CS |

### 物理ボタンの配置（実機写真で確認済み）

| 位置 | ボタン |
|---|---|
| 底面（横一列、左から） | もどる(BACK) / ✓(CONFIRM) / ←(LEFT) / →(RIGHT) |
| 左側面 | ↑(UP) |
| 右側面 | ↓(DOWN) |
| 右上端 | 電源(POWER) |
| 左上端 | リセット（ソフトウェアからは制御しない物理リセット） |

UI側の`PhysicalButton` enum（`ui/FooterGuide.h`）は底面の4ボタンのみを対象にしている。
UP/DOWNは側面のため画面下部のフッターでは位置合わせできず、表示から省略している。

## セットアップ

ハードウェア抽象化層として [open-x4-sdk](https://github.com/open-x4-epaper/community-sdk)
をローカルクローンで利用する（gitでは追跡しない）:

```
git clone https://github.com/open-x4-epaper/community-sdk.git open-x4-sdk
```

ビルドと書き込み（PlatformIO）:

```
pio run              # ビルド
pio run -t upload    # 書き込み
pio device monitor   # シリアルログ確認 (115200bps)
```

## 書き込み前のバックアップ

純正ファームウェアに戻せるよう、初回書き込み前にフラッシュ全体を吸い出しておく:

```
esptool --chip esp32c3 --port COM3 --baud 921600 read_flash 0x0 0x1000000 x3_stock_firmware_full_16MB.bin
```

復元する場合:

```
esptool --chip esp32c3 --port COM3 --baud 921600 write_flash 0x0 x3_stock_firmware_full_16MB.bin
```

（バックアップの保存先: `C:\Users\Riku\dev\XTEink\backup\`）

## 開発フェーズ

- [x] フェーズ0: プロジェクトセットアップ、Hello World描画、ボタン入力のシリアル出力
- [x] フェーズ1: 共通UIコンポーネント (ステータスバー、SettingRow、HomeGridButton、フッターガイド)
- [x] フェーズ2: ホーム画面とフォルダ画面
- [x] フェーズ2.5: SVGアイコン(Material Symbols)のオフライン変換とプレースホルダー差し替え
- [x] フェーズ2.6: ステータスバーへのバッテリーアイコン表示（残量3段階、充電中は保留）
- [x] フェーズ2.7: BQ27220からの実残量取得（kode_BQ27220ライブラリ導入）
- [x] フェーズ2.8: 電流の符号による充電中アイコン表示
- [ ] フェーズ3: TXT読書画面と基本の読書中メニュー
- [ ] フェーズ4: 設定画面
- [ ] フェーズ5: フォントシステム (SDカードからのカスタムフォント読み込み、CJK対応)
- [ ] フェーズ6: Markdown対応
- [ ] フェーズ7: EPUB対応

クラウド同期機能は実装しない。

## 構成方針

```
src/
├── main.cpp        # エントリポイント
├── gfx/            # 描画まわり (フレームバッファ操作、フォント、アイコン)
├── ui/             # 共通UIコンポーネント (フェーズ1〜)
├── screens/        # 各画面 (フェーズ2〜)
├── core/           # 設定・書籍管理などのロジック (フェーズ2〜)
└── generated/       # scripts/convert_icons.py の自動生成物 (手動編集しない)

assets/icons/        # アイコンのSVG原本 (Material Symbols, Apache License 2.0)
scripts/              # ビルド前処理スクリプト
```

## アイコンの変換（フェーズ2.5〜）

`assets/icons/*.svg`(Material Symbols由来)を、ビルド前にオフラインで1bppモノクロ
ビットマップへ変換し、`src/generated/icons_generated.h`として出力する。実機
(ESP32-C3)ではSVGパース・ベクター描画・実行時スケーリングを一切行わない。
サイズ違い(24px/40px)はそれぞれ別のビットマップとしてラスタライズ済み。

SVGを追加・変更した場合のみ再実行する（`icons_generated.h`はコミット対象。
ビルドのたびに自動実行はしない）:

```
pip install resvg-py Pillow
python scripts/convert_icons.py
```

### バッテリー表示について

`StatusBar::setBatteryPercent(int)`で残量(0-100)に応じてアイコン
(`battery_full`/`battery_half`/`battery_low`、閾値60%・20%)を出し分けている。

X3実機のBQ27220(I2C 0x55)からの実残量取得には、SDKの`BatteryMonitor`(X4の
ADC分圧方式専用で使えない、下記注意点参照)ではなく、外部ライブラリ
[kode_BQ27220](https://github.com/kodediy/kode_BQ27220)(Apache-2.0)を
`core/BatteryService.h`でラップして使っている。`main.cpp`が起動時と30秒おきに
残量を読み直し、値が変化したときだけ両画面のStatusBarへ反映して部分更新する。

実機での実測値(USB接続中): 4363mV, 100%。妥当な範囲の値が取れることを確認済み。

充電中は`battery_charging`アイコンを残量アイコンより優先して表示する
(`StatusBar::setBatteryCharging(bool)`)。充電判定は`BatteryService::isCharging()`が
瞬時電流(`readCurrentMilliamps()`)の符号を見て行っている
(閾値+5mA超で充電中と判定)。kode_BQ27220の作者コメント"positive=charging"に基づく判断で、
実機のUSB接続時に電流+46mA・`BatteryStatus()`のDSGビット(bit0)=0という結果が
一貫して得られたことを根拠にしている。

**検証の限界**: X3実機にポゴピン等USB以外の充電手段が無く、USB接続時(充電中と
推測される状態)のデータしか取得できていない。非充電状態での電流値との比較が
できていないため、「電流が正=充電中」という判定が本当に正しいかは完全には
確認できていない。`BatteryStatus()`のDSGビット自体も、kode_BQ27220のヘッダに
「レジスタマップは類似のTI fuel gauge(bq27441等)からの推定値」という注記があり、
生値の取得(`BatteryService::readRawBatteryStatus()`)はデバッグ用途にとどめている。

### 注意点

- SDKの`BatteryMonitor`はX4のADC分圧方式用。X3はBQ27220 (I2C)なので使えない。
  実残量取得には`lib_deps`に追加した外部ライブラリ`kode_BQ27220`を使う
  (詳細は上記「バッテリー表示について」を参照)
- E-inkの更新は`FULL_REFRESH`(高品質・遅い)と`FAST_REFRESH`(部分更新・速い)を使い分ける
- 大きなファイルはRAMに全展開せず、SDカード上でストリーム処理する
- **画面の向き**: E-inkパネルはネイティブでは792x528(横長)のフレームバッファしか
  持たないが、実機は縦持ちで使う機器。UI層(`gfx/`・`ui/`)は常に528x792(縦長)の
  論理座標で描画し、`FrameBufferOps`が物理座標への90度回転変換を行う
  (実機を時計回りに90度回すと正しい向きになる)
- テキスト描画は`Font`インターフェース(`gfx/Font.h`)経由で行い、UIコンポーネント
  側でpxのハードコードによるレイアウトをしない。現在の実装は`MiniFontImpl`
  (ASCII専用5x7固定幅)のみだが、フェーズ5で可変幅CJKフォントに差し替える設計
- **SDカード初期化とSPIバスの落とし穴**: `EInkDisplay::begin()`が内部で
  `SPI.begin(sclk, -1, mosi, cs)`とMISOピン抜きでSPIバスを初期化する。ESP32の
  `SPIClass::begin()`は「既にバスが初期化済みなら何もせずtrueを返すだけ」の
  実装のため、後からMISOを指定して`SPI.begin()`を呼び直しても無視される。
  SDカード(`FileBrowserService::begin()`)を初期化する前に必ず`SPI.end()`で
  切断してから`SPI.begin(sclk, miso, mosi, -1)`で再初期化すること。またSDカードの
  電源制御(GPIO13)は単純な`OUTPUT`ではなく`OUTPUT_OPEN_DRAIN`で駆動する必要がある
  (実機解析情報の"OUTPUT with pullup"に対応)。どちらか片方でも欠けると
  `SD_CARD_ERROR_CMD0`(応答なし)で検出に失敗する
- フォルダ画面のファイル/フォルダ一覧には`SDCardManager::listFiles()`ではなく
  `FileBrowserService`(自作ラッパー、`core/FileBrowserService.h`)を使う。
  `listFiles()`はディレクトリを除外してしまうため、ファイラー用途には使えない
