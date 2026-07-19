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

**UP/DOWNは純粋な移動キー(以前は長押しでCONFIRM/BACKショートカットだった)**:
側面のUP/DOWNは以前、短押しではその画面本来の意味(フォーカス移動等)のまま、
`kLongPressMs`(500ms)以上の長押しではCONFIRM/BACKの代わりとして働いていた
(UP長押し=CONFIRM、DOWN長押し=BACK。底面のCONFIRM/BACKまで持ち替えなくても
片手の側面ボタンだけで一通り操作できるようにする目的で追加した機能)。

しかし「フォルダ探索でサイドボタンでの連打が遅い」というフィードバックを受け、
下記「連打対応」の押しっぱなし連続ナビをUP/DOWNにも拡張することにした際、
同じ「長押し」という操作を連続ナビとCONFIRM/BACKショートカットが取り合って
しまう(特にDOWNは「連続スクロールしながら長押しを続けると、離した瞬間に
BACKでフォルダごと抜けてしまう」という分かりにくい組み合わせになる)ことが
判明し、UP/DOWNは純粋な移動キーに戻し、このショートカット自体を廃止した。
`appSettings.longPressMs`自体の設定項目(設定画面「LONG PRESS」)は残っており、
引き続き読書画面のLEFT(短押し=ブックマーク追加、長押し=一覧表示、後述
「TXT読書画面について」参照)の判定に使われている。

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

以前はUP/DOWNの長押し(UP=CONFIRM、DOWN=BACK)を`main.cpp`側で一律に変換して
各画面の`handleButton()`に渡す仕組みがあったが、後述「連打対応」の理由により
廃止した(詳細は「物理ボタンの配置」参照)。現在UP/DOWNは常にこのセクションの
「短押しでどう動くか」の意味のまま各画面へ届く(長押しによる意味の変化はない)。

**連打時のフォーカス移動が取りこぼされる不具合の対策**: E-inkの部分更新
(`FAST_REFRESH`)は数百msブロッキングし、その間`InputManager::update()`が
呼ばれない(SPIバスを占有しているためボタンを読みに行けない)。以前はフォーカスが
1つ動くたびに毎回同期的に`FAST_REFRESH`していたため、その数百msの間に連打すると
大半のタップが「押されて離されたことがまるごと見えない」状態になり取りこぼされ、
フォルダの探索で連打しても思った通りに進まない不具合があった。各画面の
`handleButton()`はフォーカス位置の更新自体を描画とは独立に即座に行っているため、
`main.cpp`側で実際のE-ink書き込みだけを短時間(`kListRedrawDebounceMs`=60ms、
連打が続いても`kListRedrawMaxWaitMs`=220msで上限)デバウンスし、無操作期間が
空いてから最新の状態を1回だけ描画するように変更した。これにより`loop()`自体は
ブロックされたままにならず、連打の全タップを取りこぼさずモデルに反映できる
(CrossPointJPの操作感を参考にした)。画面遷移相当の`FULL_REFRESH`は元々頻度が
低く連打の対象にならないため対象外(同期的なまま)。

**押しっぱなし連続ナビゲーションを全画面・4方向ボタンへ拡張**: 上記のデバウンス
対策とは別に、ボタンを押しっぱなしにした場合に一定間隔(`kContinuousNavIntervalMs`
=100ms、`kContinuousNavStartMs`=400ms以上の保持で開始)で自動的にフォーカスが
進み続ける「連続ナビ」の仕組みが以前から存在したが、`FolderScreen`のLEFT/RIGHT
限定だった。「もっと軽い操作感にしてほしい・サイドボタン(UP/DOWN)での連打が
遅い」というフィードバックを受け、全画面・LEFT/RIGHT/UP/DOWNの4方向ボタンに
拡張した。これに伴いUP/DOWNの長押しCONFIRM/BACKショートカット(前述「物理
ボタンの配置」参照)を廃止し純粋な移動キーへ変更した(同じ「長押し」という
操作を連続ナビと取り合ってしまうため)。TxtReaderScreen読書中(オーバーレイ
非表示)のLEFTのみ、ブックマーク短押し/長押し機能と衝突するため連続ナビの対象
から除外している(`main.cpp`の`leftReservedForBookmark`参照)。各画面の
`handleButton()`が方向ボタンに対して`kRedraw`/`kNone`以外を返さないことを
確認した上で、`currentScreen()`経由で汎用的に呼ぶようにしている(画面遷移系の
アクションが方向ボタンから誤発火する心配がない)。

