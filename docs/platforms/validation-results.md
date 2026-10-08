# OS・環境別の検証結果

本ドキュメントは、特定のrevisionにおいて実施した実測記録であり、将来のバージョンやあらゆる動作環境における動作を保証するものではありません。

## 記録方法

Stable release の検証記録は本ファイルへ日付付きで追記する。新しい records directory や template framework、汎用検証スクリプトは作らない。release record には candidate version/commit/workflow run、8 archive（7 binary + source）の checksum と audit、baseline tag と各 archive の byte-identity 判定、変更の hunk-level class（platform guard 単位を含む）、claim ごとの `継承` / `今回再検証` / `未認定` / `対象外`、canary/soak の選定理由（環境ID E01–E17、固定順の位置、単一OS規則による非該当）、各 test の環境ID・host・USB `port=`・archive SHA-256・UTC時刻・コマンド・counter・終了コード・ログ保存先、未実施/非該当の物理操作と理由を記録する。canonical 環境ID と手順の正本は [`../release-validation.md`](../release-validation.md)。Android ad-hoc APK は dtv-android 所管であり本記録に含めない。

各記録は歴史的な測定であり、current candidate の pass として流用しない。exact candidate 未試験の claim は `未認定` と記す。

## 2026-09-12 v0.1.5 受入記録

- **検証対象コミット / ベースライン**: branch `main`、commit `6c5715ca87f75adbf799253573afc84e2beb23d5` (short `6c5715c`)、VERSION `0.1.5`（実機試験済み runtime/packaging baseline）
- **GitHub Actions**: Release candidate run `34682612787`（全15 job success）
- **パッケージング・成果物監査**:
  - 全7プラットフォームのバイナリアーカイブおよびソースアーカイブ（計8アーカイブ）の `SHA256SUMS` 照合・展開監査に成功。
  - 7バイナリアーカイブの `manifest.json` は version `0.1.5`、`source_ref="6c5715c"`。展開されたバイナリ群は同runのraw artifactとバイト完全一致（byte-identical）。
  - ソースアーカイブは `VERSION` 0.1.5、resolved commit `6c5715c`。
  - Windows ZIP内の `siano-ts.exe` (SHA-256 `dbdf7d6912ca60ffdce35bf65f5221a89d1a19c45b89bd32a04a86607995dc7f`) および `libusb-1.0.dll` (SHA-256 `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7`) は固定ベースラインと一致し、pre-RCの再現可能ビルド成果物とバイト完全一致。
- **変更内容**:
  - macOSにおけるTS continuity欠落バーストを抑止するため、`siano-ts.c`でmacOSのみ`MAX_URBS=128U`へ変更（他OSは従来の32Uを維持、commit `a4a914f`）。
  - Windows環境でのQPC（QueryPerformanceCounter）からtimespecへの変換において、`counter * 1000000000` のuint64オーバーフローにより単調時計および受信時間（--time）判定が壊れる不具合を修正。回帰テストを追加（commit `2cd91cd`）。
  - WindowsバイナリのPEタイムスタンプ差を解消するため、MSVCビルドに`/Brepro`を導入し、クリーン2回ビルド全バイト一致およびベースライン照合（`packaging/windows-baseline.sha256`）を行う再現可能ビルドを導入（commit `d418f8b`、`ee5c6b9`）。同一run内再現性とrun間一致（cross-run identity）を確認。
  - 非Windows成果物（macOS arm64、Linux static x86_64/aarch64、Android 3 ABI）およびWindows `libusb-1.0.dll` はbyte-identical（同一バイナリ）。
