# OS・環境別の検証結果

本ドキュメントは、特定のrevisionにおいて実施した実測記録であり、将来のバージョンやあらゆる動作環境における動作を保証するものではありません。

## 2026-09-12 現行CI候補（fix/release-symbol-stripping）

- **ブランチ / 検証対象コミット**: ブランチ `fix/release-symbol-stripping`、build / baseline candidate commit `ee5c6b9047f5000b1ddc0c45792900b71d75ae42`（short `ee5c6b9`、Windows baseline固定）、reproducible build commit `d418f8bc85d3226a66ee1633044d6f282731af14`（short `d418f8b`）、runtime code candidate `2cd91cd5681742603d07d3dd62d747eb2be3c7b0`（short `2cd91cd`）
- **GitHub Actions**: run `34654669361`（commit `ee5c6b9`）/ run `34654057294`（commit `d418f8b`）（いずれも全job success）
- **変更内容**:
  - macOSにおけるTS continuity欠落バーストを抑止するため、`siano-ts.c`でmacOSのみ`MAX_URBS=128U`へ変更（他OSは従来の32Uを維持、commit `a4a914f`）。
  - Windows環境でのQPC（QueryPerformanceCounter）からtimespecへの変換において、`counter * 1000000000` のuint64オーバーフローにより単調時計および受信時間（--time）判定が壊れる不具合を修正。回帰テストを追加（commit `2cd91cd`）。
  - WindowsバイナリのPEタイムスタンプ差を解消するため、MSVCビルドに`/Brepro`を導入し、クリーン2回ビルド全バイト一致およびベースライン照合（`packaging/windows-baseline.sha256`）を行う再現可能ビルドを導入（commit `d418f8b`、`ee5c6b9`）。同一run内再現性とrun間一致（cross-run identity）を確認。
  - 非Windows成果物（macOS arm64、Linux static x86_64/aarch64、Android 3 ABI）およびWindows `libusb-1.0.dll` はbyte-identical（同一バイナリ）。