**単押しで2回移動してしまう不具合**: 上記の拡張直後、方向ボタンをどれも単発で
軽くタップしただけで必ず2段分フォーカスが移動する不具合が実機で見つかった。
原因は、単押し1回に対して`handleButton()`が呼ばれうる箇所が2つあり
(a. ボタン処理forループの`wasPressed()`検出時、b. `pendingListRedraw`の
デバウンス描画時)、このうちb.の`safeRenderAndRefresh(FAST_REFRESH)`呼び出しの
直後にも連続ナビ判定ブロックが同じloop()周回内で続けて実行されること。
`FAST_REFRESH`は数百msブロッキングし、その間`input.update()`が呼ばれないため、
ブロッキング直後の連続ナビ判定は「まだ押されている」という古いスナップ
ショットのまま`getHeldTime()`だけが(実際に経過したブロッキング時間分)大きな
値になり、`kContinuousNavStartMs`(400ms)の条件を実際の押下時間とは無関係に
満たしてしまい、連続ナビ側の`handleButton()`が2回目呼ばれていた。

最初は「ブロッキング直後の1周期だけ判定を見送る」フラグ(`skipContinuousNavCheckOnce`)
をb.のデバウンス描画にも追加する対策を試みたが、これは**2つの理由で不十分**
だった: (1) このフラグを立てる行が、同じ周回内で判定に使われた直後に無条件で
上書きされる場所にあり、次の周回まで持ち越せていなかった。(2) そもそも
`InputManager`側のデバウンス(`DEBOUNCE_DELAY`=5ms)は「状態が変化した」と
検知したupdate()呼び出しそのものでは確定させず、次のupdate()呼び出しで確定
させる作りのため、ブロッキング明け1周期だけでは実際の状態確定に間に合わない
(loop()の`delay(10)`を挟むため、確定までブロッキング明けから最短でも約20ms
かかる)。

最終的に、個別の呼び出し箇所ごとにフラグを立てる方式をやめ、`renderAndRefresh()`
が実際にブロッキング描画を行うたびに`lastBlockingRefreshMs`(グローバルな
タイムスタンプ)を更新するよう一本化し、そこから`kPostBlockSettleMs`(=40ms、
デバウンス確定に必要な最短時間に余裕を持たせた値)が経過するまでは、どの
ブロッキング描画がきっかけであっても連続ナビの判定自体を一律で見送るように
変更した。個別箇所ごとの対策漏れが起きようがない、より頑健な仕組みにした。

**選択行の強調表示をグレーに変更**: `SettingRow`(フォルダ画面・履歴画面・設定
画面・読書中メニュー・待機画面の一覧で共通利用)の選択中の見た目は、以前は
背景を丸ごと反転する黒背景+白文字だったが、選択の主張が強すぎる・切り替えの
たびに大きな黒矩形が反転して見た目がちらつく、という理由でグレー相当の表示に
変更した。パネルは1bpp(白黒2値)で真のグレー階調は表現できないため、
`FrameBufferOps::fillRectLightGrayDither()`が4px中1pxだけ黒にする薄いドット
パターンで背景を敷き、その上に(反転させず)通常通り黒文字を描画することで
疑似的なグレー選択を実現している(`SettingRow::render()`参照。文字より先に
背景を敷くのは、各フォント実装の`drawText()`が「文字の黒画素だけを描く透過
描画」のため、順序を守れば文字が欠けたり汚れたりしないため)。

