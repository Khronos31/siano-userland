# siano-ts

[![CI](https://github.com/Khronos31/siano-userland/actions/workflows/ci.yml/badge.svg)](https://github.com/Khronos31/siano-userland/actions/workflows/ci.yml)

`siano-ts`は、PLEX PX-S1UDなどのSiano RIOファミリーUSBチューナー向けの
スタンドアロンなユーザー空間ドライバです。libusb-1.0だけを使用し、ISDB-T
ファームウェアをロードし、日本のISDB-T物理チャンネルを1つチューニングして、
MPEG-TSを標準出力（または`--output`）へ書き出します。診断出力は標準エラー出力へ
送られます。

USBとの通信にはlibusb-1.0だけを使用し（usbfs ioctlは使用しません）、musl、
glibc、Bionicでビルドできます。また、開かれたUSBファイルディスクリプタを
受け取れるため、Android/Termuxで`/dev/bus/usb`を列挙する必要がありません。

## ビルドとテスト

依存するものは、C11コンパイラ、`make`、`pkg-config`、およびlibusb-1.0の
開発用ファイル（`--fd`には>= 1.0.23）です。glibc固有のAPIは使用していません。

```sh
make
make test
```

CI（GitHub Actions）では、Ubuntu/glibc、Alpine/musl、macOS上でビルドとテストを
行い、Android NDKを使ってTermux用ELF（aarch64およびarmv7a）をクロスコンパイル
します。CIにはチューナーがないため、Linux/macOSジョブはコンパイル・リンクと
オフラインのプロトコルテストを行います。Androidジョブではランナー上でバイナリを
実行できません（Bionicはホストのlibcではないため）。Bionicインタープリター
（`/system/bin/linker64`または`/system/bin/linker`）、静的libusb、空の
`RPATH`/`RUNPATH`、およびELF全体にビルド/ソース/NDKの絶対パスが含まれていない
ことを検査します。

Alpineの場合：

```sh
apk add gcc make pkgconf musl-dev libusb-dev
make
```

実行時には`libusb`（`libusb-1.0.so.0`）が必要です。`make test`の`--list`ステップ
には`/dev/bus/usb`が必要です。これがないコンテナでは、muslバイナリ自体に問題が
なくても`libusb_init`が失敗することがあります。

Linuxでは、USBイベントスレッドが`SCHED_FIFO`と`mlockall`を要求します。いずれも
任意であり、`CAP_SYS_NICE` / `CAP_IPC_LOCK`がなくてもプロセスは通常の優先度で
継続します。処理中のUSBリングは32 × 16KiBです。

ファームウェアはこのリポジトリには含まれていません。`scripts/provenance.py`に
記録された固定済みlinux-firmwareコミットのURLから`isdbt_rio.inp`を取得し、
SHA256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`を
検証して`--firmware`で渡すか、`./firmware/isdbt_rio.inp`または
`/lib/firmware/isdbt_rio.inp`に置いてください。候補バイナリアーカイブには、
`firmware/isdbt_rio.inp`の隣にバイナリが含まれます（ELFには埋め込まれていません）。
ソースアーカイブにはファームウェアやベンダーのバイナリは一切含まれません。
[LICENCE.siano](LICENCE.siano)にあるSianoファームウェアのライセンスは、著作権表示と
免責事項を付けたバイナリの再配布を許可していますが、リバースエンジニアリング、
逆コンパイル、逆アセンブルを禁止しています。ファームウェアをgitに追加しないで
ください。

`main`上の**Actions → Release candidate**は、1組の候補セットをビルド、監査、
アップロードします。`main`を変更したり、タグを作成したり、公開済みリリースを
変更したりすることはありません。候補版では、公開済みの5つのバイナリアセット名を
維持します：Linux x86_64（Alpine/musl）、macOS arm64、Windows WinUSB x64、および
Androidの両ABIです。また、対応するソースアーカイブを1つと、外側の
`SHA256SUMS`も含みます。glibcはビルド対応かつCI監査対象ですが、パッケージには
含まれません。後で許可された場合の昇格には、再ビルドせず、監査済みのこれらの
バイト列をそのまま使用しなければなりません。Windowsでは一度だけWinUSB（Zadig）が
必要です。zipには`libusb-1.0.dll`と簡潔な出所情報が含まれますが、ダウンロード
した7zはビルド中に検証され、埋め込まれません。macOSではHomebrewの`libusb`が
必要です。AndroidアーカイブはTermux用のBionic ELFであり、Play Store APKでも
Linux muslアーカイブでもありません。

各ビルドホストのバイナリ監査には、正確なGitHub Actionsソースコミットが記録されます。
パッケージおよびアーカイブの監査では、その証拠がアーカイブマニフェストと一致する
ことが必要です。

## Termux

Android候補アーカイブには、LGPL-2.1のライセンステキスト、正確なlibusb 1.0.28の
ソースアーカイブ、`REBUILD.md`、NDKの注意事項、および監査可能な静的リンクの
一覧が含まれます。libusbは`--disable-udev --enable-static
--disable-shared`でビルドされ、`libusb-1.0.a -llog`としてリンクされるため、
実行時にTermuxの`$PREFIX/lib`は必要ありません。Linux muslアーカイブをスマート
フォンにコピーしないでください。`ld-musl`を要求するためロードできません。
Windowsパッケージでは、正確なlibusb 1.0.28パッケージと対応するソースアーカイブを
URLとSHA256で特定しています。

| アーカイブ | ABI | インタープリター | 一般的なデバイス |
|---|---|---|---|
| `siano-ts-*-android-aarch64.tar.gz` | `aarch64-linux-android` API 24 | `/system/bin/linker64` | 64ビット版Termux（Pixel） |
| `siano-ts-*-android-armv7a.tar.gz` | `armv7a-linux-androideabi` API 24 | `/system/bin/linker` | Google TV Streamer（`armeabi-v7a`のみ） |

USBアクセスは、`termux-usb`が`--fd`（または末尾の整数引数）にfdを渡す方式です。
`--list`は`/dev/bus/usb`を列挙し、`--fd`と同時には使用できません。
`firmware/isdbt_rio.inp`をバイナリの隣に置いてください。

## 使い方

ISDB-Tとして扱われるRIOのUSB IDは、`3275:0080`、`187f:0600`、および
`187f:0302`です。最後のIDはここでは意図的にRIOとして扱っています。そのカーネル
テーブル上のVenice/CMMBファームウェアを使用しないでください。

```sh
./siano-ts --list
./siano-ts --channel 27
./siano-ts -c 27 -t 30 -o /tmp/x.ts
./siano-ts --freq 557142857 --firmware /path/to/isdbt_rio.inp
./siano-ts -c 27 --pid 0 --pid 0x1fff
./siano-ts --channel 27 --fd 3
termux-usb -r -e './siano-ts --channel 27' /dev/bus/usb/001/004
```

`--fd`は`libusb_wrap_sys_device`でそのディスクリプタをラップし、デバイスを
スキャンしません。末尾の整数引数は`--fd`と同じです（`termux-usb -e`はfdを
追加します）。`--device N`は列挙時に一致するRIOデバイスのN番目を選択します。
`--list`はUSBデバイスを開かずにディスクリプタを列挙します。

デフォルトのPIDフィルターは`0x2000`（Siano/DVBのキャッチオール）です。すべての
`--pid`値はこれに加えて追加されます。このファームウェアは`0x2000`にACKを返さない
ことがありますが、多重化ストリームは流れ続けます。

mirakcのチューナーコマンドは次のように設定できます：

```toml
command = ['/usr/local/bin/siano-ts', '--channel', '{{channel}}']
```

## 範囲、ライセンス、出所情報

ソースはGPL-2.0-or-laterです。[COPYING](COPYING)を参照してください。通信プロトコルは、
提供されたLinux v6.18リファレンススナップショットに含まれるGPLのLinux
`smsusb`、`smscoreapi`、`smsdvb`のソースをもとに、ユーザー空間向けに書き直されて
います。カーネルのDVBコア、usbfs ioctl、libudev、IR、debugfs、sysfsのコードは使用
していません。Android/Termuxのfd経路を含め、USBアクセスはlibusbだけです。

実際のチューナーを頼りにする前に、5分間のキャプチャを3回行い、ffmpegの
`corrupt`報告を数えてください。目標は、カーネル側の軽減策のベースラインと一致する
0.33報告/分です。この測定は、このオフライン実装工程では意図的に行っていません。