- **ファームウェア**: Android 3 ABI、macOS、Windowsの試験で使用したfirmware SHA-256は `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。
- **検証概要**:
  - **macOS**: M2 Mac mini / macOS 26.6.2 / PX-S1UD / T22にて、final RC archiveを展開して30秒受信を実施。64,972,800 bytes / 345,600 packets、188-byte remainder 0、TSDuck continuity error 0、exit 0を確認。先行pre-RC exact CI artifactにおいて30秒受信および30分連続受信（TSDuck continuity error 0、queue drop/libusb errorなし、正常終了）、受信中USB物理切断時の有限終了（LIBUSB_ERROR_IO）、再接続復帰、SIGINT正常終了（1秒以内exit 0）が検証済み。
  - **Windows**: GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343) / PX-S1UD (WinUSB) にて、final RC ZIPを展開しZIP内exact EXEで検証を実施。1800秒soakにて自律終了（実測約1802秒、stderr上USB/queue error記録なし、終了後プロセス残留0。監視ラッパーExitCode欄null、外側command exit 0）、Ctrl+CでLASTEXITCODE 0およびプロセス残留0、受信中物理切断にて有限終了（約36秒でLIBUSB_ERROR_PIPE、exit 1、プロセス残留0）、OS再起動なしの再接続後30秒TS取得にて64,769,760 bytes / 344,520 packets、remainder 0、continuity error 0、exit 0、プロセス残留0を確認。先行pre-RC exact CI artifact（run `34654057294` / commit `d418f8b`等）とbyte-identicalであることを確認。
  - **Android**: Pixel 9a (aarch64)、Google TV Streamer (armv7a)、Bliss OS (x86_64) にてfinal RC archiveを展開して実機受信検証を実施。3環境とも地上波T22の30秒取得にてremainder 0、continuity error 0、exit 0、プロセス残留0を確認。Bliss OSにおける30分連続受信、シグナル終了、物理切断有限終了・再接続復帰は先行検証済みバイナリとの完全一致により継承。
  - **Linux**: final RC archiveをCI上でUbuntu glibcおよびAlpine musl（x86_64 / aarch64）の全4組合せで起動確認済み。各archの同一ランタイムバイナリ系統でLinux実機受信を検証済み。
  - 全プラットフォーム向けの統合 release-candidate archive の作成・展開監査および各対象環境での受入試験は完了。

| 環境 | arch/libc | 対象バイナリ SHA-256 | 確認内容 | 状態 |
| --- | --- | --- | --- | --- |
| Pixel 9a / Android 17 / Termux | aarch64 / Bionic | `5046f44a2c7b93abe07881bd0937169bd68cec6470d8995501e5832c6306a4ff` | final RC archiveを展開しPX-S1UDで地上波T22を30秒受信。64,766,000 bytes / 344,500 packets、remainder 0、continuity error 0、exit 0、終了後プロセス残留0。先行バイナリ受信（約31秒、64,972,800 bytes / 345,600 packets、エラー0）の証跡を継承。 | final RC実機受信確認済 |
| Google TV Streamer / Android 14 / Termux arm | armv7a / Bionic | `c0e19d928f7d4e26cacf3830f923c94fc041da32824ecbf08fd070413aef54fc` | final RC archiveを展開しPX-S1UDで地上波T22を30秒受信。64,747,200 bytes / 344,400 packets、remainder 0、continuity error 0、exit 0、終了後プロセス残留0。先行バイナリ受信（約31秒、64,758,480 bytes / 344,460 packets、エラー0）の証跡を継承。 | final RC実機受信確認済 |
| Bliss OS / Termux | x86_64 / Bionic | `0db2e15cfaab035e70783645a6132078608581b8335b1acd894e98d694cde353` | final RC archiveを展開しPX-S1UDで地上波T22を30秒受信。64,972,800 bytes / 345,600 packets、remainder 0、continuity error 0、exit 0、終了後プロセス残留0（初回USB permission時間切れ後、Termux前景で再試行成功、残留0）。先行バイナリでの30分受信、シグナル、物理切断・再接続の検証証跡を継承。 | final RC実機受信確認済 |
| M2 Mac mini / macOS 26.6.2 | arm64 | `23614737764d6a3ef0a2355b9ed204930e3a830384e31f03aaf09a623da6523f` | final RC archiveを展開しPX-S1UDで地上波T22を30秒受信。64,972,800 bytes / 345,600 packets、188-byte remainder 0、TSDuck continuity error 0、exit 0。先行pre-RC exact CI artifactでの30秒受信、30分連続受信（TSDuck continuity error 0、queue drop/libusb error 0、残留0）、物理切断有限終了（LIBUSB_ERROR_IO）、再接続復帰、SIGINT（1秒以内exit 0）の検証証跡を継承。 | final RC実機受信確認済 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343) | x86_64 / Windows (WinUSB) | EXE: `dbdf7d6912ca60ffdce35bf65f5221a89d1a19c45b89bd32a04a86607995dc7f`<br>DLL: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7` | final RC ZIPを展開しZIP内exact EXEでPX-S1UD（WinUSB）の実機検証を実施。1800秒soak自律終了（実測約1802秒、stderr上USB/queue error記録なし、終了後プロセス残留0。監視ラッパーExitCode欄null、外側command exit 0）、Ctrl+C終了（LASTEXITCODE 0、残留0）、物理切断有限終了（約36秒でLIBUSB_ERROR_PIPE、exit 1、残留0）、OS再起動なしの再接続後30秒TS取得（64,769,760 bytes / 344,520 packets、remainder 0、continuity error 0、exit 0、残留0）。pre-RC再現可能ビルド成果物とバイト完全一致。 | final RC実機受信確認済 |


## 2026-09-05 共通回帰