当初`SettingRow`のみ変更していたが、「全体に実装してほしい」というフィードバックを
受け、黒背景反転が残っていた`ui/HomeGridButton.h`(ホーム画面の2x2グリッド選択)と
`SettingsScreen::drawClockEdit()`(時刻編集の選択中フィールド)にも同じ手法
(`FrameBufferOps::fillRectLightGrayDither()`を文字より先に描く)を適用し、
アプリ全体の選択表示をグレーに統一した。使われなくなった`FrameBufferOps::
invertRect()`/`togglePixel()`は削除済み。

**一覧画面の行を拡大**: 「リストの視認性を上げてほしい」というフィードバックを
受け、`FolderScreen`/`HistoryScreen`/`SettingsScreen`(メイン一覧)/`StandbyScreen`の
一覧行を拡大した。行の余白(`kRowPadding`)を10→30に、行内アイコン(`SettingRow`が
`hasIcon_`時に描く`IconId`)を`IconSize::kSmall`(24px)→`IconSize::kLarge`(40px、
`SettingRow::kIconPx`として公開)にそれぞれ変更している。行の高さは
`std::max(font.lineHeight(), SettingRow::kIconPx) + kRowPadding`で計算し、
TEXT SIZE設定でフォントを小さくしてもアイコンが行からはみ出さないようにした
(`FolderScreen::RowHeight()`参照。他の画面も同じ考え方)。`TxtReaderScreen`の
「READING SETTINGS」メニュー・ブックマーク一覧はモーダルなオーバーレイ(画面
中央の小さな枠内)のため対象外にしている(行数が多い場合に枠が画面をはみ出す
懸念があるため、対象は常時全画面表示される一覧画面のみに絞った)。

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

**待機画面(写真表示中)で不用意に上書きされていた不具合**: 上記の「期間が明けたら
1回だけ強制的に描画する」処理(`pendingRedrawAfterSettle`)は`renderAndRefresh()`
(`display.clearScreen()`→`render()`→`displayBuffer()`)を無条件に呼んでいたが、
`StandbyScreen::render()`は写真表示中、初回描画後は何もせずreturnするだけの
実装のため、クリアされた空のフレームバッファがそのままパネルに送られ、表示中の
写真が(ベースの白黒プレーンだけ白紙化される一方、先に書き込み済みのグレー
スケールプレーンは残るため)全体的に白っぽく明るくなって見える不具合があった。
バッテリー残量ポーリング側は同じ理由で以前から`isShowingImage()`を見て
スキップしていたが、こちらの分岐には同じガードが入っていなかった。写真表示中は
この保留描画自体をスキップするよう修正した。ただし、この不具合はUSB接続状態の
変化(電源不安定判定)が写真表示中に検知された場合にのみ発生する狭いケースで、
実際に実機で報告された「表示から約5秒ほどで画像全体が明るくなる」症状(USB接続の
有無に関わらず、暗所で長時間放置していても再現)の説明にはならなかった。
真因は下記「待機画面について」参照。

### 待機画面について

写真を選んで表示すると、約5秒ほどで画像全体が白っぽく明るくなる(退色する)
不具合があった。原因の切り分けに3段階かかったので、経緯も含めて記録する。

**第1段階(不十分だった対策)**: `StandbyScreen::showImageGrayscale()`が
グレースケール2パス書き込みの最終ステップで`EInkDisplay::displayGrayBuffer()`を
`turnOffScreen`引数なし(=`false`)で呼んでいた点を修正し、`turnOffScreen=true`
(パネルのアナログ電源レールを明示的に遮断する`0x02`コマンド)を渡すようにした。
`EInkDisplay/README.md`が「プログラム終了前に必ずパネルを電源オフして画像を
lock inすること」と明記していたための対策だったが、実機のシリアルログ
(`pio device monitor`で採取)で検証したところ、`X3_CMD02_POWEROFF`が確実に
送信されている(=電源は正しく遮断されている)にも関わらず症状が再現し、
かつ画像表示から次の操作までの数十秒間、E-ink関連のコマンドが一切送られて
いない(=ソフトウェア側の再描画は起きていない)ことも確認できた。つまり
この対策は妥当ではあるものの、退色の直接原因ではなかった。

