# Siano 追加移植性確認手順

Stable release前の必須回帰matrixは [`../release-validation.md`](../release-validation.md) の正式配布archiveに限る。本書はそのmatrix外にあるOS/ABI・access pathの追加移植性確認を保管する。以下は独立した追加確認であり、Stable release gateの頻度・合否条件に自動追加しない。対象となる配布設定・環境固有の変更時、または利用者が追加確認を依頼した場合に使う。結果は [`validation-results.md`](validation-results.md) に該当環境とsource/archiveを分けて記録し、release gate結果と混同しない。

| 環境 | 追加確認の手順 | 適用範囲・根拠 |
|---|---|---|
| Fedora x86_64 / SELinux (E08) | [`fedora-selinux.md`](fedora-selinux.md) の再検証手順に従い、context/label、`ausearch -m AVC`、終了値を記録する。 | Fedora固有の構成・文書・claimを変更した場合。 |
| Gentoo x86_64 (E09) | 記録済み手順なし。追加確認時はexact archiveをOpenRCとudev `usb` group構成で実行し、実施条件を記録する。 | 現在の状態は未確認。記録なし。 |
| NixOS x86_64 (E10) | [`nixos.md`](nixos.md) に従い、2台同時受信とcold boot後のkernel module未ロードを確認する。 | NixOS構成手順の追加確認。 |
| Chimera Linux x86_64 (E11) | [`chimera-linux.md`](chimera-linux.md) に従い、Clang native build、全test、static/dynamic binaryの2台同時受信を確認する。 | Chimera構成手順の追加確認。 |
| Alpine x86_64 / mdev (E12) | [`alpine-mdev.md`](alpine-mdev.md) に従い、`mdev -s`、`siano-ts-mdev --scan`、非rootの2台受信を確認する。 | `packaging/mdev/**` またはmdev固有材料の変更時の追加確認。 |
| OpenWrt x86_64 (E13) | 既存記録 `0315bd5` はexit 1であり現行claimではない。procd起動停止とhotplug権限はOpenWrt手順を新設/変更した場合にだけ確認する。 | 履歴上の失敗を今回candidateの結果にしない。 |
| FreeBSD x86_64 (E14) | 正式配布archiveのnative matrixとは別の追加移植性確認環境として扱う。既存記録は履歴として保持し、今回candidateやcurrent release gateの合格に使わない。 | 現行READMEの対象外。 |
| Debian x86/i386 (E16) | [`debian-i686.md`](debian-i686.md) のnative buildと`make test`、30秒のnonblocking受信、slow-sinkの`--fail-on-drop` / exit 8を確認する。5分を超える負荷試験はユーザーが決めた場合だけ行う。 | i386配布artifactはなく、source-build claimを変更する場合の追加確認。 |

Fedora aarch64 (E15-G) は `linux-aarch64` 正式配布artifactのnative runtimeであるため、この追加確認文書ではなく共通環境表と各版matrixに従う。CARD/PC/SC/WinSCard backendは製品機能外であり、追加移植性試験にも含めない。