| 環境 | arch/libc | revision | 確認内容 |
| --- | --- | --- | --- |
| Home Assistant OS（kernel 6.18.39-haos） | x86_64 / musl（Alpine 3.20.10） | `0802a500271208dc610f0f98deafaeb95f6f5667` | PX-S1UD 1台で地上波27chを5秒、5秒、30秒受信。全てlockおよびexit 0。30秒は64,972,800 bytes / 345,600 packets、188-byte alignment、sync/malformed/TEI/continuity error 0。終了後process残留なし。 |
| Latitude 5300 / AnduinOS（kernel 7.0.0-31-generic） | x86_64 / glibc 2.43 | `0802a500271208dc610f0f98deafaeb95f6f5667` | native `make test`成功。PX-S1UDで地上波27chを5秒、5秒、30秒受信し全てexit 0。30秒は64,758,480 bytes / 344,460 packets、188-byte alignment、sync/malformed/TEI/continuity error 0。USB node権限のためhardware取得時のみsudoを使用。 |
| M2 Mac mini / macOS 26.6.2 | arm64 | `0802a500271208dc610f0f98deafaeb95f6f5667` | PX-S1UDで地上波27chを5秒、5秒、30秒受信。全てlockおよびexit 0。30秒は64,758,480 bytes / 344,460 packets、alignment/sync/malformed/TEI/continuity error 0。 |
| Pixel 9a / Termux | aarch64 / Bionic | `0802a500271208dc610f0f98deafaeb95f6f5667` | Android API 24向け成果物。`termux-usb`から渡した1 fdを使用。PX-S1UDで地上波27chを5秒、5秒、30秒受信し全てlockおよびexit 0。30秒は64,972,800 bytes / 345,600 packets、sync/malformed/TEI/continuity error 0。`mlockall`とrealtime scheduling拒否は非致命。 |
| Google TV Streamer / Termux | armeabi-v7a / Bionic | `0802a500271208dc610f0f98deafaeb95f6f5667` | Android API 24向け成果物。`termux-usb`から渡した1 fdを使用。PX-S1UDで地上波27chを5秒、5秒、30秒受信し全てlockおよびexit 0。30秒は64,750,960 bytes / 344,420 packets、sync/malformed/TEI/continuity error 0。`mlockall`とrealtime scheduling拒否は非致命。 |
| Google TV Streamer / ad-hoc APK | armeabi-v7a / Bionic | `0802a500271208dc610f0f98deafaeb95f6f5667` | Android USB Host API所有の1 fdを使用。PX-S1UDで地上波27chを5秒、5秒、30秒受信し全てlockおよびexit 0。30秒は64,972,800 bytes / 345,600 packets、sync/malformed/TEI/continuity error 0。APKはこのリポジトリの配布物ではない。 |
| GEEKOM A6 / Windows 11 x64 | x86_64 / Windows | `0802a500271208dc610f0f98deafaeb95f6f5667` | WinUSB構成のPX-S1UDで地上波27chを5秒、5秒、30秒受信。全てlockおよびexit 0。30秒は64,758,480 bytes / 344,460 packets、sync/malformed/TEI/continuity error 0。 |

## 追加検証

