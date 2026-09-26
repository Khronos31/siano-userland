# siano-ts

PLEX PX-S1UD などの Siano RIO 系 USB チューナーに対応した、ユーザー空間で動作する ISDB-T 選局・MPEG-TS 出力ツールです。

## 概要

`siano-ts` は、単一の実行ファイルとして USB デバイスを直接制御し、ファームウェア転送と ISDB-T チャンネル選局を行い、受信した MPEG-TS を標準出力または指定ファイルへ出力します。常駐デーモンやカードリーダー機能は備えていません。

## 対応機種・対応OS

### 対応機種

主対象は PLEX PX-S1UD です。以下の USB ID を持つ Siano RIO 系デバイスを ISDB-T 機器として扱います。実機で受信動作を確認しているのは PX-S1UD (`3275:0080`) のみであり、`187f:0600` および `187f:0302` はコード上対応していますが実機未検証です。

- `3275:0080` (PX-S1UD、実機確認済み)
- `187f:0600` (実機未検証)
- `187f:0302` (実機未検証)

### 対応OS

- **Linux** (glibc / musl; x86_64 / aarch64)
- **macOS**
- **Android (Termux)** (aarch64 / armv7a / x86_64)
  - Termux 用のコマンドライン実行ファイルです（Android 向け APK アプリケーションではありません）。
  - x86_64 は Bliss OS / Termux で実機確認済みです。
- **Windows** (x64, WinUSB)

## 必要物

### ファームウェア

動作には ISDB-T ファームウェア (`isdbt_rio.inp`) が必要です。

- **ファイル名**: `isdbt_rio.inp`
- **SHA-256**: `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`
- **探索順序**:
  1. `--firmware PATH` で指定されたパス
  2. `./firmware/isdbt_rio.inp`
  3. `./isdbt_rio.inp`
  4. `/lib/firmware/isdbt_rio.inp` (Linux / macOS)

配布用バイナリアーカイブにはライセンスに従い同梱されています。ソースツリーには含まれません。

### ランタイム

- **Linux**: ソースビルドは glibc / musl に対応します。配布バイナリは libusb 1.0.30 を静的リンクした、libc 非依存のバイナリです。
- **macOS**: 追加ランタイム不要。配布バイナリ (arm64) は libusb 1.0.30 を静的リンクしており、Homebrew などの libusb は不要です (動的に読み込むのは macOS のシステムライブラリとフレームワークだけです)。`MACOSX_DEPLOYMENT_TARGET=11.0` でビルドしています。
- **Android (Termux)**: 追加ランタイム不要 (Bionic 向けに libusb を静的リンク済み)。配布対象 ABI は aarch64 / armv7a / x86_64 です。
- **Windows**: WinUSB ドライバ、`libusb-1.0.dll` (配布アーカイブに同梱)

## 導入

- **Linux / macOS**: 実行ファイルとファームウェアを配置します。ソースからビルドする場合は libusb-1.0 の開発用パッケージが必要です。
- **Windows**: Zadig などを用いて対象チューナーのドライバを WinUSB に設定します。配布 zip ではルートに `siano-ts.exe` と `libusb-1.0.dll`、`firmware/` 配下に `isdbt_rio.inp` が配置されています。アーカイブの構成を保ったままルートをカレントディレクトリとして実行するか、`--firmware` でファームウェアのパスを明示します。
- **Android (Termux)**: Termux 環境に実行ファイルとファームウェアを配置します。USB デバイスのオープンには `termux-usb` コマンドを使用します。

### Linux の USB デバイス権限

Linux ディストリビューション別の実機検証済み構成例は [Linux環境別の検証済み構成例](docs/platforms/README.md) を参照してください。

