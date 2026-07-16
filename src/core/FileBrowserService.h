#pragma once
#include <Arduino.h>

#include <vector>

// フォルダ画面用のディレクトリエントリ(ファイル/フォルダ1件分)。
struct DirEntry {
  String name;
  bool isDirectory;
  uint32_t size;  // ディレクトリの場合は0
};

// SDCardManager(open-x4-sdk)の上に被せた薄いラッパー。
//
// SDCardManager::listFiles()はファイルのみを返しディレクトリを除外するため、
// フォルダ画面(ファイル/フォルダ両方を一覧表示する必要がある)には使えない。
// このクラスはディレクトリ直下のファイル・フォルダを両方列挙する用途に特化している。
//
// 実ファイルの読み込み(TXT/EPUB本文など、フェーズ3以降)は、このクラスを経由せず
// 引き続きSDCardManager(SdMan)のreadFile系メソッドを直接使う想定。
class FileBrowserService {
 public:
  bool begin();
  bool ready() const;

  // pathディレクトリ直下のエントリ一覧を返す(ディレクトリ優先、次に名前の昇順)。
  // ディレクトリが開けない場合や空の場合は空のvectorを返す。
  std::vector<DirEntry> listDirectory(const char* path);
};
