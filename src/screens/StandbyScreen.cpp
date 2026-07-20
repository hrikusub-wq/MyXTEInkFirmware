#include "StandbyScreen.h"

#include <InputManager.h>
#include <JPEGDEC.h>
#include <SDCardManager.h>
#include <esp_system.h>
#include "../gfx/Wallpaper.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../gfx/FrameBufferOps.h"
#include "../ui/BatteryDateOverlay.h"

namespace {

bool hasJpegExtension(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

// JPEGDECのコールバックはC関数ポインタ(非メンバ)である必要があるため、デコード
// 対象のファイル・出力先フレームバッファをここに一時的に保持する
// (decodeAndDrawImage()の呼び出し中だけ有効、他の状態と共有しない)。
FsFile g_jpegFile;
uint8_t* g_targetFb = nullptr;
uint16_t g_targetFbWidth = 0;
uint16_t g_targetFbHeight = 0;
int g_offsetX = 0;
int g_offsetY = 0;
// JPEGDEC組み込みの粗い縮小(1/2・1/4・1/8のみ)だけでは画面ぴったりのサイズに
// できないため、デコード後の座標にこの倍率(<=1.0)をさらに掛けて画面に収まる
// 最大サイズへ微調整する(decodeAndDrawImage()参照)。
float g_extraScale = 1.0f;

// Floyd-Steinberg誤差拡散用のバンドバッファ(decodeAndDrawImage()がmalloc()で
// 確保し、デコード完了後にfree()する)。JPEGDECは1つのMCU行バンド(Y0〜Y0+h-1、
// hはMCU高さ、通常8か16)を複数回のコールバック(X範囲違い)に分けて配信するため、
// 「行ごとの誤差」をバンド内の各行につき1本、バンドの高さ分だけ持つ必要がある
// (単純にcur/nextの2本だけだと、同じバンド内の別X範囲のコールバックが来た時点で
// 前の行の誤差を上書きしてしまい、結局ブロック単位でリセットされたのと同じ不具合
// になる)。バンドが変わったら、直前バンドの最終「出力」行(次バンドへの下方向拡散分)を
// 新バンドの0行目の初期値として引き継ぐことで、バンドの境目でも誤差が途切れない
// ようにしている(jpegDrawCb参照)。
//
// 【出力(スケール後)座標で誤差拡散する】以前の実装はこのバッファをソース(デコード後、
// スケール前)の列でインデックスしていたため、g_extraScale<1で複数のソース画素が
// 同じ出力画素へ縮小される場合、各ソース画素が個別に誤差拡散・描画され、最後に
// 処理した画素の結果だけが残る(前の画素の描画は上書きされて消える)という不具合が
// あった。jpegDrawCb()では、同じ出力画素にマップされる隣接ソース画素(最大2x2)を
// 先に平均化してから誤差拡散に渡すことで、この上書きを防ぎ、間引きではなく
// 平均化ダウンスケールになるようにしている。そのためg_bandErrorWidthは
// デコード後の幅ではなく出力(スケール後)の幅+2で確保する(出力の方が小さいので
// メモリ削減にもなる)。
//
// バンド境界・コールバック(X範囲)境界をまたぐ2x2平均化はしない(隣接ソース画素が
// 別のコールバック呼び出しで配信されるため、そのコールバックの実行時点では
// まだ手元に無い)。MCUの継ぎ目(8〜16行/列おき)にあるごく一部の画素だけが
// 1x1(平均化なし)にフォールバックする、既知の軽微な制限。
constexpr int kMaxBandRows = 17;
int16_t* g_bandError = nullptr;   // [kMaxBandRows+1][g_bandErrorWidth]、行優先(出力座標系)
int g_bandErrorWidth = 0;         // 出力(スケール後)の画像幅+2(オフセット込み)
int g_bandY0 = -1;                // このバンドの先頭ソースY(-1=未初期化)
int g_bandPrevOutRows = 0;        // 直前バンドで実際に使った出力行数(バンド切り替え時の引き継ぎに使う)

void* jpegOpenCb(const char* filename, int32_t* pFileSize) {
  g_jpegFile = SdMan.open(filename, O_RDONLY);
  if (!g_jpegFile) return nullptr;
  *pFileSize = static_cast<int32_t>(g_jpegFile.size());
  return &g_jpegFile;
}

void jpegCloseCb(void* handle) {
  (void)handle;
  if (g_jpegFile) g_jpegFile.close();
}

int32_t jpegReadCb(JPEGFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  FsFile* f = static_cast<FsFile*>(pFile->fHandle);
  if (!f || !*f) return 0;
  return static_cast<int32_t>(f->read(pBuf, iLen));
}

int32_t jpegSeekCb(JPEGFILE* pFile, int32_t iPosition) {
  FsFile* f = static_cast<FsFile*>(pFile->fHandle);
  if (!f || !*f) return 0;
  f->seek(iPosition);
  return iPosition;
}

// RGB565(1ピクセル)から輝度(0-255)を近似計算する(0.299R+0.587G+0.114Bの
// 8bit固定小数点近似)。
uint8_t luminanceFromRgb565(uint16_t px) {
  const uint8_t r5 = (px >> 11) & 0x1F;
  const uint8_t g6 = (px >> 5) & 0x3F;
  const uint8_t b5 = px & 0x1F;
  const uint8_t r8 = static_cast<uint8_t>((r5 * 255) / 31);
  const uint8_t g8 = static_cast<uint8_t>((g6 * 255) / 63);
  const uint8_t b8 = static_cast<uint8_t>((b5 * 255) / 31);
  return static_cast<uint8_t>((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
}

// 輝度に対するガンマ補正テーブル(0=黒はそのまま黒、255=白はそのまま白、
// 中間調だけ持ち上げて明るくする)。E-inkの4階調表示は中間調が実際の写真より
// 暗く沈んで見える(諧調自体が少ないうえ、階調の閾値が単純な線形分割だと
// シャドウ側に偏って見える)傾向があったため、gamma<1のカーブで中間調を
// 明るい側へシフトしてから量子化する。decodeAndDrawImage()が呼び出しごとに
// 一度だけ構築する(pow()を毎画素呼ぶコストを避けるため)。
uint8_t g_gammaLut[256];

void buildGammaLut(float gamma) {
  for (int i = 0; i < 256; i++) {
    const float normalized = static_cast<float>(i) / 255.0f;
    const float corrected = powf(normalized, gamma) * 255.0f + 0.5f;
    g_gammaLut[i] = static_cast<uint8_t>(corrected < 0 ? 0 : (corrected > 255 ? 255 : corrected));
  }
}

// quantizeLevel()が決めたレベル(0〜3、kBlackMax/kWhiteMin宣言部のコメント参照)を
// 白(レベル3)かそれ以外(黒扱い)かの2値としてFrameBufferOps経由で論理座標へ
// 描画する(EInkDisplayのdrawImage()は座標系が不明なため使わない)。
// 以前は4階調グレースケール表示用にLSB/MSBプレーンへもマーカーを書き込んでいたが、
// この待機画面はパネルの4階調グレースケール駆動(専用の中間電圧LUT波形)自体を
// 使わなくなった(quantizeLevel()宣言部のコメント参照)ため、そのマーカー書き込みは
// 不要になり削除した。
inline void drawQuantizedPixel(int level, int fx, int fy) {
  if (level == 3) {
    FrameBufferOps::setWhitePixel(g_targetFb, g_targetFbWidth, g_targetFbHeight, fx, fy);
  } else {
    FrameBufferOps::setBlackPixel(g_targetFb, g_targetFbWidth, g_targetFbHeight, fx, fy);
  }
}

// ソース座標(JPEGDECのデコード後、pDraw->x/y基準)から出力(画面)座標への変換。
// g_extraScale(<=1.0)はJPEGDEC組み込みの粗い縮小(1/2/4/8)では収まらない端数を
// 吸収するための追加スケール(decodeAndDrawImage()参照)。
inline int mapOutX(int srcX) { return g_offsetX + static_cast<int>(srcX * g_extraScale + 0.5f); }
inline int mapOutY(int srcY) { return g_offsetY + static_cast<int>(srcY * g_extraScale + 0.5f); }

// (lx,ly)を左上として、colSpan x rowSpan(最大2x2、同じ出力画素にマップされる
// 隣接ソース画素の範囲、jpegDrawCb参照)のガンマ補正後輝度の平均を返す。
// 間引き(最近傍)ではなく平均化によるダウンスケールにすることで、特に細かい
// 模様・文字を含む写真のジャギー・モアレを減らす。
inline int averagedGammaGray(const JPEGDRAW* pDraw, int lx, int ly, int colSpan, int rowSpan) {
  int sum = 0;
  for (int dy = 0; dy < rowSpan; dy++) {
    for (int dx = 0; dx < colSpan; dx++) {
      const uint16_t px = pDraw->pPixels[(ly + dy) * pDraw->iWidth + (lx + dx)];
      sum += g_gammaLut[luminanceFromRgb565(px)];
    }
  }
  return sum / (colSpan * rowSpan);
}

// Floyd-Steinberg誤差拡散をg_bandError(出力座標系の行バッファ、g_bandY0/
// g_bandPrevOutRowsで管理)を使って画像全体で連続的に行う。JPEGDECは1つのMCU行
// バンドを複数回のコールバック(X範囲違い)に分けて配信するため、バンド内の
// 各出力行につき1本の誤差バッファを持ち、同じバンドの別コールバックが来ても
// 引き継がれるようにしている。以前試した「ブロックごとに誤差をリセットする
// 簡易実装」は明るさの継ぎ目(長方形の区切り)が見え、「Bayer順序ディザ」は
// 継ぎ目は消えたが精細感が落ちる(固定パターンで階調を表現するため)という
// フィードバックがあり、この完全なFloyd-Steinberg実装に置き換えた。
//
// g_bandErrorがnullptr(malloc失敗)の場合は、誤差拡散を諦めて単純な閾値
// 量子化のみを行う(追加メモリを一切使わないため必ず成功する)。階調表現は
// 粗くなるが、実機で確認した「ヒープ逼迫時に写真が全く表示されない(真っ白の
// まま)」不具合よりは、多少粗くても表示されるほうを優先した。
//
// どちらの経路でも、隣接する2つのソース画素(行・列とも最大2つ)が同じ出力画素に
// マップされる場合は先に平均化してから量子化する(averagedGammaGray参照、
// g_bandError宣言部のコメントに詳細)。
// 【グレースケール駆動を廃止した経緯】以前は4階調のうち「暗いグレー」「明るい
// グレー」(レベル1・2)をLSB/MSBプレーン経由の専用グレーLUT波形で駆動していたが、
// 実機検証で、このグレー駆動をパネルに1回でも使うと(画素の量に関わらず、フレーム
// 全体がその波形で駆動されるため)、表示後数秒〜十数秒かけて画像全体が光学的に
// 緩和し明るく見える(退色したように見える)現象が確認された。閾値を黒白寄りに
// 振ってグレー画素の"量"を減らしても症状は変わらず、グレー画素をゼロにして初めて
// (=グレーLUT波形自体を一切使わなくなって初めて)症状が消えることを実機で確認した。
// そのため、このファイルはもう`EInkDisplay::copyGrayscaleLsbBuffers()`/
// `copyGrayscaleMsbBuffers()`/`displayGrayBuffer()`(グレー専用LUT波形)を一切
// 呼ばず、常に通常の白黒2値駆動(パネルが最も安定する状態)だけで描画する
// (`showImage()`参照)。
//
// この量子化自体は4階調のまま残している。レベル1・2は画面上ではどちらも黒として
// 描画される(drawQuantizedPixel()参照)が、誤差拡散の計算上は0/3と異なる代表輝度
// (85/170)として扱われるため、黒白2値の中でも中間調の網点密度が変わり、諧調の
// 見え方に影響する(退色とは無関係な、単なる仕上がりの好み)。kBlackMax/kWhiteMinを
// 直接書き換えて好みの見え方を探ってよい:
//   - kBlackMaxを上げる/kWhiteMinを下げる → 暗部・明部の網点が早めに黒/白へ寄る
//     (よりくっきり・コントラスト重視)
//   - kBlackMaxを下げる/kWhiteMinを上げる → 中間調の網点パターンがより滑らかになる
constexpr int kBlackMax = 30;  // これ以下の輝度は黒(level 0)
constexpr int kWhiteMin  = 226;  // これ以上の輝度は白(level 3)

inline int quantizeLevel(int gray) {
  if (gray <= kBlackMax) return 0;
  if (gray >= kWhiteMin) return 3;
  return (gray <= (kBlackMax + kWhiteMin) / 2) ? 1 : 2;
}

int jpegDrawCb(JPEGDRAW* pDraw) {
  if (!g_targetFb) return 0;

  const int w = (pDraw->iWidthUsed > 0) ? pDraw->iWidthUsed : pDraw->iWidth;
  const int h = pDraw->iHeight;

  if (!g_bandError) {
    for (int ly = 0; ly < h;) {
      const int fy = mapOutY(pDraw->y + ly);
      const int rowSpan = (ly + 1 < h && mapOutY(pDraw->y + ly + 1) == fy) ? 2 : 1;
      for (int lx = 0; lx < w;) {
        const int fx = mapOutX(pDraw->x + lx);
        const int colSpan = (lx + 1 < w && mapOutX(pDraw->x + lx + 1) == fx) ? 2 : 1;
        const int gray = averagedGammaGray(pDraw, lx, ly, colSpan, rowSpan);
        const int level = quantizeLevel(gray);
        drawQuantizedPixel(level, fx, fy);
        lx += colSpan;
      }
      ly += rowSpan;
    }
    return 1;
  }

  if (pDraw->y != g_bandY0) {
    // 新しいMCU行バンドに入った。直前バンドの最終出力行(g_bandPrevOutRows-1行目、
    // 次バンドへの下方向拡散分)を新バンドの0行目の初期値として引き継ぐことで、
    // バンドの境目でも誤差が途切れないようにする(初回はg_bandY0<0なので単純に
    // ゼロ初期化)。
    if (g_bandY0 >= 0 && g_bandPrevOutRows > 0 && g_bandPrevOutRows < kMaxBandRows) {
      memcpy(g_bandError, g_bandError + g_bandPrevOutRows * g_bandErrorWidth, g_bandErrorWidth * sizeof(int16_t));
      memset(g_bandError + g_bandErrorWidth, 0, static_cast<size_t>(kMaxBandRows) * g_bandErrorWidth * sizeof(int16_t));
    } else {
      memset(g_bandError, 0, static_cast<size_t>(kMaxBandRows + 1) * g_bandErrorWidth * sizeof(int16_t));
    }
    g_bandY0 = pDraw->y;
  }

  // bandRowはこのバンド内で「何番目の出力行を処理中か」(g_bandErrorのインデックス)。
  // rowSpan==2で2ソース行が1出力行にまとまる場合があるため、lyとは別に数える。
  // (pDraw->y, ly)だけで決まる決定論的な値なので、同じバンドの別X範囲コールバックが
  // 来ても同じly→bandRowの対応になり、誤差バッファの行がずれることはない。
  int bandRow = 0;
  for (int ly = 0; ly < h;) {
    const int fy = mapOutY(pDraw->y + ly);
    const int rowSpan = (ly + 1 < h && mapOutY(pDraw->y + ly + 1) == fy) ? 2 : 1;

    if (bandRow < kMaxBandRows) {
      int16_t* curRow = g_bandError + bandRow * g_bandErrorWidth;
      int16_t* nextRow = g_bandError + (bandRow + 1) * g_bandErrorWidth;

      for (int lx = 0; lx < w;) {
        const int fx = mapOutX(pDraw->x + lx);
        const int colSpan = (lx + 1 < w && mapOutX(pDraw->x + lx + 1) == fx) ? 2 : 1;
        const int localX = fx - g_offsetX;  // 出力座標系での0始まり列インデックス

        if (localX < -1 || localX + 2 >= g_bandErrorWidth) {
          lx += colSpan;
          continue;  // 安全ガード(通常発生しない)
        }

        const int avgGray = averagedGammaGray(pDraw, lx, ly, colSpan, rowSpan);
        const int gray = avgGray + curRow[localX + 1];
        const int clamped = gray < 0 ? 0 : (gray > 255 ? 255 : gray);

        // 4階調(0=黒・1=暗いグレー・2=明るいグレー・3=白)への量子化(quantizeLevel()、
        // グレー駆動の緩和対策で黒白寄りに閾値を振っている、宣言部のコメント参照)。
        // 誤差拡散の基準となる代表輝度は0/85/170/255(256を3等分)のまま据え置く
        // (閾値と代表輝度がずれるが、FS拡散の目的は「量子化で失われた明るさを周囲に
        // 伝播させる」ことなので、代表輝度はどの分割でも変わらず妥当)。
        const int level = quantizeLevel(clamped);
        const int quantized = (level * 255) / 3;
        const int err = clamped - quantized;

        curRow[localX + 2] += static_cast<int16_t>((err * 7) / 16);   // 右
        nextRow[localX] += static_cast<int16_t>((err * 3) / 16);      // 左下
        nextRow[localX + 1] += static_cast<int16_t>((err * 5) / 16);  // 真下
        nextRow[localX + 2] += static_cast<int16_t>((err * 1) / 16);  // 右下

        drawQuantizedPixel(level, fx, fy);
        lx += colSpan;
      }
    }
    // bandRow >= kMaxBandRowsの場合は安全ガードとしてこの出力行の処理自体を
    // 丸ごとスキップする(通常発生しない)。

    bandRow++;
    ly += rowSpan;
  }
  g_bandPrevOutRows = bandRow;  // 次バンドへの引き継ぎ用に、このバンドで使った出力行数を確定させる
  return 1;  // 非0を返すとデコード続行
}

// jpeg.open()〜decode()〜close()までの一連の処理をまとめたヘルパー。
// gammaPercentはSettingsScreen「PHOTO GAMMA」で設定した値(20〜100、小さいほど
// 明るい、AppSettings::standbyGammaPercent参照)。
// 戻り値はJPEGのオープンに成功したかどうか。
bool decodeAndDrawImage(const String& path, uint16_t fbWidth, uint16_t fbHeight, uint8_t* baseFb,
                        uint8_t gammaPercent) {
  // JPEGDEC(JPEGIMAGE)は内部にデコード用の固定サイズバッファを複数持ち、サイズが
  // 約17KB以上ある。ESP32-C3のデフォルトタスクスタック(約8KB)を超えるため、
  // スタック上のローカル変数にすると即座にスタックオーバーフロー(Guru Meditation
  // Error: Stack protection fault)でクラッシュする(実機で確認済み)。staticに
  // して.bssセクション(スタックではない静的領域)に確保することで回避する。
  static JPEGDEC jpeg;
  if (!jpeg.open(path.c_str(), jpegOpenCb, jpegCloseCb, jpegReadCb, jpegSeekCb, jpegDrawCb)) return false;

  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);

  // 中間調を持ち上げて明るくするガンマ補正(gamma<1)。4階調表示だと写真が
  // 実物より暗く沈んで見える(特に影の部分が過度に黒に寄る)との実機での
  // フィードバックを受けて追加した。実機での見え方に応じてSettingsScreen
  // 「PHOTO GAMMA」で調整できる(値を下げるほど明るくなる)。
  buildGammaLut(static_cast<float>(gammaPercent) / 100.0f);

  // 画面(528x792)にアスペクト比を保ったまま収まる最大サイズ(targetW x targetH)
  // を計算する。画面より小さい画像は拡大しない(idealScale上限1.0)。
  const int nativeW = jpeg.getWidth();
  const int nativeH = jpeg.getHeight();
  float idealScale = 1.0f;
  if (nativeW > static_cast<int>(fbWidth) || nativeH > static_cast<int>(fbHeight)) {
    const float scaleW = static_cast<float>(fbWidth) / static_cast<float>(nativeW);
    const float scaleH = static_cast<float>(fbHeight) / static_cast<float>(nativeH);
    idealScale = (scaleW < scaleH) ? scaleW : scaleH;
  }
  const int targetW = static_cast<int>(nativeW * idealScale + 0.5f);
  const int targetH = static_cast<int>(nativeH * idealScale + 0.5f);

  // JPEGDEC組み込みの縮小デコードは1/2・1/4・1/8の粗い倍率にしか対応していない
  // ため、それだけでは狙った(画面にぴったり収まる)サイズにできず、写真が
  // 必要以上に小さく表示される不具合があった(例: 1/4では大きすぎるが1/8では
  // 小さすぎる、という画像サイズがよくある)。そこで、デコード後の解像度が
  // targetW x targetHを下回らない範囲でできるだけ軽い(荒い)プリスケールを選び、
  // 端数はjpegDrawCb側でg_extraScale(<=1.0)によるさらなる縮小で吸収して
  // targetW x targetHにぴったり合わせる。
  int preScale = 1;
  if (nativeW / 8 >= targetW && nativeH / 8 >= targetH) {
    preScale = 8;
  } else if (nativeW / 4 >= targetW && nativeH / 4 >= targetH) {
    preScale = 4;
  } else if (nativeW / 2 >= targetW && nativeH / 2 >= targetH) {
    preScale = 2;
  }
  const int decW = nativeW / preScale;

  g_extraScale = (decW > 0) ? (static_cast<float>(targetW) / static_cast<float>(decW)) : 1.0f;
  g_offsetX = (static_cast<int>(fbWidth) - targetW) / 2;
  g_offsetY = (static_cast<int>(fbHeight) - targetH) / 2;
  g_targetFb = baseFb;
  g_targetFbWidth = fbWidth;
  g_targetFbHeight = fbHeight;

  // Floyd-Steinberg誤差拡散用のバンドバッファ(jpegDrawCbのコメント参照)。
  // デコード対象1回ごとに確保・解放する(常時確保するとBLEスタック初期化用の
  // ヒープを圧迫し起動時にハングする不具合があった)。確保に失敗
  // した場合(ヒープ逼迫時)でもデコード自体は諦めない。jpegDrawCbがg_bandError
  // ==nullptrを見て、誤差拡散なしの単純量子化にフォールバックする(何も
  // 表示されないよりは階調が粗くても表示されるほうを優先)。
  // 誤差拡散は出力(スケール後)座標系で行う(jpegDrawCb宣言部のコメント参照)ため、
  // デコード後の幅(decW)ではなく出力の幅(targetW)でバッファを確保する
  // (targetW<=decWなので、以前よりメモリも少なく済む)。
  g_bandErrorWidth = targetW + 2;
  g_bandError =
      static_cast<int16_t*>(malloc(static_cast<size_t>(kMaxBandRows + 1) * g_bandErrorWidth * sizeof(int16_t)));
  g_bandY0 = -1;
  g_bandPrevOutRows = 0;
  if (!g_bandError && Serial) {
    Serial.println("[X3FW] standby: dither band buffer alloc failed, falling back to no dithering");
  }

  jpeg.decode(0, 0, preScale == 1 ? 0 : preScale);

  free(g_bandError);
  g_bandError = nullptr;
  g_targetFb = nullptr;
  jpeg.close();
  return true;
}

}  // namespace

StandbyScreen::StandbyScreen(uint16_t fbWidth, uint16_t fbHeight, const Font& font, FileBrowserService& fileBrowser,
                             EInkDisplay& display, AppSettings& appSettings, BatteryService& battery, RtcService& rtc)
    : fileBrowser_(fileBrowser),
      display_(display),
      appSettings_(appSettings),
      font_(&font),
      fbWidth_(fbWidth),
      fbHeight_(fbHeight),
      battery_(battery),
      rtc_(rtc),
      footer_(Rect{0, static_cast<int>(fbHeight) - kFooterHeight, static_cast<int>(fbWidth), kFooterHeight}) {
  footerItems_[0] = {PhysicalButton::kBack, "HOME"};
  footerItems_[1] = {PhysicalButton::kConfirm, "", IconId::kCheck, true};
  footer_.setItems(footerItems_, 2);

  layoutRows(font);
}

void StandbyScreen::layoutRows(const Font& font) {
  const int rowH = font.lineHeight() + kRowPadding;
  for (int i = 0; i < kMaxVisibleRows; i++) {
    rows_[i].setBounds(Rect{0, 16 + i * rowH, static_cast<int>(fbWidth_), rowH});
    rows_[i].setSelectionStyle(SettingRow::SelectionStyle::kGrayHighlight);
  }
}

void StandbyScreen::reloadFileList() {
  jpegFiles_.clear();
  for (const DirEntry& entry : fileBrowser_.listDirectory(kStandbyDir)) {
    if (!entry.isDirectory && hasJpegExtension(entry.name)) {
      jpegFiles_.push_back(entry.name);
    }
  }
  focusIndex_ = 0;

  visibleRowCount_ = static_cast<int>(jpegFiles_.size());
  if (visibleRowCount_ > kMaxVisibleRows) visibleRowCount_ = kMaxVisibleRows;
  for (int i = 0; i < visibleRowCount_; i++) {
    rowLabels_[i] = jpegFiles_[i];
    rows_[i].setLabel(rowLabels_[i].c_str());
    rows_[i].setValue("");
  }
  updateRowSelection();
}

// フォーカス移動時、一覧の再スキャンはせず選択状態の表示だけを更新する
// (kMaxVisibleRows件以内という前提のシンプルな一覧のため、FolderScreenのような
// ページ送り・表示ウィンドウの再構築は不要)。
void StandbyScreen::updateRowSelection() {
  for (int i = 0; i < visibleRowCount_; i++) {
    rows_[i].setSelected(i == focusIndex_);
  }
}

void StandbyScreen::onEnter() {
  mode_ = Mode::kList;
  imageDrawn_ = false;
  sleepRequested_ = false;
  quickMode_ = false;
  reloadFileList();
}

bool StandbyScreen::enterQuickRandom() {
  reloadFileList();
  const int total = static_cast<int>(jpegFiles_.size());
  if (total == 0) return false;

  int idx = static_cast<int>(esp_random() % static_cast<uint32_t>(total));
  if (total > 1 && idx == lastQuickIndex_) {
    idx = (idx + 1) % total;  // 直前と同じ画像が連続で選ばれるのを避ける
  }
  lastQuickIndex_ = idx;
  focusIndex_ = idx;

  mode_ = Mode::kShowingImage;
  imageDrawn_ = false;
  sleepRequested_ = false;
  quickMode_ = true;
  return true;
}

bool StandbyScreen::consumeSleepRequested() {
  const bool v = sleepRequested_;
  sleepRequested_ = false;
  return v;
}

void StandbyScreen::drawOverlays(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) const {
  RtcDateTime dt;
  RtcDateTime localDt;
  const RtcDateTime* pDt = nullptr;
  if (appSettings_.rtcEnabled && rtc_.ready() && rtc_.readDateTime(dt)) {
    localDt = addHoursToDateTime(dt, appSettings_.timezoneOffsetHours);
    pDt = &localDt;
  }
  BatteryDateOverlay::drawBatteryAndDate(
      fb, fbWidth, fbHeight, font,
      16, static_cast<int>(fbHeight) - 40,
      battery_.readPercent(), battery_.isCharging(), pDt,
      true, false);

  if (focusIndex_ >= 0 && focusIndex_ < static_cast<int>(jpegFiles_.size())) {
    const String& filename = jpegFiles_[focusIndex_];
    int dotIndex = filename.lastIndexOf('.');
    String basename = (dotIndex >= 0) ? filename.substring(0, dotIndex) : filename;

    int nameW = font.measureText(basename.c_str());
    int lineH = font.lineHeight();
    int padding = 4;
    int bgX = static_cast<int>(fbWidth) - nameW - padding * 2 - 16;
    int bgY = 16;
    int bgW = nameW + padding * 2;
    int bgH = lineH + padding * 2;

    FrameBufferOps::fillRoundRect(fb, fbWidth, fbHeight, bgX, bgY, bgW, bgH, 8, false);
    font.drawText(fb, fbWidth, fbHeight, bgX + padding, bgY + padding, basename.c_str());
  }
}

void StandbyScreen::render(uint8_t* fb, uint16_t fbWidth, uint16_t fbHeight, const Font& font) {
  if (mode_ == Mode::kShowingImage) {
    // 通常はshowImage()が画像表示への遷移(「LOADING...」表示→デコード→表示)を
    // 完結させ、imageDrawn_をtrueにする(main.cpp参照)。ここに来るのは、電源
    // 不安定期間中に遷移が発生しpendingRedrawAfterSettle経由でこの通常の
    // render()パイプラインが呼ばれた場合等のフォールバックのみ。
    if (!imageDrawn_ && focusIndex_ >= 0 && focusIndex_ < static_cast<int>(jpegFiles_.size())) {
      FrameBufferOps::fillRect(fb, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight, false);  // 白紙に戻してから描画
      const String path = String(kStandbyDir) + "/" + jpegFiles_[focusIndex_];
      decodeAndDrawImage(path, fbWidth, fbHeight, fb, appSettings_.standbyGammaPercent);
      saveWallpaper(fb);
      drawOverlays(fb, fbWidth, fbHeight, font);
      imageDrawn_ = true;
    }
    return;  // 画像表示中はステータスバー・フッターを重ねない(画像を極力汚さない)
  }

  if (jpegFiles_.empty()) {
    font.drawText(fb, fbWidth, fbHeight, 16, 36, "(NO IMAGES IN /System/standby)");
  } else {
    for (int i = 0; i < visibleRowCount_; i++) {
      rows_[i].render(fb, fbWidth, fbHeight, font);
    }
  }
  footer_.render(fb, fbWidth, fbHeight, font);
}

void StandbyScreen::showImage() {
  if (focusIndex_ < 0 || focusIndex_ >= static_cast<int>(jpegFiles_.size())) {
    imageDrawn_ = true;
    return;
  }

  uint8_t* fb = display_.getFrameBuffer();
  const String path = String(kStandbyDir) + "/" + jpegFiles_[focusIndex_];

  // JPEGデコード(画像によっては数百ms〜)には時間がかかるが、パネルの表示内容は
  // display_.displayBuffer()を呼ぶまで変わらないため、何もしないとデコード完了までの
  // 間ずっと直前の一覧画面が表示されたままになり、「ボタンを押したのに一覧画面が
  // 一瞬見える(実際には反応が遅れているだけ)」ように見えてしまう。デコードを
  // 始める前に軽い「読み込み中」表示を1回送り、一覧画面の残像を先に消しておく
  // ことで、ボタン操作への反応を体感しやすくする。
  //
  // FAST_REFRESHではなく必ずFULL_REFRESHを使う: FAST_REFRESH(差分更新)は
  // 波形が簡易な分、直前だけでなく「さらに数世代前」の表示(例: この待機画面に
  // 入る前のホーム画面やヘッダー)の残像をきれいに消し切れないことが実機で
  // 確認された。FULL_REFRESHは完全な清掃波形を使うため確実に残像が消えるが、
  // その分この読み込み表示自体の反応はFAST_REFRESHより少し遅くなる
  // (体感の速さより、残像が一切見えないことを優先した)。
  {
    static const char* kLoadingText = "LOADING...";
    FrameBufferOps::fillRect(fb, fbWidth_, fbHeight_, 0, 0, fbWidth_, fbHeight_, false);
    const int textWidth = font_->measureText(kLoadingText);
    const int x = (static_cast<int>(fbWidth_) - textWidth) / 2;
    const int y = (static_cast<int>(fbHeight_) - font_->lineHeight()) / 2;
    font_->drawText(fb, fbWidth_, fbHeight_, x, y, kLoadingText);
    display_.displayBuffer(EInkDisplay::FULL_REFRESH);
  }

  // あえてグレースケール駆動(EInkDisplay::displayGrayBuffer()、専用の中間電圧LUT
  // 波形)は使わず、常に通常の白黒2値駆動だけで描画する。以前は4階調グレースケール
  // 表示を行っていたが、実機検証で「グレー駆動を1回でも使うと、画素数に関わらず
  // 表示後数秒〜十数秒かけて画像全体が光学的に緩和し明るく見える(退色する)」ことが
  // 確認され、グレー駆動を完全に廃止して初めて症状が消えることも確認された
  // (quantizeLevel()宣言部のコメント参照)。この画面はユーザーがCONFIRMを押すまで
  // 何秒でも画像を表示し続ける設計のため、パネルが最も安定する白黒2値駆動だけを
  // 使う。
  display_.clearScreen();
  FrameBufferOps::fillRect(fb, fbWidth_, fbHeight_, 0, 0, fbWidth_, fbHeight_, false);
  decodeAndDrawImage(path, fbWidth_, fbHeight_, fb, appSettings_.standbyGammaPercent);
  g_wallpaperValid = true;
  // turnOffScreen=trueでパネルのアナログ電源(チャージポンプ・VCOM等)を明示的に
  // 遮断する(EInkDisplay/README.mdが「プログラム終了前に必ずパネルを電源オフし
  // 画像をlock inすること」と明記している)。この画面は表示後ユーザーがCONFIRMを
  // 押すまで何秒でも表示し続けるため、通電したままにしないための保険。次に一覧へ
  // 戻る等でdisplayBuffer()を呼んだ際は、isScreenOnを見て自動的に再通電するため、
  // 呼び出し側で追加の対応は不要(EInkDisplay.cppのisScreenOnチェック参照)。
  saveWallpaper(fb);
  drawOverlays(fb, fbWidth_, fbHeight_, *font_);
  display_.displayBuffer(EInkDisplay::FULL_REFRESH, true);
  imageDrawn_ = true;
}

ScreenAction StandbyScreen::handleButton(uint8_t buttonIndex) {
  if (mode_ == Mode::kShowingImage) {
    if (buttonIndex == InputManager::BTN_CONFIRM) {
      sleepRequested_ = true;
      return ScreenAction::kNone;  // main.cpp側がconsumeSleepRequested()を見て処理する
    }
    if (buttonIndex == InputManager::BTN_BACK) {
      if (quickMode_) {
        // 一覧を経由せず直接ランダム表示に入った(AutoStandby/電源キー長押し)場合は
        // 一覧に戻さず、main.cppのkNavigateBack+kStandbyの既存処理でそのままホームへ戻す。
        quickMode_ = false;
        return ScreenAction::kNavigateBack;
      }
      mode_ = Mode::kList;
      imageDrawn_ = false;
      return ScreenAction::kRedraw;
    }
    return ScreenAction::kNone;
  }

  const int total = static_cast<int>(jpegFiles_.size());
  if (buttonIndex == InputManager::BTN_RIGHT || buttonIndex == InputManager::BTN_DOWN) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + 1) % total;
    updateRowSelection();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_LEFT || buttonIndex == InputManager::BTN_UP) {
    if (total == 0) return ScreenAction::kNone;
    focusIndex_ = (focusIndex_ + total - 1) % total;
    updateRowSelection();
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_CONFIRM) {
    if (total == 0) return ScreenAction::kNone;
    mode_ = Mode::kShowingImage;
    imageDrawn_ = false;
    return ScreenAction::kRedraw;
  }
  if (buttonIndex == InputManager::BTN_BACK) {
    return ScreenAction::kNavigateBack;
  }

  return ScreenAction::kNone;
}
