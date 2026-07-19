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
| 12 | SDカード CS |
| 13 | バッテリー電源ラッチMOSFET制御(SDカードとは無関係、下記注意点参照) |
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

**UP/DOWNの長押しショートカット**: 側面のUP/DOWNは短押しではその画面本来の意味
(フォーカス移動等)のまま、`kLongPressMs`(500ms)以上の長押しではCONFIRM/BACKの
代わりとして働く(UP長押し=CONFIRM、DOWN長押し=BACK)。底面のCONFIRM/BACKまで
持ち替えなくても片手の側面ボタンだけで一通り操作できるようにする目的。
`main.cpp`の`loop()`内で、UP/DOWNのみ`wasPressed()`ではなく`wasReleased()`+
`InputManager::getHeldTime()`で判定し、長押しと判定した場合はBTN_CONFIRM/
BTN_BACKのボタンコードに置き換えて各画面の`handleButton()`に渡す(各画面側の
実装は一切変更していないので、`FolderScreen`のようにUP/DOWNへ独自の意味
(ディレクトリ階層移動)を割り当てている画面とも衝突しない)。短押しの判定も
「離した瞬間」に確定するため、追加の待ち時間は発生しない(ダブルクリックで
区別する方式だと2回目の入力を待つ必要があり反応が遅れるため、長押し方式を採用した)。

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
- [x] フェーズ2.9: バッテリー駆動時のフリーズ不具合を修正(GPIO13の役割誤認が原因)
- [x] フェーズ2.10: DS3231 RTCからの時刻読み書きに対応(UI表示・NTP同期は別途)
- [x] フェーズ3: TXT読書画面と基本の読書中メニュー
- [x] フェーズ3.5: CJK(日本語)フォント対応(crosspoint-jpの.cpfont形式を移植、詳細は「CJKフォントについて」参照)
- [x] フェーズ4: 設定画面(時刻設定・ステータスバー時刻表示・システムフォント/文字サイズ切り替え・端末情報・キャッシュ削除)
- [x] フェーズ5: フォントシステムの残り (フォント/サイズ切り替えUIをフェーズ4で実装。読み込み自体はフェーズ3.5で対応済み)
- [x] フェーズ6: Markdown対応(見出し・箇条書き・コードブロックの最小構成、詳細は「Markdown対応について」参照)
- [ ] フェーズ6.5: BLEファイル転送・クラウド同期
- [ ] フェーズ7: EPUB対応

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
なお、[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)の
`HalGPIO::isUsbConnected()`もX3では同じくBQ27220のCurrent()レジスタの符号で
USB/充電を判定しており、このアプローチ自体は裏付けが取れている。

### RTC(時刻)について