- **ファームウェア**: Android 3 ABI、macOS、Windowsの試験で使用したfirmware SHA-256は `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。
- **検証概要**:
  - **macOS**: M2 Mac mini / macOS 26.6.2 / PX-S1UD / T22にて、CI生成macOSバイナリ（SHA-256 `23614737...`）の実機検証を実施。30秒受信および30分連続受信にてTSDuck continuity error 0、queue drop/libusb errorなし、正常終了を確認。受信中USB物理切断時の有限終了（LIBUSB_ERROR_IO）、再接続復帰、SIGINT正常終了（1秒以内）を確認。
  - **Windows**: GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343) / PX-S1UD (WinUSB) にて、再現可能ビルドexact CI artifact（実機試験元: run `34654057294` / commit `d418f8b`、commit `ee5c6b9` / run `34654669361`はcross-run byte-identical；EXE SHA-256 `dbdf7d69...`、DLL SHA-256 `7cbf37e7...`）の実機検証を実施。地上波T22を30秒受信し、64,769,760 bytes / 344,520 packets、alignment 0、sync 0、TEI 0、continuity error 0、malformed 0、exit 0、process残留0を確認。同一runtime sourceにおいて、先行するcommit `2cd91cd`の旧EXEによる30分連続受信（--time 1800にて約1800秒で自然終了、lock取得、queue drop/USB error 0、process残留0）、Ctrl+C終了、物理切断有限終了（LIBUSB_ERROR_PIPE）、再接続復帰（30秒TS取得exit 0、continuity 0）が検証済み。
  - **Android**: Android 3 ABIバイナリ（Pixel 9a aarch64、Google TV Streamer armv7a、Bliss OS x86_64）は検証済みバイナリとbyte-identical。
  - 全プラットフォーム向けの統合 release-candidate archive の作成および最終ガバナンスは今後実施。

| 環境 | arch/libc | 対象バイナリ SHA-256 | 確認内容 | 状態 |
| --- | --- | --- | --- | --- |
| Pixel 9a / Android 17 / Termux | aarch64 / Bionic | `5046f44a2c7b93abe07881bd0937169bd68cec6470d8995501e5832c6306a4ff` | PX-S1UDで地上波T22を受信（約31秒）。64,972,800 bytes / 345,600 packets。sync error 0、TEI 0、continuity error 0。exit 0、終了後process残留なし（バイナリ一致により証跡継承）。 | 実機受信確認済 |
| Google TV Streamer / Android 14 / Termux arm | armv7a / Bionic | `c0e19d928f7d4e26cacf3830f923c94fc041da32824ecbf08fd070413aef54fc` | PX-S1UDで地上波T22を受信（約31秒）。64,758,480 bytes / 344,460 packets。sync error 0、TEI 0、continuity error 0。exit 0、終了後process残留なし（バイナリ一致により証跡継承）。 | 実機受信確認済 |
| Bliss OS / Termux | x86_64 / Bionic | `0db2e15cfaab035e70783645a6132078608581b8335b1acd894e98d694cde353` | 30分受信、signal、物理切断・再接続を検証した実機バイナリとexact match（詳細は既存の追加検証「Bliss OS」行参照、バイナリ一致により証跡継承）。 | 実機検証済バイナリと一致 |
| M2 Mac mini / macOS 26.6.2 | arm64 | `23614737764d6a3ef0a2355b9ed204930e3a830384e31f03aaf09a623da6523f` | PX-S1UDで地上波T22を受信。exact CI artifactにて30秒受信および30分連続受信（drop output）を実施し、TSDuck continuity error 0、queue drop/libusb error 0、exit 0、process残留0。受信中物理切断時の有限終了（LIBUSB_ERROR_IO）、再接続復帰、SIGINT（1秒以内exit 0）を確認。 | 実機受信確認済 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343) | x86_64 / Windows (WinUSB) | EXE: `dbdf7d6912ca60ffdce35bf65f5221a89d1a19c45b89bd32a04a86607995dc7f`<br>DLL: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7` | PX-S1UDで地上波T22を受信。再現可能ビルドexact CI artifact（実機試験元: run `34654057294` / commit `d418f8b`；commit `ee5c6b9` / run `34654669361`はcross-run byte-identical）にて30秒受信（64,769,760 bytes / 344,520 packets、alignment/sync/TEI/continuity/malformed 0、exit 0、process残留0）を確認。同一runtime sourceにおいて、先行する旧EXE（commit `2cd91cd`）による30分連続受信（約1800秒自然終了、drop/error 0）、Ctrl+C終了、物理切断有限終了（LIBUSB_ERROR_PIPE）、再接続復帰（30秒TS取得exit 0、continuity 0）が検証済み。 | 実機受信確認済 |


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
| Fedora 44 | `0315bd5609422748293bdde63e760c9e02a9cef8` | x86_64 / glibc / SELinux Enforcing。一般ユーザーでPX-S1UDの地上波27chを30秒受信、64,972,800 bytes / 345,600 packets、188-byte remainder 0、sync error 0、AVC拒否0。 | — |
| FreeBSD 15.1-RELEASE | — | amd64。ソース修正なし、native buildと全testを実施。PX-S1UD 2台の単独・同時受信を確認。単独TSのalignment/sync/TEI 0、同時受信は両方exit 0。 | 現行Release対象外。 |
| OpenWrt 25.12.5 | `0315bd5609422748293bdde63e760c9e02a9cef8` | x86_64 / musl 1.2.5 / procd。static/stripped成果物を使用。USB `3275:0080`。PX-S1UDで地上波27chを10秒受信、21,804,240 bytes / 115,980 packets、alignment/sync/TEI/continuity error 0。受信中切断で`LIBUSB_ERROR_IO`・exit 1・ハングなし。再接続後1回目の列挙で復帰し同条件の受信成功。 | OpenWrt向けソース修正なし。 |
| Bliss OS（Android 13 API 33 / x86_64 / Bionic） | `2b73ec58a756d49b96402cc6f4c7a201132ffddc` | PX-S1UDを使用。CI成果物で地上波27chを10秒受信し21,800,480 bytes / 115,960 packets、native buildと全test成功後のnative binaryで30秒受信し64,972,800 bytes / 345,600 packets。いずれもalignment/sync/malformed/TEI/continuity error 0。受信中切断は`LIBUSB_ERROR_IO`・exit 1で有限終了し、プロセス残存なし。OS再起動なしの再接続後も受信成功。 | IP3 GT1、kernel 6.1.112-gloria-xanmod1、Termux 0.118.3。 |
| Latitude 5300 / AnduinOS（Linux標準Sianoドライバ参照比較） | kernel 7.0.0-31-generic | 同一PX-S1UD・T22でLinux標準smsusb/smsdvbによる約6分受信（recisdb 1.2.4、777,060,352 bytes / 4,133,299 packets、末尾140 bytes、SHA-256 `7656a9cf61e6de1da05505d204f5c612ea4bd3155a0826ddf869abfe2e73b892`）。sync loss 0、resync 0、TEI 0、malformed adaptation 0。packet index 4,116,021〜4,116,267（出力停止の約1.5秒前）に4 PID（273, 274, 2112, 2144）のcontinuity missing 9 event（計52 missing packets）を検出。短時間の複数PID burstという形態はmacOS userland（旧32 transfer時）と共通するが、発生量・PID数・位置は同等でなく原因は未特定。本比較時点ではmacOS判定を保留としたが、後続の転送深度調査（MAX_URBS=128U）およびexact CI artifact検証によりmacOSの実機検証は完了した。 | 参考比較用（siano-userlandバイナリではない） |
| M2 Mac mini / macOS 26.6.2（macOS限定 MAX_URBS=128U exact CI artifact） | commit `2cd91cd` / `a4a914f`（CI build SHA-256: `23614737764d6a3ef0a2355b9ed204930e3a830384e31f03aaf09a623da6523f`） | PX-S1UD / T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。macOSのみ`MAX_URBS=128U`とする修正後のexact CI成果物を使用。30秒受信（source/TSDuck/wait exit 0/0/0、continuity error 0）、30分連続受信（exit 0/0/0、TSDuck continuity error 0、queue drop/libusb errorなし、process残留0）。受信中物理切断時の有限終了（LIBUSB_ERROR_IO、exit 1、残留0）、再接続復帰、SIGINT（1秒以内exit 0）を確認。 | exact CI artifact実機検証完了。 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343)（Windows単調時計修正 exact CI artifact） | commit `2cd91cd`（CI build EXE SHA-256: `b9a90cb61127efeb7d64d96d259fe00c68be7da1e214bfb67b297fe34d66ffe6`、DLL SHA-256: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7`） | PX-S1UD（WinUSB）/ T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。単調時計修正後のexact CI成果物を使用。30秒smoke（exit 0）、30分soak（--time 1800にて約1800秒で自然終了、lock取得、queue drop/USB error 0、process残留0）、Ctrl+C終了、受信中物理切断時の有限終了（LIBUSB_ERROR_PIPE、exit 1、残留0）、再接続復帰（30秒TS取得exit 0、TSDuck continuity error 0）を確認。 | exact CI artifact実機検証完了。 |
| GEEKOM A6 / Windows 11 Pro Insider Preview 25H2 (build 26220.9343)（Windows再現可能ビルド exact CI artifact） | commit `d418f8b`（実機試験元、run `34654057294`）/ commit `ee5c6b9`（baseline固定、run `34654669361`、cross-run byte-identical）（CI build EXE SHA-256: `dbdf7d6912ca60ffdce35bf65f5221a89d1a19c45b89bd32a04a86607995dc7f`、DLL SHA-256: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7`） | PX-S1UD（WinUSB）/ T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。MSVC `/Brepro`、2回クリーンビルド全バイト一致、ベースライン照合済みのexact CI成果物を使用。実機試験を実施した新exact artifactはrun `34654057294`（commit `d418f8b`）生成物（commit `ee5c6b9` / run `34654669361`とcross-run byte-identical）。30秒受信にて64,769,760 bytes / 344,520 packets、alignment 0、sync 0、TEI 0、continuity error 0、malformed 0、exit 0、process残留0を確認。同一runtime sourceに対しcommit `2cd91cd`旧EXEで30分soak検証済み。 | exact CI artifact実機検証完了。 |


## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機は未確認。
- Android x86_64はbuild、ELF検査、package監査、再現生成、relink試験に加え、Bliss OS実機試験まで完了。