**第2段階(量子化閾値の調整、これも不十分だった)**: 実機で「コントラストの
強い(黒・白がほとんどで中間調が少ない)写真では症状が起きない」ことが確認され、
4階調(黒/暗いグレー/明るいグレー/白)のうち「暗いグレー」「明るいグレー」の
2階調だけがLSB/MSBプレーン経由の専用グレーLUT波形で駆動されること
(純粋な黒・白は通常の白黒2値駆動のまま)が疑わしいと判断した。そこで量子化
閾値(`kBlackMax`/`kWhiteMin`)を黒白寄りに調整し、グレー画素の"量"を減らす
対策を試みたが、閾値をかなり黒白寄りにしても(グレーの幅を256階調中23段=約9%
まで狭めても)症状は再現し続けた。

**第3段階(実際の原因)**: グレー画素の"量"ではなく、**グレー用LUT波形を
1回でも使うこと自体**が原因だと判明した。E-inkのLUT波形はフレーム単位で
選択されるため、グレー画素が少数でもあれば、そのフレーム全体が(黒・白と
分類された画素も含めて)グレー用の波形で駆動されてしまう。これを検証する
ため、量子化閾値を`kBlackMax=127`/`kWhiteMin=128`(グレーの範囲をゼロにする、
=グレー用LUT波形を完全に使わなくなる設定)にしたところ、退色が完全に消える
ことを実機で確認した。

**対策**: 待機画面の写真表示は、`EInkDisplay::copyGrayscaleLsbBuffers()`/
`copyGrayscaleMsbBuffers()`/`displayGrayBuffer()`(グレー専用LUT波形)を
一切使わず、常に通常の白黒2値駆動(パネルが最も安定する状態)だけで描画する
ように変更した(`StandbyScreen::showImage()`、旧`showImageGrayscale()`から
改名)。4階調の量子化(`quantizeLevel()`)自体は残しており、レベル1・2は
画面上ではどちらも黒として描画されるが、誤差拡散の計算上は異なる代表輝度
として扱われるため、黒白2値の中でも中間調の網点密度に影響する(退色とは
無関係な、単なる仕上がりの好み)。`kBlackMax`/`kWhiteMin`を直接書き換えることで
好みの見え方を調整できる。

**第4段階(本当の真因、ソフトウェアのレースコンディション)**: 上記1〜3段階の
対策後も「約5秒ほどで画像全体が明るくなる」症状の報告が続いた。第三者の解析
(Antigravity)により、`main.cpp`の連打対策(`pendingListRedraw`)に潜んでいた
レースコンディションが本当の原因だと判明した:

1. 一覧画面でLEFT/RIGHT/UP/DOWNを押すと、連打時のタップ取りこぼし対策として
   `pendingListRedraw = true`が立つ(`main.cpp`のkRedraw分岐の`else`節参照)。
2. デバウンス時間(60ms)が経過する前に素早くCONFIRMを押して写真を選ぶと、
   `StandbyScreen::handleButton()`のCONFIRM処理も`ScreenAction::kRedraw`を返す。
   `action != kRedraw && action != kNone`の場合のみ`pendingListRedraw`を
   リセットする仕組み(不要なFAST_REFRESHの重複実行を防ぐための処理)のため、
   `kRedraw`を返すこのCONFIRM処理では`pendingListRedraw`がリセットされず、
   trueのまま持ち越されてしまう。