| 環境 | revision | 確認内容 | 補足 |
| --- | --- | --- | --- |
| HAOS上のDebian 13 Studio Code Server container | — | x86_64 / glibc 2.41。PX-S1UDでstatic CLIのsmoke、soak、物理切断と再接続を確認。 | container root実行であり、一般ユーザー権限試験の代用ではない。 |
| HAOS Supervisor管理Alpine add-on | — | x86_64 / musl。PX-S1UDでSupervisorのUSB公開、container起動停止、物理切断時の有限終了、再接続後の手動復帰、自動restart loopなしを確認。 | — |
| Alpine Linux 3.24.1 | `898fa71e0438e36b4299d4f836e67ad5321ec8ae` | x86_64 / musl 1.2.6。PX-S1UD 2台、mdev hotplug/coldplug helper、一般ユーザーで実行。両方30秒lock・exit 0、queue drop 0、188-byte alignment、process残留なし。 | [Alpine Linuxの構成例](alpine-mdev.md) |
| Fedora Linux 42 | `b2c946626ea7a1590b417f6a477c1dc2337532ed` | aarch64 / glibc 2.41 / kernel 4.9.140-l4t+。配布archiveを一般ユーザーで実行。PX-S1UDで地上波22chを30秒受信、64,758,480 bytes / 344,460 packets、remainder/sync/TEI 0、process残留なし。 | kernel moduleはblacklist済み。SELinuxはDisabled。 |
| NixOS 26.05.9227.c25784012c99 | `898fa71e0438e36b4299d4f836e67ad5321ec8ae` | x86_64 / glibc 2.42。PX-S1UD 2台、一般ユーザーで両方受信成功。blacklistを固定したcold boot後もkernel module未ロードで両方受信成功。 | [NixOSの構成例](nixos.md) |
| Chimera Linux rootfs snapshot 20251220 | `898fa71e0438e36b4299d4f836e67ad5321ec8ae` | x86_64 / musl。Clang native buildと全test、native動的binaryと配布static binaryの双方でPX-S1UD 2台同時受信、lock、exit 0、queue drop 0、188-byte alignmentを確認。 | [Chimera Linuxの構成例](chimera-linux.md) |
| Fedora 44 | commit `0315bd5609422748293bdde63e760c9e02a9cef8` / version `0.1.4` | 保存済みv0.1.4要約上の受信確認（一般ユーザー、地上波27ch 30秒）。 | 現行v0.1.5、専用ポリシー、systemd実行は再検証対象。詳細は [Fedora 44 / SELinux Enforcing](fedora-selinux.md) を参照。 |
| FreeBSD 15.1-RELEASE | — | amd64。ソース修正なし、native buildと全testを実施。PX-S1UD 2台の単独・同時受信を確認。単独TSのalignment/sync/TEI 0、同時受信は両方exit 0。 | 現行Release対象外。 |
| OpenWrt 25.12.5 | `0315bd5609422748293bdde63e760c9e02a9cef8` | x86_64 / musl 1.2.5 / procd。static/stripped成果物を使用。USB `3275:0080`。PX-S1UDで地上波27chを10秒受信、21,804,240 bytes / 115,980 packets、alignment/sync/TEI/continuity error 0。受信中切断で`LIBUSB_ERROR_IO`・exit 1・ハングなし。再接続後1回目の列挙で復帰し同条件の受信成功。 | OpenWrt向けソース修正なし。 |
| Bliss OS（Android 13 API 33 / x86_64 / Bionic） | `2b73ec58a756d49b96402cc6f4c7a201132ffddc` | PX-S1UDを使用。CI成果物で地上波27chを10秒受信し21,800,480 bytes / 115,960 packets、native buildと全test成功後のnative binaryで30秒受信し64,972,800 bytes / 345,600 packets。いずれもalignment/sync/malformed/TEI/continuity error 0。受信中切断は`LIBUSB_ERROR_IO`・exit 1で有限終了し、プロセス残存なし。OS再起動なしの再接続後も受信成功。 | IP3 GT1、kernel 6.1.112-gloria-xanmod1、Termux 0.118.3。 |
| Latitude 5300 / AnduinOS（Linux標準Sianoドライバ参照比較） | kernel 7.0.0-31-generic | 同一PX-S1UD・T22でLinux標準smsusb/smsdvbによる約6分受信（recisdb 1.2.4、777,060,352 bytes / 4,133,299 packets、末尾140 bytes、SHA-256 `7656a9cf61e6de1da05505d204f5c612ea4bd3155a0826ddf869abfe2e73b892`）。sync loss 0、resync 0、TEI 0、malformed adaptation 0。packet index 4,116,021〜4,116,267（出力停止の約1.5秒前）に4 PID（273, 274, 2112, 2144）のcontinuity missing 9 event（計52 missing packets）を検出。短時間の複数PID burstという形態はmacOS userland（旧32 transfer時）と共通するが、発生量・PID数・位置は同等でなく原因は未特定。本比較時点ではmacOS判定を保留としたが、後続の転送深度調査（MAX_URBS=128U）およびexact CI artifact検証によりmacOSの実機検証は完了した。 | 参考比較用（siano-userlandバイナリではない） |
| M2 Mac mini / macOS 26.6.2（macOS限定 MAX_URBS=128U exact CI artifact） | commit `2cd91cd` / `a4a914f`（CI build SHA-256: `23614737764d6a3ef0a2355b9ed204930e3a830384e31f03aaf09a623da6523f`） | PX-S1UD / T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。macOSのみ`MAX_URBS=128U`とする修正後のexact CI成果物を使用。30秒受信（source/TSDuck/wait exit 0/0/0、continuity error 0）、30分連続受信（exit 0/0/0、TSDuck continuity error 0、queue drop/libusb errorなし、process残留0）。受信中物理切断時の有限終了（LIBUSB_ERROR_IO、exit 1、残留0）、再接続復帰、SIGINT（1秒以内exit 0）を確認。 | exact CI artifact実機検証完了。 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343)（Windows単調時計修正 exact CI artifact） | commit `2cd91cd`（CI build EXE SHA-256: `b9a90cb61127efeb7d64d96d259fe00c68be7da1e214bfb67b297fe34d66ffe6`、DLL SHA-256: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7`） | PX-S1UD（WinUSB）/ T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。単調時計修正後のexact CI成果物を使用。30秒smoke（exit 0）、30分soak（--time 1800にて約1800秒で自然終了、lock取得、queue drop/USB error 0、process残留0）、Ctrl+C終了、受信中物理切断時の有限終了（LIBUSB_ERROR_PIPE、exit 1、残留0）、再接続復帰（30秒TS取得exit 0、TSDuck continuity error 0）を確認。 | exact CI artifact実機検証完了。 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343)（Windows再現可能ビルド exact CI artifact） | commit `d418f8b`（実機試験元、run `34654057294`）/ commit `ee5c6b9`（baseline固定、run `34654669361`、cross-run byte-identical）（CI build EXE SHA-256: `dbdf7d6912ca60ffdce35bf65f5221a89d1a19c45b89bd32a04a86607995dc7f`、DLL SHA-256: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7`） | PX-S1UD（WinUSB）/ T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。MSVC `/Brepro`、2回クリーンビルド全バイト一致、ベースライン照合済みのexact CI成果物を使用。実機試験を実施した新exact artifactはrun `34654057294`（commit `d418f8b`）生成物（commit `ee5c6b9` / run `34654669361`とcross-run byte-identical）。30秒受信にて64,769,760 bytes / 344,520 packets、alignment 0、sync 0、TEI 0、continuity error 0、malformed 0、exit 0、process残留0を確認。同一runtime sourceに対しcommit `2cd91cd`旧EXEで30分soak検証済み。 | exact CI artifact実機検証完了。 |
| EPSON Endeavor NJ1000 / Debian 12 i386（32-bit / glibc / EHCI） | Stable v0.1.5 source archive | 条件付き合格。native buildおよびnonblocking sinkでの30分soak合格。低速ファイル出力でqueue drop検出（Issue #4）。 | 詳細および境界は [Debian 12 i386での検証結果](debian-i686.md) を参照。 |
| Latitude 5300 / AnduinOS（localhost usbip / VHCI） | Stable v0.1.5 x86_64 archive | 合格。同一ホスト内VHCIノードを`--fd`指定し、60秒gateおよび30分soak完走。全エラー0件。 | localhost/VHCI経路の実績。LAN経由は再検証対象。詳細は [localhost usbipでの検証結果](usbip-localhost.md) を参照。 |
| Latitude 5300 / AnduinOS（AppArmor enforce） | Stable v0.1.5 x86_64 archive | 合格。USB拒否gate、complain、enforce各試験完走。2台30分受信で両系統のalignment/sync/TEI/queue drop/libusb error 0件、AppArmor拒否0件。受信中物理切断（切断側exit 1有限終了、残存側受信継続・exit 0）、OS再起動なしの再接続復帰（2台60秒全エラー0）、cleanup pass。live handoff時のpage dumpは別個のfailとして保持。 | profileは利用者側で用意する。live handoffは行わない（smsusb unbind時page dump fail）。詳細は [AppArmorで実行する際の注意](apparmor.md) を参照。 |