> [!WARNING]
> - チューナー接続前にカーネルモジュール（`smsusb`、`smsdvb`、`smsmdtv`）をblacklistへ登録する。
> - 稼働中のチューナーをカーネルから `siano-ts` へ動的に切り替える運用（live handoff）は安全と判定していない。`siano-ts` は既定では**カーネルのドライバが掴んでいるデバイスを奪わず**、理由を表示して終了する（奪うのは `--detach-kernel-driver` を付けたときだけ）。
> - すでにbind済みの場合は、blacklistを反映したうえでOSを再起動する。
> - 詳細は [AppArmor文書の競合説明](docs/platforms/apparmor.md#smsusbとの競合) を参照する。

Linux では実行ユーザーに USB デバイスノードの読み書き権限が必要です。権限がない場合は `libusb_open: LIBUSB_ERROR_ACCESS` になります。udev を使う環境では、対象 ID だけを許可するルール例を `/etc/udev/rules.d/70-siano-userland.rules` に置けます。

```udev
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3275", ATTR{idProduct}=="0080", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0600", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0302", MODE="0660", GROUP="video"
```

実行ユーザー（サービスなら `User=` で指定したユーザー）を `video` グループへ追加する例:

```sh
sudo usermod -aG video "$USER"
```

ルールを再読込する例:

```sh
sudo udevadm control --reload-rules
```

再読込後は既存のデバイスノードへ反映するため、チューナーを物理的に挿し直してください。`MODE="0666"` のような全ユーザー許可は推奨しません。権限設定後は常に `sudo` で実行する必要はありません。

#### Alpine Linux / BusyBox mdev

Alpine Linux（BusyBox mdev、コールドプラグスキャンヘルパー、OpenRC）での実機検証済み手順は [Alpine Linux の構成例](docs/platforms/alpine-mdev.md) を参照してください。USB ノードのパーミッションや video グループの要件は上記と同様です。

#### AppArmor

自作profileによる拘束、USBデバイスノードの権限、`smsusb`との競合に関する実機検証結果は [AppArmorで実行する際の注意](docs/platforms/apparmor.md) を参照する。

#### SELinux

Fedora 44（SELinux Enforcing）における検証要約、証明範囲、再検証時の確認項目は [Fedora 44 / SELinux Enforcing](docs/platforms/fedora-selinux.md) を参照する。

## 最短の使用例

```sh
# 認識されている Siano デバイスの一覧表示
./siano-ts --list

# 地上波 27ch を選局し、MPEG-TS を標準出力へ出力
./siano-ts --channel 27

# 地上波 27ch を 30 秒間受信し、ファイルへ保存
./siano-ts -c 27 -t 30 -o /tmp/output.ts

# 起動後に標準入力から選局し直す (channel 27 / tune 545142857 / quit)
./siano-ts --control

# Android (Termux) で termux-usb を介して受信
termux-usb -r -e './siano-ts --channel 27' /dev/bus/usb/001/004
```

## CLI仕様

```
使用法: siano-ts [オプション]
```

`--list` と `--control` を除き、受信には `-c, --channel` または `-f, --freq` のどちらか一方が必須です（両方の同時指定は不可）。`--control` では初期の `-c` / `-f` を省略できます。

### オプション一覧

| オプション | 引数 | 説明 |
|---|---|---|
| `-c, --channel` | `N` | ISDB-T 物理チャンネル (13..62)。`-f` と排他。`--control` なしの受信では `-f` とどちらか一方が必須。 |
| `-f, --freq` | `HZ` | 受信周波数を Hz 単位で指定。`-c` と排他。`--control` なしの受信では `-c` とどちらか一方が必須。 |
| `-t, --time` | `SECONDS` | 1以上の秒数を指定して受信後に終了。省略時は SIGINT (Ctrl+C) まで継続。 |
| `--control` | なし | 標準入力の `channel N` / `tune HZ` / `quit` で選局・終了。初期選局は省略可能。TS は stdout、診断は stderr。`-t` / `--list` とは併用不可。 |
| `-o, --output` | `PATH` | MPEG-TS の出力先ファイルパス。省略時は標準出力 (stdout)。 |
| `-d, --device` | `SPEC` | 対応 RIO デバイスを選択。インデックス (0 起算)、ポートパス (`0-1.3` / `1-4.3`)、bus:address (`0:4` / `1:4`) を受理。既定値はインデックス `0`。 |
| `-l, --list` | なし | デバイスを開かずに一覧表示。`--device` 指定で一覧結果は絞り込まれません。`px4d --list` に揃えた `key=value` 形式で、各行に `bus=` / `address=` / `port=` を付ける（下記）。 |
| `--detach-kernel-driver` | なし | カーネルのドライバ (`smsusb` など) が掴んでいても切り離して使う。既定では、`siano-ts` が使うインターフェースをカーネルのドライバが掴んでいれば奪わずに `interface N is bound to a kernel driver` と表示して終了する (live handoff は安全と判定していないため)。Windows など libusb が判定できない環境では従来どおり開く。 |
| `--fd` | `FD` | オープン済みの USB ファイルディスクリプタ番号。`termux-usb -e` が末尾に追加する整数引数も同義。`--list` または 0 以外の `--device` とは併用不可。 |
| `-p, --pid` | `PID` | 受信する PID (複数回指定可、最大64個)。1個以上指定した場合は指定 PID 群のみを設定。未指定時はキャッチオール `0x2000` を設定。明示した `0x2000` も ACK を待たずに設定する。最初の選局成功後に一度だけ設定する。 |
| `-F, --firmware` | `PATH` | ファームウェアファイル (`isdbt_rio.inp`) のパス。 |
| `-v, --verbose` | なし | 制御メッセージ種別を標準エラー出力へ表示。 |
| `--fail-on-drop` | なし | TS キューの最初の満杯を検知した時点で出力を止め、終了コード `8` で終了する。省略時は従来どおり drop 数を記録して受信を続け、終了時に drop があれば `8` を返す。 |
| `-h, --help` | なし | ヘルプを表示して終了。 |

`--list` は `px4-userland` の `px4d --list` に揃えた `key=value` 形式です。対応 RIO デバイス 1 台につき `model= usb= bus= address= port= status=ready receivers=1` の行と、受信機の `receiver=0 device=1 local=0 system=ISDB-T` の行を出力します（Siano はシリアル番号を持たないため `serial=` はありません）。`--device` は受信時の選択に使い、一覧出力は絞り込みません。対応外の Siano デバイスは `rejected model= usb= bus= address= port= status=unsupported` の行になります。`bus` と `address` は usbfs のノード（Linux では `/dev/bus/usb/BBB/AAA`）、`port` は Linux の `/sys/bus/usb/devices` での名前（`バス-ポート.ポート…`）で、ポートの並びが分からないときは `-` です。PX-S1UD のようにシリアルの無い機材は `port` で見分けられ、`--fd` で渡すノードも `bus` と `address` から分かります。いずれもデバイスを開かずに得られる値です。デバイスが無いときは何も出力せず終了します。

```
$ ./siano-ts --list
model=PX-S1UD usb=3275:0080 bus=1 address=4 port=1-2 status=ready receivers=1
receiver=0 device=1 local=0 system=ISDB-T
```

MPEG-TS ストリームデータは標準出力または `-o` で指定したファイルへ出力されます。診断やログはすべて標準エラー出力 (stderr) へ出力されるため、標準出力をパイプ等で安全に中継できます。

### 終了コード

| code | 意味 |
|---:|---|
| 0 | 正常終了、SIGINT/SIGTERM、または `--control` の `quit` |
| 1 | その他のエラー |
| 2 | 引数不正 |
| 3 | RIO デバイスが見つからない |
| 4 | デバイス使用中、またはカーネルドライバー bind 済み |
| 5 | 選局・ロック待ちタイムアウト |
| 7 | 受信中の USB 切断 |
| 8 | TS キュー満杯による drop |
| 10 | ファームウェア欠損またはロード拒否 |
| 70 | メモリ確保またはスレッド生成失敗 |

既定では終了時に `TS queue dropped` が出力された場合は code `8` を返します。`--fail-on-drop` を指定すると最初のdrop時に writer を起こし、プロセスを終了させます。

同一Linuxホスト内でlocalhost usbipを使用する場合、export元の物理USBノードとVHCI側のimport済みノードを区別するため、VHCI側ノードを事前にopenして`--fd`で渡す経路を実機検証している。これは同一ホスト内での検証記録であり、LAN経由のusbip構成に関する要件を示すものではない。

## 注意事項

### Linux での任意の性能最適化

USB ノードの読み書き権限とは別に、Linux では USB イベントスレッドの `SCHED_FIFO` リアルタイムスケジューリング (`CAP_SYS_NICE` または適切な `RLIMIT_RTPRIO`) と `mlockall` によるメモリロック (`CAP_IPC_LOCK` または十分な `RLIMIT_MEMLOCK`) を任意の最適化として試みます。これらがなくても警告を出して通常優先度・通常のメモリ管理で処理を継続し、致命的なエラーにはなりません。

### PID フィルタの ACK 応答

キャッチオール PID (`0x2000`) に対し、ファームウェアから ACK 応答が返らない場合があります。未指定時と `--pid 0x2000` のどちらも ACK を待たずに設定し、ストリーム受信を継続します。

## ビルド

### Linux / macOS / Android (Termux)

要件: C11 コンパイラ、`make`、`pkg-config`、libusb-1.0 開発用パッケージ (`--fd` サポートには libusb 1.0.23 以上)。

```sh
make
make test
```

配布バイナリと同じく libusb を静的リンクしたビルドは、Linux では `scripts/build-linux-static.sh` (Alpine)、macOS (arm64) では `scripts/build-macos-static.sh` で作れます。どちらも固定した libusb 1.0.30 のソースを取得・検証してビルドするため、libusb の開発用パッケージは不要です。手順は [packaging/REBUILD.md](packaging/REBUILD.md) を参照してください。

### Windows

要件:
- Visual Studio (MSVC)
- libusb のヘッダおよび x64 インポートライブラリ。`Makefile.win` の既定では `./libusb/include/libusb-1.0/libusb.h` と `./libusb/libusb-1.0.lib` を要求するため、この2ファイルを既定位置へ配置するか、同じ相対構造を持つディレクトリを `LIBUSB_DIR` で指定します（例: `nmake /f Makefile.win LIBUSB_DIR=...`）。実行時には `siano-ts.exe` と同じ場所に `libusb-1.0.dll` が必要です。

Visual Studio の Developer Command Prompt から実行します。

```cmd
nmake /f Makefile.win
```

## ライセンス

- **本体プログラム**: GPL-2.0-or-later ([COPYING](COPYING))
- **ファームウェア (`isdbt_rio.inp`)**: Siano 社の再配布許諾ライセンス ([LICENCE.siano](LICENCE.siano))。リバースエンジニアリング、逆コンパイル、逆アセンブルは禁止されています。