3. `standbyScreen.showImage()`(LOADING表示→デコード→本番表示→電源オフ、
   数百ms〜数秒かかる)が完了しloop()に戻ってきた時点で、`pendingListRedraw`の
   デバウンス条件(`quiet`または`waitedTooLong`)は経過時間からして無条件に
   満たされてしまう。
4. `pendingListRedraw`実行ブロックには、バッテリーチェック等の他の箇所には
   実装されていた「`standbyScreen.isShowingImage()`ならスキップする」という
   ガードが漏れていたため、`safeRenderAndRefresh(FAST_REFRESH)`が発火する。
5. `StandbyScreen::render()`は画像表示中、初回描画後は何もせずreturnするだけの
   実装のため、`display.clearScreen()`で白紙化されたフレームバッファがそのまま
   `display.displayBuffer(FAST_REFRESH)`(`turnOffScreen`省略=`false`)に渡され、
   写真が白紙で上書きされた上に、せっかくオフにしたパネルの電源も再びオンの
   まま放置される。

つまり第1〜3段階で修正・特定した「アナログ電源を切り忘れる」「グレーLUT波形の
物理的な緩和」という現象自体は正しかったが、それらの対策(電源オフ)を無効化
してしまう別のソフトウェアのバグが並行して存在していたため、症状が消えなかった。
`pendingListRedraw`実行ブロックに`standbyShowingImage`ガードを追加して解消した
(他の箇所と同じパターン)。

### バッテリー%・日付表示、ヘッダー(StatusBar)廃止、RTCオン/オフ設定