X3実機にはDS3231 RTC(I2C 0x68、バッテリー・IMUと同じバスを共有)が搭載されている。
`core/RtcService.h`が外部ライブラリを使わずWireで直接レジスタを読み書きする薄い
ラッパーで、[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)の
`lib/hal/HalClock.cpp`(同一ハードウェア向けの参考実装)を参考にした。当初候補に
挙がった[libdriver/ds3231](https://github.com/libdriver/ds3231)は汎用MCU/Linux向けの
フル機能ドライバで、`/interface`層のプラットフォーム固有IIC実装を自前で書く必要が
あり、時刻の読み書きだけが目的の現段階では過剰と判断し採用しなかった。

`RtcService::readDateTime()`/`writeDateTime()`で年月日時分秒を扱う(年はcenturyビットを
無視して常に2000年代のフル西暦として扱う)。`lostPower()`はステータスレジスタ
(0x0F)のOscillator Stop Flag(bit7)を見て、バックアップ電源が一度でも切れて
発振が停止したか(=保持されている時刻が信頼できないか)を判定する。

現時点ではUIへの時刻表示・NTP同期(WiFi経由)は未実装。`main.cpp`の`setup()`で
検出状況と現在時刻をシリアルログに出力するのみ。

### TXT読書画面について(フェーズ3)

`core/TxtReaderService.h`がページング・進捗保存のコアロジックを、
`screens/TxtReaderScreen.h`が表示・ボタン操作を担当する。フォルダ画面で`.txt`
ファイルを選択する(`.md`/`.epub`はまだ非対応)か、ホーム画面の「READ」で
続きの本を開くと遷移する。

- **ページング**: ファイル全体をRAMに展開せず、SD上をチャンク単位(4KB)で
  ストリーム読みしながら`Font::measureText()`で折り返し、各ページの開始バイト
  オフセット一覧(ページインデックス)を構築する。構築方式は
  [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)の
  `TxtReaderActivity`(`src/activities/reader/TxtReaderActivity.cpp`)を参考にした
  (グレースケール・傾きセンサー・多言語対応など本プロジェクトに不要な機能は
  持ち込んでいない)。ページインデックスはSD上の隠しディレクトリ
  `/.reader_cache/`にキャッシュし(ファイル名はパスの`/`を`_`に置換したもの)、
  ファイルサイズ・表示幅・1ページ行数が前回と一致すれば再スキャンをスキップする。
- **ボタン**: LEFT/UPで前ページ、RIGHT/DOWNで次ページ(同じ意味を両方に割り当て)。
  BACKで「閉じてホームへ」の確認オーバーレイを開閉する(基本の読書中メニュー、
  項目は1つのみ)。CONFIRMで確定してホームへ、もう一度BACKでキャンセルして読書に戻る。
- **進捗の永続化**: ページをめくるたび(および本を開いた時点でも)、現在ページ番号を
  `/.reader_cache/<sanitized>.pos`に、パス+進捗%を`/.reader_cache/last_book.txt`に
  保存する。次回同じファイルを開くと保存済みページから再開し、ホーム画面の本
  プレースホルダーには`last_book.txt`の内容(直近に開いていた本)を表示する。

**既知の制限**: 折り返しの二分探索はバイト単位で行うが、結果をマルチバイト
UTF-8の文字境界まで後退させてから確定するため、日本語のようにほぼ全バイトが
マルチバイトの文章でも文字の途中で切れることはない(`appendWrapped()`内の
`isUtf8Continuation()`によるスナップ処理)。ただし1ページの読み込みは1チャンク
(4KB)までしか読まないため、極端に短い行が大量に続くページは本来の行数上限
より短くなることがある(本文が欠落することはなく、次ページで続きが表示される)。
E-ink更新は既存画面と同様ページ送りのたびに`FAST_REFRESH`を使っており、
長時間読書時のゴースト対策(定期的なFULL_REFRESH差し込み)は今後の課題。

### CJKフォントについて(日本語表示)

TXT読書画面の本文描画には、ASCII専用の`MiniFontImpl`ではなく
[crosspoint-jp](https://github.com/zrn-ns/crosspoint-jp)(Xteink X3/X4向け日本語
特化ファームウェア)のSDカードフォント形式(`.cpfont`)を読み込む
`gfx/CjkFontImpl.h`を使う。`TxtReaderScreen::setContentFont()`で本文用に
差し替えられる(`Font`インターフェースの別実装として追加しただけで、
`StatusBar`/`FooterGuide`/`SettingRow`など他のUIコンポーネントには一切手を
入れていない)。

**移植した範囲**: crosspoint-jpの`lib/EpdFont`(`EpdFont`/`SdCardFont`)・
`lib/GfxRenderer`(`GfxRenderer::renderChar`のグリフ描画部分)を参考に、
`.cpfont`のヘッダ・スタイルTOC・文字コード区間テーブルを読み、コードポイント
ごとにグリフメトリクスと2bit/pixelビットマップをSDから都度シーク読みして
`FrameBufferOps`(1bpp)へしきい値描画する(2bit値が0=白、1以上=黒)。直近
使用した最大200グリフだけを小さなキャッシュに保持し、同一ページ内での
再読み込みを減らす。

**あえて移植しなかったもの**:
- **DEFLATE圧縮(`FontDecompressor`/`uzlib`)**: SDカードフォント(`.cpfont`)は
  非圧縮の生ビットマップとして格納されており(圧縮はフラッシュ組み込み用の
  ビルトインフォント専用の仕組み)、そもそも不要だった
- **カーニング・合字・縦書きグリフ置換**: ファイル形式には含まれるが、横書き
  専用のこのリーダーには不要。パース時に該当セクションをスキップする
  (バイトサイズだけ計算に使い、内容は読まない)
- **複数スタイル(bold/italic)・グレースケール/ダークモード**: 最初に見つかった
  スタイル1つだけを使い、`FrameBufferOps`が1bppのみのため白黒2値でしきい値化する

**フォントファイルの入手・配置**: crosspoint-jpは`.cpfont`をビルド済みで
[GitHub Releases](https://github.com/zrn-ns/crosspoint-jp/releases/download/sd-fonts/)
(タグ`sd-fonts`)に公開しており、自前でPython製フォント変換パイプラインを
動かす必要はない。`NotoSansJp_12.cpfont`(JIS X 0213対応ゴシック体、12pxグリフ、
約6.5MB)を使うことにし、SDカードの`/System/fonts/NotoSansJp_12.cpfont`に置く前提で
`main.cpp`の`CJK_FONT_PATH`を設定している。本体ファームウェアはSDをUSB
マスストレージとして公開していないため、配置はカードリーダー経由で手動で行う。
ファイルが見つからない場合、`CjkFontImpl::begin()`が`false`を返し
`MiniFontImpl`にフォールバックする(非ASCII文字は豆腐表示になるが動作は継続する)。

### Markdown対応について(フェーズ6)

フォルダ画面で`.md`/`.markdown`ファイルを選択すると、TXT読書画面
(`screens/TxtReaderScreen.h`)がMarkdownモードで開く。現在のフォントシステムは
1画面につき1スタイルしか描画できず(太字・斜体・可変フォントサイズの概念がない)、
本格的なリッチテキスト表示は土台から作り直す規模になるため、今回は最小構成に
絞った:

- **見出し(`#`〜`######`)**: 本文とは別の大きいフォント(見出しフォント)で描画し、
  行頭の`#`記号は非表示にする。見出しフォントを用意していない場合は本文と同じ
  大きさで表示されるだけ(`TxtReaderScreen::setHeadingFont()`、既定は
  `contentFont()`にフォールバック)
- **太字/斜体(`**`/`*`)**: 記号を除去してプレーン表示にする(太字・斜体としての
  スタイル変化はしない、`**`と`*`を区別せず単純に全ての`*`を取り除くだけ)
- **箇条書き(`-`/`*`/`+`)**: 行頭記号を`-`に正規化するだけで、インデント等の
  特別な整形はしない。番号付きリスト(`1. `等)は変換せずそのまま表示する
- **コードブロック(` ``` `)**: フェンス行自体を非表示にし、中身はMarkdown記法を
  解釈せずそのまま表示する(区切り線などの装飾はない、最小構成)

**設計上の重要な判断**: 記号の除去は`TxtReaderService`のページインデックス構築
(`appendWrapped()`)時点では行わず、`TxtReaderScreen`の描画直前に行っている。
理由は、記号除去でテキストの文字数が変わると、折り返し計算が消費した
バイト数(`consumed`)と実際にファイルへ書き込まれているバイト数がずれてしまい、
ページ境界(バイトオフセット)の計算が壊れるため。折り返し判定自体は記号を
含んだ「生のテキスト」に対して行われるので、わずかに保守的(記号の分だけ
余白が残ることがある)になるだけで、崩れることはない。

**既知の制限**: コードブロックがページ境界をまたぐ場合、保存済み進捗から
直接そのページへジャンプして開くと、コードブロック内かどうかの状態
(`inCodeBlock_`)が復元されず、そのページだけ見出し等の記法を誤認識する
可能性がある(逐次的にページをめくっている限りは正しく動作する)。

**見出しフォント**: `main.cpp`はMiniFontImpl用(`miniHeadingFont`、本文より
拡大率を1段階上げるだけ)とCJK用の2種類を用意する。CJK用は当初
[crosspoint-jp](https://github.com/zrn-ns/crosspoint-jp)の`.cpfont`をもう1つ
(見出しサイズ違い)ダウンロードする案を検討したが、ユーザーが既に
[XTEink Web Font Maker](https://github.com/lakafior/XTEink-Web-Font-Maker)
(純正Xteinkファームウェア向けのカスタムフォント変換ツールが使う`.bin`形式、
TTF/OTFから変換)で作成したフォントファイルをSDカードに持っていたため、
追加ダウンロードなしでそちらを使うことにした。この`.bin`形式は`.cpfont`より
さらに単純(ヘッダ無し、Unicodeコードポイントで直接インデックスされた固定サイズ
グリフの配列がそのまま並んでいるだけ)で、`gfx/XteinkBinFontImpl.h`が読み込む。
1グリフが固定サイズの等幅フォントのため、CJK以外の文字(特にラテン文字)は
字間がやや不自然になることがある。ファイル自体に幅・高さの記録が無いため、
`main.cpp`の`CJK_HEADING_FONT_WIDTH`/`CJK_HEADING_FONT_HEIGHT`にファイル名
(例: `Noto Sans JP 24pt.32×46.bin`)から読み取った値をハードコードしており、
`XteinkBinFontImpl::begin()`はファイルサイズが「0x10000文字分ちょうどか」で
簡易的に整合性を検証する。見つからない場合は本文と同じCJKフォント
(`cjkFont`)のサイズで見出しが表示される。

### 設定画面について(フェーズ4)

ホーム画面の「SETTINGS」から遷移する。`core/SettingsService.h`が`AppSettings`
構造体をSDカード上の`/.settings.bin`にバイナリで即時保存・読込する(項目が
少なくキー/バリュー形式にする必要性が薄いため、`TxtReaderService`のページ
インデックスキャッシュと同じ「構造体をそのまま読み書き」方式)。項目は
`screens/SettingsScreen.h`のUP/DOWNフォーカス移動リストで、変更のたびに
即SDへ保存する(進捗保存と同じ「都度保存」方針)。

- **TIME**: CONFIRMで時刻編集モードに入る。LEFT/RIGHTで年/月/日/時/分の
  どのフィールドを編集するか選び、UP/DOWNで増減、CONFIRMでRTC
  (`RtcService::writeDateTime()`)へ書き込む、BACKで破棄してキャンセル。
  表示・編集する値はTIMEZONE適用後のローカル時刻(下記参照)。日の上限は
  簡略化のため月によらず一律31までのラップ(厳密な月末日数チェックはしない)。
- **TIMEZONE**: CONFIRMで別ウィンドウの編集モードに入り、LEFT/RIGHT
  (またはUP/DOWN)で-9〜+9時間の範囲で調整、CONFIRMで保存、BACKでキャンセル。
  RTCの生値自体は変更せず、`RtcService::addHoursToDateTime()`で表示・TIME編集
  時にのみオフセットを加減算する(NTP同期がなく生値=ユーザーが最初に入力した
  値そのものなので、生値を直接いじるより「表示だけ時差分ずらす」方が
  タイムゾーンをまたぐ調整に強い)。
- **CLOCK IN STATUS BAR**: ON時、ホーム画面のステータスバー左側(元は
  "12:34"固定のダミー表示だった)に実際の"HH:MM"(TIMEZONE適用後)を表示する。
  バッテリー残量チェックと同様20秒間隔でRTCを読み直し、変化時のみ再描画する。
  フォルダ画面・読書画面・設定画面自身の左側テキストは元々パス/書名/
  画面名として使っているため対象外(ステータスバーの左スロットは1つしか
  ないため)。
- **SYSTEM FONT**: LEFT/RIGHTで`MiniFontImpl`とSDカードの`/System/fonts/*.cpfont`を
  1つずつ循環選択するか、CONFIRMで一覧から選ぶ別ウィンドウ(フォントピッカー)
  を開く。**UIチローム全体(StatusBar/FooterGuide/SettingRowなど)に反映
  される**、TXT読書画面の本文フォント(フェーズ3.5、`CJK_FONT_PATH`固定)
  とは独立した別設定・別`CjkFontImpl`インスタンス(`systemCjkFont`)。
  変更されるとUIラベル文字列自体("FOLDER"等)は英語のままなので、
  日本語UIになるわけではない(文字列のローカライズは別途必要)。
- **TEXT SIZE**: `MiniFontImpl`の拡大率(1-4、`MiniFontImpl::setScale()`)。
  SYSTEM FONTがCJKフォント選択時は見た目に影響しない(MiniFont用の設定のため)。
- **BATTERY**: 読み取り専用、残量%と電圧。SDカードの空き容量表示は
  `SDCardManager`(open-x4-sdk、ローカルクローンの外部依存)がボリューム
  空き容量を取得するAPIを公開しておらず、外部SDKを改変したくなかったため
  今回は見送った。
- **CLEAR CACHE**: 確認オーバーレイ(TxtReaderScreenの閉じる確認と同じ
  パターン)を経て`/.reader_cache/`内のファイルを削除する(ページインデックス
  キャッシュ・進捗・最後に開いた本の記録がすべてリセットされる)。

**フォント変更時のレイアウト再計算**: フォントが変わると行の高さ
(`Font::lineHeight()`)も変わるため、行の高さに依存するレイアウトを持つ
`FolderScreen`・`SettingsScreen`・`HistoryScreen`自身は`relayout()`で再計算する。
`HomeScreen`・`TxtReaderScreen`のチローム部分は固定pxレイアウトで
フォントに依存しないため対象外(本文レイアウトは`TxtReaderScreen`が
既に`setContentFont()`で個別に処理している)。`main.cpp`の
`applySystemFontSettings()`がこの一連の処理と、変更後の`FULL_REFRESH`を
まとめて行う。

**既知の制限**: `/System/fonts`ディレクトリのスキャンは設定画面を開くたびではなく
起動時(`setup()`)に一度だけ行うため、起動後にSDカードへ`.cpfont`を追加しても
再起動するまで選択肢に現れない。

### ボタン割り当てについて(リスト画面共通)

`FolderScreen`・`SettingsScreen`・`HistoryScreen`のようなリストベースの画面は、
**LEFT/RIGHT・UP/DOWNのどちらでも(冗長に)同じ意味でリスト内のフォーカス移動が
できる**(`TxtReaderScreen`のLEFT/UP=前ページ・RIGHT/DOWN=次ページと同じ考え方)。
値の変更・決定は必ずCONFIRM経由に統一している。

この配置に至るまでに3段階の変更があった: 当初はUP/DOWN=フォーカス移動・
LEFT/RIGHT=値変更だったが、「リスト移動のつもりでLEFT/RIGHTを押すと設定値が
変わってしまう」というフィードバックを受けてLEFT/RIGHT=フォーカス移動・
UP/DOWN=値変更に入れ替え、さらに「UP/DOWNも(値変更ではなく)フォーカス移動に
してほしい」というフィードバックを受けて値の変更・決定をすべてCONFIRMに一本化した。
`FolderScreen`はこの2回目の変更時にUP(=ディレクトリに入る)/DOWN(=親ディレクトリへ)
という独自の意味を割り当てたが、これがUP長押しのCONFIRMショートカット(後述)と
意味的に重複し「側面ボタンを押すと選択になってしまう」という3度目のフィードバックを
招いたため、最終的に他画面と同じLEFT/RIGHT・UP/DOWN完全redundant化+CONFIRM/BACKへの
一本化に揃えた。

具体的には:

- `FolderScreen`: LEFT/RIGHT・UP/DOWNどちらでもフォーカス移動。CONFIRMで選択中の
  フォルダに入る/ファイルを開く。BACKはルートでなければ親ディレクトリへ1段戻り、
  ルートならホーム画面へ戻る(フッターの表示も"UP"/"HOME"で切り替わる)
- `SettingsScreen`: LEFT/RIGHT・UP/DOWNどちらでも行フォーカス移動。CONFIRMで
  項目ごとの操作(トグル切り替え・別ウィンドウを開く・TEXT SIZEは押すたびに
  1→2→3→4→1…と循環)を行う。時刻編集(LEFT/RIGHT=年/月/日/時/分のフィールド
  選択、UP/DOWN=増減)やタイムゾーン編集のような「別ウィンドウ」の中は
  元々この配置に近かったため変更していない
- `HistoryScreen`: LEFT/RIGHT=フォーカス移動、CONFIRM=選択中の本を開く

`TxtReaderScreen`(LEFT/UP=前ページ、RIGHT/DOWN=次ページ)と`HomeScreen`
(2x2グリッドの縦横移動)は元からこのパターンに沿っているため対象外。

なお上記のUP/DOWN長押しショートカット(UP長押し=CONFIRM、DOWN長押し=BACK、
詳細は「物理ボタンの配置」参照)は`main.cpp`側で一律に変換してから各画面の
`handleButton()`に渡す仕組みのため、画面側の実装(このセクションの内容)は
「短押しでどう動くか」だけを考えればよく、長押しのことは意識しなくてよい。

### 履歴画面について

ホーム画面でBACKを押すと開く、最近開いた本の一覧画面(`screens/HistoryScreen.h`)。
`core/TxtReaderService.h`に`readHistory()`/`updateHistory()`を追加し、本を開く・
ページをめくるたび(=`saveProgress()`のたび)に`/.reader_cache/history.log`へ
「パス+進捗%」を1行ずつ、最近使った順(MRU)で保存する。同じ本を再度開くと
既存エントリを先頭に付け直す(重複させない)。最大`TxtReaderService::kMaxHistoryEntries`
件(10件)を超えると古いものから切り捨てる。

`last_book.txt`(ホーム画面の「続きから」表示用、常に1件だけ)とは別ファイル・
別データで、履歴は複数件を保持する点が異なる。ページングはせず、上限件数
(10件)がそのまま画面に収まる行数として扱われる。

### 電源安定化(USB抜き差し対策)

USB(給電+シリアル)の抜き差しは電源経路の切り替えを伴い、瞬間的に電圧が
不安定になる。`main.cpp`は`Serial`の接続状態(`static_cast<bool>(Serial)`)の
変化を検知し、変化してから3秒間はE-ink描画(`renderAndRefresh`)を止める
(`safeRenderAndRefresh`)。不安定期間中にボタン操作等で描画が抑制された場合、
期間が明けたタイミングで最新状態を1回だけ強制的に描画する。この対策だけでは
フリーズは解消しなかった(下記GPIO13の注意点が根本原因だった)が、電源切替の
瞬間にSPI通信を行うリスクを減らす保険として残している。

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
  側でpxのハードコードによるレイアウトをしない。実装は`MiniFontImpl`(ASCII専用
  5x7固定幅)と`CjkFontImpl`(SDカードの.cpfont、可変幅CJK対応)の2種類があり、
  設定画面の「SYSTEM FONT」でランタイム切り替えできる(詳細は「CJKフォント
  について」「設定画面について」を参照)
- **SdFatのLFN(長いファイル名)はデフォルトで非ASCII文字が`?`になる**:
  `SdFatConfig.h`の`USE_UTF8_LONG_NAMES`はデフォルト0で、この場合日本語などの
  ファイル/フォルダ名が`FsFile::getName()`の時点で文字化けではなく全て`?`に
  置き換わってしまう(フォント描画側の豆腐グリフとは別問題)。`platformio.ini`に
  `-DUSE_UTF8_LONG_NAMES=1`をビルドフラグとして追加し、SdFatConfig.hのコメントに
  ある通りの方法で上書きして解消した
- **SDカード初期化とSPIバスの落とし穴**: `EInkDisplay::begin()`が内部で
  `SPI.begin(sclk, -1, mosi, cs)`とMISOピン抜きでSPIバスを初期化する。ESP32の
  `SPIClass::begin()`は「既にバスが初期化済みなら何もせずtrueを返すだけ」の
  実装のため、後からMISOを指定して`SPI.begin()`を呼び直しても無視される。
  SDカード(`FileBrowserService::begin()`)を初期化する前に必ず`SPI.end()`で
  切断してから`SPI.begin(sclk, miso, mosi, -1)`で再初期化すること
- **GPIO13はバッテリー電源ラッチ、SDカードとは無関係**: 当初「SDカード電源制御」と
  誤解し`OUTPUT_OPEN_DRAIN`で駆動していたが、実際にはバッテリーからの給電を
  自己保持する電源ラッチMOSFETのゲート制御ピンだった([crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)の
  `HalPowerManager.cpp`のコメントで判明、コード自体は移植していない)。USB給電中は
  USB自体がVCCを供給するためGPIO13の状態に関わらず動作するが、USBを抜いて
  バッテリー単独駆動に切り替わった瞬間、`OUTPUT_OPEN_DRAIN`(ハイインピーダンス)
  では確実にHIGHを保持できずラッチが不安定になり、電源が落ちてしまう不具合が
  あった(症状: 画面がモザイク状のまま操作不能になり、リセットしても復帰しない。
  USB再接続でのみ回復)。`main.cpp`の`setup()`冒頭、他の何よりも先に
  `pinMode(13, OUTPUT); digitalWrite(13, HIGH);`で確実にpush-pull駆動すること
- フォルダ画面のファイル/フォルダ一覧には`SDCardManager::listFiles()`ではなく
  `FileBrowserService`(自作ラッパー、`core/FileBrowserService.h`)を使う。
  `listFiles()`はディレクトリを除外してしまうため、ファイラー用途には使えない