## 2026-10-08 v0.1.10修正の機能確認（配布候補は未認定）

対象は `c0703d9` を基点とする #15（出力詰まり時の終了・制御）と #25（CLIの `T13`..`T62`）。#27は次版の対象。以下はソースからのnative buildによる機能確認であり、最終CI archiveの必須matrixを代替しない。

POSIXのソースsnapshot SHA-256は `9b4ecb3de1801de5581990acf3182a4e80dfb4fe68140cd8c91aa8a23eaf644e`。Windowsはsnapshot07（SHA-256 `eb416b5c52f72113930a5a084291e39a9614964b2dc9845bb76e8ab3818dcc88`）を使用。snapshot08の追加変更はPOSIX側のテストmockとMakefileで、Windows製品コードは同一。比較対象は公開v0.1.9。raw記録はHAOSの `/config/.work/siano-p0-channel-task/` に保持した。

| 環境 | 今回再検証した機能 | 結果・境界 |
|---|---|---|
| GEEKOM A6 / Windows build 26300.9457 / WinUSB / x64 | native全test、通常受信、未読stdoutのままtime・quit・fail-on-drop・実OS Ctrl+Break、物理切断と復帰 | 修正版は有限終了。Ctrl+Break exit0 / 26.21ms、quit exit0 / 48.24ms、time3 exit8 / lockから3.025秒、fail-on-drop exit8。切断はraw USB PIPE、drop19930、自然exit8。再接続後T27/10秒はexit0、21616240 bytes、188-byte remainder/sync異常0、残留0。 |
| Latitude 5300 / AnduinOS 2.0.4 / kernel 7.0.0-34-generic / glibc x64 | native全test、通常受信、未読stdoutのままSIGINT・SIGTERM・time・quit・fail-on-drop、物理切断と復帰 | 修正版は有限終了。SIGINT/SIGTERM exit0 / 25.186・25.239ms、quit exit0 / 50.404ms、time3 exit8 / lockから3.034167秒、fail-on-drop exit8。切断はraw USB IO、drop68442、自然exit8。再接続後T27/10秒はexit0、21586160 bytes、remainder/sync異常0、残留0。 |
| Mac mini / macOS 26.6.2 (25G83) / arm64 | native全test、通常受信、未読stdoutのままSIGINT・SIGTERM・time・quit・fail-on-drop、物理切断と復帰 | 修正版は有限終了。SIGINT/SIGTERM exit0 / 34.959・32.192ms、quit exit0 / 66.951ms、time3およびfail-on-drop exit8。切断はraw USB IO、drop61806、自然exit8。再接続後T27/10秒はexit0、21804240 bytes、remainder/sync異常0、残留0。USB portは切断前2-3、復帰時2-4。 |

Linux/Macの比較対象は、実際に未読pipeの詰まりを確認してからsignal/quitを送った各5ケースで30秒以内に終了せず、所有PIDをwatchdogで回収した。Windowsのtime/quit/fail-on-drop/Ctrl+Breakも同様。修正版の切断試験は全環境でwatchdogなしの自然終了を確認した。物理抜去時刻は計測しておらず、抜去から終了までの遅延は未測定。raw IO/PIPEは内部でNO_DEVICEへ写像され、drop優先によりexit8となるため、dropなしの切断exit7は最終配布候補で確認する。

未読pipeの実測占有量はWindows3948 bytes、Linux63168 bytes、Mac65424 bytes。Linux/Macでは188-byte writeを別pipeで校正し、製品stdoutは読み取らなかった。Linuxの初回は校正前の閾値が不適切でsignal/quit送信前に前提条件を満たせず、未完了として保持し、校正後に別試行で再検証した。Windowsの初回物理操作待ちは5分期限切れで未完了として保持した。補助PID0受信は比較対象・修正版ともfilter ACK timeoutで失敗し、通常受信や切断試験の合格根拠に含めない。

候補準備時の独立レビューで、パケットを100 bytes出力した後の成功retuneが未送信88 bytesを破棄し、継続する出力のpacket境界をずらす組合せを確認した。成功retuneでは開始済みpacketの残りだけをbounded pumpで完了し、他の旧データを破棄する実装へ修正。100-byte出力、zero-write、失敗retune、繰り返し成功retune、部分再開、新チャンネル出力の組合せを新規テストで検証した。snapshot09（SHA-256 `1947aa9e3677d83b41704432f9857a90e70081fdd52ca976bcd2485113fe6e2f`）でLinuxの全testとASan/UBSan、Macのnative全test、Windowsのnative build/全testが合格。既存のassertionは保持した。この追加修正後の実機受信・制御は最終候補の検証対象で、上表の旧snapshot実機結果から追加pathの合格を継承しない。

通常・復帰TSの検査は188-byte alignmentと各packetのsync byteまで。TEI・continuity・内容decodeはこの機能確認では未確認。Android3 ABI、Linux aarch64、全7配布archiveの最終実機matrixは未認定。

### Windows canonical hash取得とCI準備