上記の待機画面の不具合対応が一段落した後、「小さい画面でヘッダー・フッターが
窮屈」というフィードバックを受け、以下3点をまとめて実装した(実装は
[Google Antigravity](https://antigravity.google/)に一度委譲し、Claudeが結果を
検証・一部修正した)。

**バッテリー%・日付の共通描画ヘルパー**: `ui/BatteryDateOverlay.h/.cpp`に
`drawBatteryAndDate()`を新設した。バッテリーは数字のみ(例: "87%")、充電中は
`IconId::kBatteryCharging`(既存の電池+雷アイコン、新規アセット生成は不要)を
数字の左に追加する。日付は`YYYY.MM.DD`形式で、`RtcDateTime*`が`nullptr`
(RTC無効時)なら省略する。`drawBackgroundBox=true`で、写真等の上に重ねても
読めるよう白背景の箱を敷いてから描く。`rightAlign`引数で右寄せにも対応する。

- **待機画面**: `StandbyScreen::showImage()`が画像デコード直後・パネル書き込み
  直前に、写真の隅へこのオーバーレイを描き込む(`BatteryService`/`RtcService`への
  参照をコンストラクタで新たに受け取るようにした)。
- **ホーム画面**: `HomeScreen`にも同様に`BatteryService`/`RtcService`への参照を
  追加し、`render()`のたびに右上へ`rightAlign=true`で描画する(旧
  `setBatteryPercent()`/`setClockText()`のような「main.cpp側が定期ポーリングして
  値をpushする」方式ではなく、`render()`内で毎回`battery_.readPercent()`/
  `rtc_.readDateTime()`を直接呼ぶ方式に変わっている。ホーム画面の再描画頻度は
  他画面より低いため実用上問題はないが、redraw毎にI2Cアクセスが発生する点は
  設計上の既知のトレードオフとして記録しておく)。

**`StatusBar`ウィジェットの完全廃止**: 全9画面(Home/Folder/History/Settings/
TxtReader/Standby/Bluetooth/FolderSync/LiveText)から`StatusBar`メンバー・
`render()`呼び出し・`setBatteryPercent`/`setBatteryCharging`転送メソッドを削除し、
`ui/StatusBar.h/.cpp`自体も削除した(どの画面からも参照されなくなったため)。
各画面の`kStatusBarHeight`オフセットもレイアウト計算(行の開始y座標・
`contentTop_`等)から取り除き、ヘッダー分の領域をコンテンツに還元した
(`TxtReaderScreen`/`LiveTextScreen`は`viewportHeightPx_`が増え、1ページの
表示行数が増える)。ヘッダーが表示していた情報(パンくずパス・画面タイトル・
`showOpenFailedMessage()`のヒープ不足通知等)は移設せず削除した(シリアルログへの
出力は維持)。

**RTCオン/オフ設定**: `AppSettings::showClockInStatusBar`(bool、構造体内の
同じ位置・同じ型)を`rtcEnabled`に改名・意味変更した。バイトオフセットが
変わらないため既存の`.settings.bin`との互換性は保たれる(値が意味的に
「ステータスバーに時計を出すか」→「RTCを使うか」に変わるだけで、ONだった
ユーザーはアップデート後も自然にRTC有効として引き継がれる)。`main.cpp`の
`rtc.begin()`呼び出しを`if (appSettings.rtcEnabled)`で囲み、OFF時はRTCへの
I2Cアクセス自体が発生しないようにした(`RtcService::readDateTime()`等は
`available_`フラグで内部ガードされているため、`begin()`を呼ばないだけで
以降のアクセスも自動的に発生しなくなる)。`SettingsScreen`の既存トグル行
(旧「CLOCK IN STATUS BAR」)を「RTC ON/OFF」に改名して転用した。設定画面で
ONに切り替えた瞬間、再起動を待たずその場で`rtc_.begin()`を呼んで即座に
時刻表示へ反映されるようにしている。

**待機画面のバッテリー表示漏れの修正**: 上記の実装直後、`StandbyScreen`の
`showImage()`が`BatteryService`/`RtcService`への参照を保持するだけで、
`BatteryDateOverlay::drawBatteryAndDate()`の呼び出し自体が実装されておらず
写真表示中にバッテリー・日付が一切表示されない不具合があった。
`StandbyScreen::drawOverlays()`を新設し、`showImage()`と`render()`の
フォールバック分岐の両方から、画像描画直後・パネル書き込み直前に呼ぶよう
修正した(写真の左下に白背景付きで表示)。あわせて、写真ファイル名(拡張子を
除いたベースネーム)を右上に角丸の白背景付きで表示する機能も追加した
(`FrameBufferOps::fillRoundRect()`新設)。

**タイムゾーンの適用漏れの修正**: `HomeScreen`/`StandbyScreen`のオーバーレイは
当初RTCの生値(UTC相当)をそのまま表示しており、`RtcService::
addHoursToDateTime()`による`appSettings.timezoneOffsetHours`の適用が
漏れていた。設定画面の時刻表示・編集と同じくローカル時刻に変換してから
表示するよう修正した。

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
- **NimBLEの初期化はヒープを恒常的に(数十KB規模)消費するため遅延初期化にしている**:
  以前は`BleTransferService::begin()`が`setup()`内で`NimBLEDevice::init()`を無条件に
  呼んでいたため、Bluetooth/FolderSync/LiveTextのいずれの画面も一度も開かない
  セッションでもBLEスタック分のヒープが常に確保されたままになり、TXT/Markdown
  読書画面がヒープ逼迫で開けない(`TxtReaderService::open()`が`std::bad_alloc`を
  捕まえて`false`を返す、開いても何も起きないように見える不具合)の主因になっていた。
  `begin()`はデバイス名の計算のみを行うよう変更し、実際のGATTサーバー構築・
  `NimBLEDevice::init()`は`BleTransferService::ensureStackReady()`に切り出して
  `startAdvertising()`の初回呼び出し時(=実際にBluetooth系の画面を開いた時点)まで
  遅延させた。あわせて、`main.cpp`の`kOpenFile`失敗時(ファイルを開けなかった場合)に
  何も起きなかった箇所へ、`FolderScreen`/`HistoryScreen`のステータスバーへの
  一時的なエラーメッセージ表示とシリアルログ(開こうとしたパス+その時点の空きヒープ)を
  追加した
