# OS・環境別の検証結果

本ドキュメントは、特定のrevisionにおいて実施した実測記録であり、将来のバージョンやあらゆる動作環境における動作を保証するものではありません。

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

## CIのみ

- Linux x86_64/aarch64 × glibc/muslはbuild、artifact audit、最終archive起動をCIで確認。aarch64/muslのUSB実機は未確認。
- Android x86_64はbuild、ELF検査、package監査、再現生成、relink試験まで確認。Bliss OS実機試験は未完了。

