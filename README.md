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
- [ ] フェーズ1: 共通UIコンポーネント (ステータスバー、SettingRow、HomeGridButton、フッターガイド)
- [ ] フェーズ2: ホーム画面とフォルダ画面
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
├── gfx/            # 描画まわり (フレームバッファ操作、フォント)
├── ui/             # 共通UIコンポーネント (フェーズ1〜)
├── screens/        # 各画面 (フェーズ2〜)
└── core/           # 設定・書籍管理などのロジック (フェーズ2〜)
```

### 注意点

- SDKの`BatteryMonitor`はX4のADC分圧方式用。X3はBQ27220 (I2C)なので自作ドライバが必要
- E-inkの更新は`FULL_REFRESH`(高品質・遅い)と`FAST_REFRESH`(部分更新・速い)を使い分ける
- 大きなファイルはRAMに全展開せず、SDカード上でストリーム処理する
