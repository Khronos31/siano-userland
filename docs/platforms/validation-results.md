# OS・環境別の検証結果

本ドキュメントは、特定のrevisionにおいて実施した実測記録であり、将来のバージョンやあらゆる動作環境における動作を保証するものではありません。

## 2026-09-12 現行CI候補（fix/release-symbol-stripping）

- **ブランチ / コミット**: `fix/release-symbol-stripping`（`a4a914fb5d58da9ef93cfea8172da868d02651d9`、short `a4a914f`）
- **GitHub Actions**: run `34641814344`（全job success）
- **変更内容**: macOSにおけるTS continuity欠落バーストを吸収するため、`siano-ts.c`でmacOSのみ`MAX_URBS=128U`へ変更（他OSは従来の32Uを維持）。Android 3 ABIバイナリおよびLinux static x86_64/aarch64バイナリは直前コミットとbyte-identical。Linux source、最終Linux archive組立、同archiveのUbuntu glibc/Alpine musl runtime smokeが成功。
- **ファームウェア**: Android 3 ABIおよびmacOSの試験で使用したfirmware SHA-256は `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。
- **留意点**: 全プラットフォーム向けの統合 release-candidate archive は未作成。macOSはPX-S1UD実受信（30分ファイル出力および10分named FIFO）を実施し、連続動作・有限終了・同期・TEI正常を確認したが、双方で複数PID同時の短いcontinuity欠落バーストを検出。同一PX-S1UDによるLinux標準カーネルドライバ比較（約6分）でも末尾に複数PID同時の短バースト（9 event / 52 missing packet / 4 PID）が観測されたものの、発生量・対象PID数・位置が同等でなく原因未確定のためStable判定は保留。その後の診断worktree（計測コードは製品ブランチ外）による計装trace試験により、32 transferの実効保持窓（約63.2 ms）に対してstream中に68.666〜95.336 msのcallback gapおよび101.420 msのwriter stallが重なり欠落位置と一致することを確認。macOSのみ`MAX_URBS=128U`（実効保持窓約252.9 ms、他OSは32U維持）とする修正をcommit `a4a914f`としてpushし、CI run `34641814344`で全job successを確認。同修正のローカルMacビルド（SHA-256 `14687b5b...`）では6分および30分named FIFO/TSDuck試験でcontinuity 0・終了後process残留0を確認済みだが、新CI artifactのmacOSバイナリ（SHA-256 `236147...`）とはビルド環境差によりhashが異なるため、CI artifactそのものの実機受信は未実施。32の保持窓不足・host-side servicing gap仮説が強く支持されローカル機能試験は通過したが、firmware/USB内部の無通知欠落等を完全に排除した原因確定とはせず、またCI artifact実受信および完全な最終candidate archiveが未作成のため、プロジェクト全体のStable判定は引き続き保留（partial）。Windowsの実受信も未完了のまま。

| 環境 | arch/libc | 対象バイナリ SHA-256 | 確認内容 | 状態 |
| --- | --- | --- | --- | --- |
| Pixel 9a / Android 17 / Termux | aarch64 / Bionic | `5046f44a2c7b93abe07881bd0937169bd68cec6470d8995501e5832c6306a4ff` | PX-S1UDで地上波T22を受信（約31秒）。64,972,800 bytes / 345,600 packets。sync error 0、TEI 0、continuity error 0。exit 0、終了後process残留なし。 | 実機受信確認済 |
| Google TV Streamer / Android 14 / Termux arm | armv7a / Bionic | `c0e19d928f7d4e26cacf3830f923c94fc041da32824ecbf08fd070413aef54fc` | PX-S1UDで地上波T22を受信（約31秒）。64,758,480 bytes / 344,460 packets。sync error 0、TEI 0、continuity error 0。exit 0、終了後process残留なし。 | 実機受信確認済 |
| Bliss OS / Termux | x86_64 / Bionic | `0db2e15cfaab035e70783645a6132078608581b8335b1acd894e98d694cde353` | 30分受信、signal、物理切断・再接続を検証した実機バイナリとexact match（詳細は既存の追加検証「Bliss OS」行参照）。 | 実機検証済バイナリと一致 |
| M2 Mac mini / macOS | arm64 | `23614737764d6a3ef0a2355b9ed204930e3a830384e31f03aaf09a623da6523f` | current raw artifact保存、SHA確認。旧CIバイナリ（`d9e62767...`）では30分ファイル出力および10分named FIFOで複数PID同時の短いcontinuity欠落バーストを検出し、Linux標準ドライバ比較でも4 PID burstを観測。計装trace診断を経てMAX_URBS=128U修正をcommit/pushしCI run `34641814344`で本バイナリを生成。同修正のローカルMacビルド（`14687b5b...`）では6分および30分named FIFO/TSDuckでcontinuity 0を確認済みだが、本CI artifactそのものの実機受信は未実施。詳細は追加検証を参照。 | CIビルド確認済（修正後CIバイナリの実機受信未実施、Stable保留） |
| Windows | x86_64 | EXE: `d5eb9dbe5a431fb627225471f7dc208992058d26a5ed71b55f2f33959cfb36ed`<br>DLL: `7cbf37e76dae9c840c7e8dbf7348ee8897dcc86c8ba45e46ada60b89411569f7` | current raw artifact保存、SHA確認。実機上の `--help` 起動は旧バイナリで確認済み、PX-S1UD実受信は未完了。 | 実機起動確認済（実受信未完了） |

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
| Latitude 5300 / AnduinOS（Linux標準Sianoドライバ参照比較） | kernel 7.0.0-31-generic | 同一PX-S1UD・T22でLinux標準smsusb/smsdvbによる約6分受信（recisdb 1.2.4、777,060,352 bytes / 4,133,299 packets、末尾140 bytes、SHA-256 `7656a9cf61e6de1da05505d204f5c612ea4bd3155a0826ddf869abfe2e73b892`）。sync loss 0、resync 0、TEI 0、malformed adaptation 0。packet index 4,116,021〜4,116,267（出力停止の約1.5秒前）に4 PID（273, 274, 2112, 2144）のcontinuity missing 9 event（計52 missing packets）を検出。短時間の複数PID burstという形態はmacOS userlandと共通するが、発生量・PID数・位置は同等でない。 | 参考比較用（siano-userlandバイナリではない） |
| M2 Mac mini / macOS 26.6.2（macOS限定 MAX_URBS=128U ローカル修正候補） | commit `a4a914f`（ローカルMac build SHA-256: `14687b5b6a238c6c430fdb6f5c92e3af766c8f7e0e0a1e98c3bca2bf3a2d228c`） | PX-S1UD / T22 / firmware SHA-256 `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`。macOSのみ`MAX_URBS=128U`とする製品ソース修正（他OSは32U維持）からMacローカルでbuild/test。同binaryの6分named FIFO/TSDuckでexit 0/0/0、continuity error 0。30分named FIFO/TSDuckでもexit 0/0/0、continuity error 0、終了後process残留0（TS本体はdrop outputへ送出し保存なし）。先行する診断worktree（`diag/macos-continuity-trace`、計測コードは製品ブランチ外）における32 transfer trace（54 missing events / 259 missing packets / 14 PID、実効保持窓約63.2 msに対しstream中gap 68.666〜95.336 msおよびwriter stall 101.420 msが欠落位置と整合）と、128 transfer trace（実効保持窓約252.9 ms、起動前gap最大217.460 ms、stream中gap最大122.755 ms・writer stall最大122.622 ms下でもTSDuck欠落0）の対照試験により、32の保持窓不足・host-side servicing gap仮説が強く支持された。本修正はcommit `a4a914f`としてpush・CI成功したが、新CI artifact（`236147...`）そのものの実機受信は未実施。 | ローカル修正ビルドで機能通過（原因確定とはせず）。CI成果物の実機受信および完全な最終candidate archiveが未作成のため、Stable全体は保留（partial）を維持。 |

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機は未確認。
- Android x86_64はbuild、ELF検査、package監査、再現生成、relink試験に加え、Bliss OS実機試験まで完了。