commit `b43a72abc0cae546706441f4675bc1111d52b456` の [CI run 37690731443](https://github.com/Khronos31/siano-userland/actions/runs/37690731443) は、Windowsのnative全testと2回のclean buildの全byte比較を通過し、新EXE SHA-256 `f5bddcb5a181632a4906545a3cc28e9a44b917c4f9d73cd9e1f448fbbf1075f0` と旧固定hashの不一致で失敗した。`windows-2022` image `20260927.320.1`、VCTools `14.44.35207`、nmake `14.44.35229.0`、pinned libusb 1.0.30を使用。新hashを `packaging/windows-baseline.sha256` へ登録し、後続CIで改めて照合する。取得元のrunを合格扱いせず、最終candidateには全job成功を要求する。

同runのLinux source/static x86_64・aarch64、配布Linux archiveのglibc/musl起動、Android3 ABIは合格。Macはstatic製品build、output統合test、channel testまで合格したが、CLI testのシェルへの新規dylib注入がarm64/arm64e不一致で失敗した。既存CLI testの期待値を保持して起動方法を修正し、後続CIで再検証する。製品のUSB実機検証結果には数えない。

## 2026-10-08 v0.1.10 final candidate（7/7、実機検証完了）

配布候補は commit `89c240b8af021d55d81b3b90fce79a3690605811`、version `0.1.10`。通常 [CI 37691193910](https://github.com/Khronos31/siano-userland/actions/runs/37691193910) と [candidate生成 37691200913](https://github.com/Khronos31/siano-userland/actions/runs/37691200913) は全job成功。各archiveは2回の生成でbyte一致し、取得後のchecksumも一致した。WindowsのEXEは canonical pin `f5bddcb5a181632a4906545a3cc28e9a44b917c4f9d73cd9e1f448fbbf1075f0` と一致。先行の失敗CIやソースsnapshotの実機結果を、以下の配布archiveの認定へ置き換えてはいない。

| `siano-ts-0.1.10-` に続くarchive名 | SHA-256 |
|---|---|
| `linux-x86_64.tar.gz` | `c7b6a86d86d90f82af63b8cf95ab80b0690826d385aabb21f8377b9145266458` |
| `linux-aarch64.tar.gz` | `203cfd4388a0b0acf06a713eae2e7c9b31c3793aa48441db8d5d24f6937436e0` |
| `darwin-arm64.tar.gz` | `c1699b1125d30696aa42bbbaa5d9313763edd579a9be1055a316d2d51f94d992` |
| `android-aarch64.tar.gz` | `9d2df017f8680f93e83aa7af477baf36cdfa1a959b665d71a7871543c20a9cb4` |
| `android-armv7a.tar.gz` | `2b5aa8590fdfe9468d028512e06625a96d816042f52d34bc71e64bc60a1ba44a` |
| `android-x86_64.tar.gz` | `23ac088214a42afe8563ca59daccbb7c9554ff508dab5b8ccaf8fd2460749b53` |
| `windows-x64.zip` | `1cf4d28afb1994d98323b0112342e76cb2a7e5ad3bd87e3fb18c72af6aa1016d` |
| `source.tar.gz` | `5512fe690a392c888f4fb460bc81d27c0e773cdf8a1c7df5238f1e506717f6a7` |

### 配布archiveの短時間実機確認

各行は同じ候補archiveのbinaryと同梱firmware（SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`）を使用した。PX-S1UD 1台で、接続中に受信を継続したまま利用者がUSBを抜き、自然exit7・drop0・watchdogなしを確認。再接続後は再列挙または新しいTermux USB fdで `T27` を30秒受信し、自然exit0・drop0・残留なしを確認した。物理抜去の瞬間は計測しておらず、抜去から終了までの遅延は未測定。2台構成の残存側受信試験には数えない。

Linux/macOS/Windowsの起動引数は `B -F FW -d PORT -c T27 -o TS`、復帰時に `-t 30` を追加した。Termuxは `termux-usb -r -e` のコールバックから `B --fd 7 -F FW -c T27 -o TS` を起動し、同様に復帰時 `-t 30` を追加。Termux欄のnodeはFDを取得したUSB nodeであり、選択はfdによる。

| 状態 | artifact / 環境 | USB portまたはnode（切断前→復帰後） | 復帰TS / 終了UTC（2026-10-07） | raw記録のbasename |
|---|---|---|---|---|
| 今回再検証・合格 | Linux aarch64 / Switch、Fedora 42、kernel 4.9.140-l4t+、非root | `1-1.3`、bus1 address68→69 | 64,972,800 bytes / 345,600 packets / 21:55:17.381Z | `switch-hotplug-0651` / `switch-recovery-0654` |
| 今回再検証・合格 | Android aarch64 / Pixel 9a、Android 17 API37、kernel 6.1.162-android14-11-g2ec90535fa34-ab15810641 | `/001/002`→`/001/002`（各回新fd） | 64,758,480 bytes / 344,460 packets / 22:08:13.440Z | `termux-hotplug-20261007T220512Z-e2b200723d28492eb4bc1f1e39d7a6da` / `termux-recovery-20261007T220740Z-04c798cb590346879f0a7daad8f33b43` |
| 今回再検証・合格 | Android armv7a / Google TV Streamer、Android 14 API34、kernel 5.15.180-android14-11-gf55c0c36ffcd-ab13512086 | `/001/015`→`/001/016` | 64,969,040 bytes / 345,580 packets / 22:24:00.738Z | `termux-hotplug-20261007T222058Z-9d8656a8ce0543b998a1d53794df1308` / `termux-recovery-20261007T222328Z-8036c53b8f0447f1a065b5c592e236c1` |
| 今回再検証・合格 | Android x86_64 / Bliss OS、Android 13 API33、kernel 6.1.112-gloria-xanmod1 | `1-2.3`、`/001/009`→`/001/012` | 64,758,480 bytes / 344,460 packets / 22:48:28.639Z | `termux-hotplug-20261007T224527Z-82c5b0e9f66a4d71a9893d27427225ef` / `termux-recovery-20261007T224756Z-b0f14720c3df4b2ab4a0a8c070db4917` |
| 今回再検証・合格 | Linux x86_64 / Latitude 5300、AnduinOS 2.0.4、kernel 7.0.0-34-generic、非root/video | `1-3`、bus1 address92→93、未bind | 64,758,480 bytes / 344,460 packets / 22:53:06Z | `latitude-hotplug-0751` / `latitude-recovery-0752` |
| 今回再検証・合格 | Windows x64 / GEEKOM A6、Windows 11 version10.0.26300.0、WinUSB service、driver6.1.7600.16385 | `5-1.2`、bus5 address3→3 | 64,984,080 bytes / 345,660 packets / 23:08:32.132Z | `windows-hotplug-20261007T230619Z-18b8bef6452041a4aa67a26aa81138dc` / `windows-recovery-20261007T230759Z-ed95bee00e894c8fab5d6c5c7d95804b` |
| 今回再検証・合格 | macOS arm64 / Mac mini、macOS 26.6.2 (25G83)、非root | `2-3`、bus2 address1→1 | 64,758,480 bytes / 344,460 packets / 23:58:36Z | `mac-hotplug-0854` / `mac-recovery-0857` |

全7環境の復帰TSは解析hostへコピーし、両hostのSHA-256一致を確認後、TSDuck `3.45-4798` で全体を検査した。188-byte remainder・invalid sync・TEI・PIDごとのcontinuity discontinuity・suspect ignoredはすべて0。Androidの`mlockall`/realtime scheduling拒否とmacOSの`mlockall`未実装は非致命。Androidで出力詰まりの実機matrixを追加実施したというclaimはしない。

復帰TSのSHA-256は、表の順に `fb35510dba1ccd60260611d1b9d28b61c5114774f8acc54b06cd47147199e254`、`09001e98d97747937cb98466a3a13e6f28dff60fa43c5a1f604e738c7ec3e321`、`24bcd60e14e0405481119388b50d7f63ec41d2cd654e3373c9df0a4b23f24c07`、`b72c92f31d06514b65aab9111dd0a94551a7206ecfce5b5352de90c001fd9680`、`60944b0a1d235332702666fed33781b89d8185476614ef42da77bb088c577998`、`70512ccec9022fb632295346e367c42a7000272a4fc6161bc568e80191a0ed78`。

未完了試行も保持した。Google TVの最初の抜去待ちは利用者の操作が5分に間に合わず、所有PIDを回収したため未完了であり、製品の失敗や合格には数えない（`termux-hotplug-20261007T221020Z-16ee0640971d4823ac503884dee91fdf`）。Blissの最初の起動はkernel driverにbind済みのため、候補が奪取を拒否してlock前に自然exit4（`termux-hotplug-20261007T222842Z-260899a3b3ee4550973300dd94ec40b4`）。その後、利用者がS1UDを外したことを確認してADB rootから `smsdvb`・`smsusb`・`smsmdtv` を順に外し、未bindの再接続で試験した。live unbind・detach flag・再起動・永続設定変更は行っていない。試験後はS1UDを移動してから元の3モジュールを正常ロードし、復元を確認した。ADBのunroot操作は接続喪失で確認できなかったが、08:51 JSTに利用者がBlissをシャットダウン済みと報告した。試験中のADBプロセスは終了しているため、そのプロセスの権限復元確認を目的とする再起動は不要。unrootの成功を確認したという記録にはしない。

### final candidateの追加機能確認

Linux x86_64とaarch64では、校正済みの未読stdout pipeでSIGINT・SIGTERM・time3・control quit・fail-on-dropの5ケースを今回再検証し、全て自然有限終了。x86_64はSIGINT0/25.176ms、SIGTERM0/25.317ms、quit0/75.701ms、time3はdrop1471/exit8、fail-on-dropはdrop1/exit8。aarch64はSIGINT0/25.311ms、SIGTERM0/25.315ms、quit0/50.455ms、time3はdrop1469/exit8、fail-on-dropはdrop1/exit8。time3はいずれもlockから2.5～5秒の範囲。dropを伴うexit8は仕様どおり。

E03/Linux x86_64とE17/Windowsでは同じopenを維持し、`--control -c T27` から20秒間隔でnumeric `channel 22`→`channel 27`→`quit` を送信した。両方でopenは1回、選局成功3回、自然exit0、drop0、残留0。Linuxは `latitude-control-0754`（622,220 packets / SHA-256 `28d8ca138617c4352c9c85191088bbcebcb64e6dac76639f56c6f3b00a90ad27`）、Windowsは `windows-control-20261007T230849Z-84692b6714524f788800714d34bfae9d`（645,960 packets / SHA-256 `273219de4665ccced653d27904fa8413e5f6825c2ff03bfa386c421b4d06ecf1`）。コピーのhash一致とTSDuckのinvalid sync・TEI 0を確認。continuity eventはLinux40件、Windows44件で、PATのTSID変化と各PIDの初出packet位置から、全件が再選局後の各PIDの先頭packetであることを確認した。以後のdiscontinuityは0。規範のretune境界除外を適用した結果であり、raw continuity 0とは表現しない。

この健康なfile出力の実機試験は、100-byte partial writeを強制した試験ではない。その組合せの根拠は、最終実装のPOSIX/Windows offline testとnative/CI testに限る。変更影響は主にC6（出力・drop・停止）、C10（retune）、C12（CLI）で、Windows canonical pinとDarwin runner修正はC3、記録はC13として扱う。

### 利用者が選択した10分soak

2026-10-08 08:12 JSTに利用者が、WindowsとLinux x86_64で各10分の提案を採用した。PX-S1UD 1台をWindowsからLatitudeへ順に移動し、それぞれのfinal archiveのbinary・同梱firmware・`T27`・`-t 600`・通常file出力を使用。各回の全TSをhash一致後にTSDuck `3.45-4798` で検査し、remainder・invalid sync・TEI・continuity discontinuity・dropは0、自然exit0、watchdogなし、残留なし、終了後`--list`はready。helperの資源判定は自動合格にせず、親担当が記録系列を確認した。

| 環境 / port | 受信開始→終了観測UTC（2026-10-07） | TS / SHA-256 | 資源記録と親担当の判定 |
|---|---|---|---|
| Windows / `5-1.2` | 23:18:34.122Z→23:28:39.169Z | 1,295,128,240 bytes / 6,888,980 packets / `ae4f1302c6b5ea3a2a848118025f558da6a6b2866049072080121df48971aee2` | 121点、最大間隔5.016秒。WorkingSet 10,727,424～12,103,680 bytes、private 5,574,656～6,172,672 bytes、handles105～112。最後の約195秒はWorkingSet12,062,720/private6,086,656/handles110で横ばい。初期増加と小幅な上下の後に安定。**安定・合格**。 |
| Linux x86_64 / `1-3`、address94 | 23:34:47.139Z→23:44:49.910Z | 1,295,297,440 bytes / 6,889,880 packets / `1581f247b62213ff79d66868bc0819ce3740f21af5d425bdfe857378e0789125` | 121点、最大間隔5.001秒、取得失敗0。起動直後RSS5,600KiB/FD8から、5秒以降はRSS5,432KiB・VmSize5,476KiB・FD9が最後まで横ばい。**安定・合格**。 |

WindowsのresourceはRSS相当のWorkingSetとhandle数であり、LinuxのFD数とは同一の量ではない。観測lockから終了までWindows603.235秒、Linux600.158秒。前者には5秒間隔の終了観測遅れが含まれ、製品の内部timerを短縮したものではない。RSS/FD/handleの絶対値に新しい合否閾値は設けていない。終了時まで安定化しない持続増加はなかった。

rawは `/config/.work/siano-p0-channel-task/` に保持。soak basenameは `windows-soak-20261007T231834Z-692ca082dd894a1d941169d407c93149` と `linux-soak-20261007T233447Z-3aa6e281c4dd438bb8d99957b57e7038`、各 `.json`・`.samples.jsonl`・`.stderr.log`・`.ts` を保存した。各hostの実行rootは `siano-rc-37691200913`。08:32 JSTのLinux準備ではUSBが見えず試験を開始せず、08:34の利用者の抜き挿し後にreadyを確認して開始した。

macOSの復帰TS SHA-256は `0c4ade7f4d976da6380583a4739977c0ed3904501ee4a98212813c224f52e4da`。切断試験はUTC23:54:47Z→23:56:46Z、PID94481がLIBUSB_ERROR_PIPEから自然exit7。復帰試験は23:58:03Z→23:58:36Z、自然exit0。両試験にtimeout/dropはなく、終了後に製品プロセスなしと再列挙readyを確認した。受信中の別の`--list`でもreadyを確認したため、他プロセス使用中は常に列挙されないとは結論しない。利用者が報告した別プロセス使用時の非表示の原因は未確定。

公開前の記録commit時点で、final archiveの7/7必須実機確認と、利用者が選択した2件のsoakは完了していた。タグ付け・公開と公開後のasset byte一致確認は、以下の公開工程で実施した。

### 公開と公開後照合

2026-10-08 09:03 JSTに利用者が公開を承認した。`v0.1.10` は候補source commit `89c240b8af021d55d81b3b90fce79a3690605811` に付け、UTC00:03:55～00:03:56に[Stable Release](https://github.com/Khronos31/siano-userland/releases/tag/v0.1.10)へcandidate run `37691200913` の8 archiveと`SHA256SUMS`を公開した。再buildは行っていない。公開後に新しい空directory `/config/.work/siano-p0-channel-task/published-v0.1.10-9azQRyle/` へ全9 assetを取得し、`sha256sum -c SHA256SUMS` は8/8成功、candidateとの`cmp`はchecksumファイルを含む9/9 byte一致。タグの参照先も候補source commitと一致した。release notesの検証記録リンクは記録commit `be0f3e6eccb45fb8b366fc3c313d790801275348` に固定している。

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機は未確認。
- Android x86_64はbuild、ELF検査、package監査、再現生成、relink試験に加え、Bliss OS実機試験まで完了。
