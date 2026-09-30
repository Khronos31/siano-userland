# Stable リリース前検証手順

Status: v1 (2026-10-01)

この文書は `siano-userland` の Stable リリースごとの実行順と、変更内容に応じた検証の選び方を定める規範文書である。`README.md` の CLI・終了コードと実装が事実の正本であり、本書の記載と食い違う場合は両者を確認して先に本書を直す。全 OS・全機種を毎回回帰する手順ではない。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 変更記録

- v1 (2026-10-01): 初版。canary と long soak を分離し、変更class C1–C13、canonical 環境ID E01–E17、単一OS選定、記録先と語彙を定める。long soak は trigger 成立時のみ、release あたり最大1 OS/runtime。FD数・RSS は記録と傾向のみで数値合否基準を設けない。APK は dtv-android 所管として対象外、FreeBSD は対象外。

## 0. 環境IDと状態語彙

canary・long soak は次の canonical 環境ID E01–E17 で選択する。AppArmor、usbip、SELinux、mdev などの変化は独立した環境IDにせず、その環境の access path note として記録する。

| ID | 環境 | siano の位置づけ | 主 artifact / doc |
|---|---|---|---|
| E01 | HAOS x86_64 Debian/glibc SCS | canonical runtime（`linux-x86_64`、container root）。production host | §7 / [validation-results.md](platforms/validation-results.md) |
| E02 | HAOS x86_64 Alpine/musl add-on | canonical runtime（`linux-x86_64` と同一 static binary、Supervisor add-on）。production host | §7 / validation-results.md |
| E03 | AnduinOS x86_64 | canonical Linux x86_64 runtime | §7 / validation-results.md |
| E04 | macOS arm64 | canonical runtime（`darwin-arm64`） | §7 / validation-results.md |
| E05 | Android Termux aarch64 | canonical runtime（`android-aarch64`、1-FD） | §7 / validation-results.md |
| E06 | Android Termux armv7a | canonical runtime（`android-armv7a`、1-FD、唯一の配布32-bit） | §7 / validation-results.md |
| E07 | Bliss OS x86_64 Termux | canonical runtime（`android-x86_64`、1-FD） | §7 / validation-results.md |
| E08 | Fedora x86_64（SELinux） | historical-only / conditional | §7 / [Fedora SELinux](platforms/fedora-selinux.md) |
| E09 | Gentoo x86_64 | unverified（記録なし） | §7 |
| E10 | NixOS x86_64 | historical-only / conditional | §7 / [NixOS](platforms/nixos.md) |
| E11 | Chimera Linux x86_64 | historical-only / conditional | §7 / [Chimera](platforms/chimera-linux.md) |
| E12 | Alpine x86_64（mdev） | historical-only / conditional（`packaging/mdev/**` 変更時） | §7 / [mdev](platforms/alpine-mdev.md) |
| E13 | OpenWrt x86_64 | historical-only / conditional | §7 / validation-results.md |
| E14 | FreeBSD x86_64 | 対象外（README 対象外。記録は履歴のみ） | 対象外 |
| E15 | Fedora aarch64（L4T kernel） | canonical runtime（`linux-aarch64` の実機claim環境）。historical record のみ | §7 / validation-results.md |
| E16 | Debian x86/i386 | source-build-only（i386 配布 artifact なし） | §7 / [Debian i386](platforms/debian-i686.md) |
| E17 | Windows 11 x86_64 | canonical runtime（`windows-x64`、WinUSB）。product 対象 | §7 / README |
| — | Android ad-hoc APK | 対象外（dtv-android 所管） | 対象外 |

用語:

- **canonical runtime**: その artifact の実機検証を代表する環境。同一 static binary を共有する runtime（E01/E02/E03/E08–E13）でも access path が違えば証拠は自動継承しない。
- **conditional**: その環境に固有の材料（配布設定、構成手順）を変更したときだけ実行する。
- **source-build-only**: 配布 artifact を持たず、source archive の native build だけで記録する。
- **historical-only**: 過去の記録だけがあり current claim ではない。
- **unverified**: 記録がなく current claim がない。

証拠の軸は model/device profile（PX-S1UD、port path で個体を識別）、runtime/access path、feature/path、artifact/source、test context とする。PX-S1UD は serial を持たないため、個体は USB `port=`（または `bus:address`）で識別し、記録に残す。継承は同一 model・同一 runtime/access path・同一 feature について、対象 archive の bytes が baseline と同一であるか、変更の影響がないと証明できる場合に限る。未知の影響は affected として扱う。観測された失敗は原因を切り分けるまで、その host/device tuple に限定して扱う。

## 1. 候補と baseline を固定する

1. 候補の `VERSION`、source commit、`release.yml` の run を特定する。dirty な作業ツリーや未 push の変更を候補として試験しない。
2. baseline は完全な記録を持つ直近 Stable とする。現在は v0.1.5（その後 v0.1.6–v0.1.9 の tag はあるが実機記録が v0.1.5 で止まっている）。baseline 以後の累積差分を `git diff --stat <baseline>..<candidate>` で把握する。
3. 各 archive の member（`siano-ts`、`libusb-1.0.dll`、`firmware/isdbt_rio.inp`、mdev 等）を baseline の実機検証 artifact と比較する。**byte-identical な archive はその実機証拠を直接継承する**。byte が異なる場合は §4 の class で hunk 単位に分類する。
4. 対象 claim ごとに `継承` / `今回再検証` / `未認定` / `対象外` を根拠付きで記録する。時間経過・release 回数だけを理由にした再検証はしない。

## 2. source / CI / candidate artifact gate（毎回）

1. 候補 commit に対応する `release.yml` を確認する。path filter 等で未実行なら `workflow_dispatch` で候補 ref を指定して実行する。
2. offline の `make test`、各 target build、`audit-artifact`、source archive 監査、`test-static-relink`、`packaged-runtime-*` smoke がすべて成功していることを確認する。失敗 job を無視して先へ進まない。
3. `release-candidate` artifact が次の7 binary archive と1 source archive（計8 archive）および `SHA256SUMS` を含むことを確認し、checksum を照合する。
   - `siano-ts-<version>-linux-x86_64.tar.gz`
   - `siano-ts-<version>-linux-aarch64.tar.gz`
   - `siano-ts-<version>-darwin-arm64.tar.gz`
   - `siano-ts-<version>-android-aarch64.tar.gz`
   - `siano-ts-<version>-android-armv7a.tar.gz`
   - `siano-ts-<version>-android-x86_64.tar.gz`
   - `siano-ts-<version>-windows-x64.zip`
   - `siano-ts-<version>-source.tar.gz`
4. 候補 commit が変わったら、その commit の CI と artifact を取り直す。PR run や旧 candidate、手元 build を代用しない。

```sh
gh workflow run release.yml --ref <candidate-ref>          # 未実行のときのみ
gh run list --workflow release.yml --limit 10
gh run view <run-id> --json headSha,conclusion
gh run download <run-id> --name release-candidate --dir <new-empty-dir>
(cd <new-empty-dir> && sha256sum -c SHA256SUMS)
```

CI が行う build・audit・smoke を成功後にローカル再実行しない。新しい汎用検証スクリプトは作らない。

## 3. exact-candidate hardware canary（毎回）

1. §2 で固定した final candidate artifact そのものを使う。archive 名と SHA-256、source commit、model/USB `port=`、host OS/version、runtime、artifact member SHA-256 を記録する。firmware は archive 同梱で `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818` を確認する。
2. canary は release ごとに1回、10分以上行う。実施環境は次の順で選ぶ。
   - 影響する release artifact がない場合（docs/license/package metadata のみ、または対象 artifact が baseline と byte-identical）: **E03** で PX-S1UD 2台を port 選択して同時受信する。
   - 影響する artifact がある場合: class C1 / C7 / C8 / C11 のいずれかが選ばれたときは **2台**（SI-2）、それ以外は **1台**（SI-1）。該当する OS/access path の環境に絞り、**E03、E01、E02、E15、E04、E05、E06、E07、E17** の順で最初のものを用いる。
3. canary の項目: `--list`、10分以上の受信（S-RX）、SIGINT 終了、2台選定時は S-DUAL、C2/C8 選定時は切断（S-DISC）、C10 選定時は `--control`。TS は §6 の方法で検査し、queue drop（exit 8）がないことを確認する。
4. 終了後に `siano-ts` プロセスの残存、出力ファイルのクローズ、`--list` の `ready` 復帰を確認する。旧 candidate や異なる artifact の結果を今回の canary に数えない。

canary は release gate であり、それだけで全機種・全 runtime/access path の claim を更新しない。

## 4. 変更class と targeted qualification

v0.1.5..v0.1.9 のように差分が大きい場合は、変更したファイル名だけでなく hunk 単位で分類する。`siano-ts.c` の `#ifdef _WIN32` / `#if defined(__APPLE__)` / `SIANO_HAVE_WRAP_SYS_DEVICE` など platform guard 内の hunk はその platform のみに作用する。v0.1.5..v0.1.9 には platform-guarded change（`__APPLE__` の `MAX_URBS`、`_WIN32` の `--fd` 拒否、`control-input.c` の Windows pipe 等）が含まれるため、必ず guard ごとに判定する。

| # | class（現行コード例） | 必要な short test | short test の環境 | S1UD 台数 | soak trigger (T1) |
|---|---|---|---|---|---|
| C1 | USB transport: ring/transfer size、event thread、async completion（`siano-ts.c` の `MAX_URBS` 等） | 10分以上受信、切断 | guard があればその platform（`__APPLE__`→E04）。なければ E03 | 2 | **yes** |
| C2 | libusb version、static/dynamic link、link option | 列挙、firmware、10分以上受信、切断/再接続 | rebuild された binary の環境。`linux-aarch64`→E15 | 1（列挙が変わるなら2） | **libusb version か link 方式の変更時のみ yes**。link option のみは no |
| C3 | compiler/toolchain/SDK/NDK/runner image、`packaging/windows-baseline.sha256` 変更 | 10分以上受信、SIGINT、切断 | rebuild された binary の環境 | 1 | no |
| C4 | platform shim: `siano-os.h`、Windows pipe（`control-input.c`）、QPC clock、`--fd`/wrap | shim された feature | Windows→E17。`--fd`/wrap→E05 + E06 + E07 と E03（Termux 3 ABI は artifact が別個） | 1 | no |
| C5 | firmware hash/format/load | firmware load、lock、10分以上受信 | E03 + E04 | 1 | no |
| C6 | stream/queue/writer/drop policy、`--fail-on-drop`、clock/`--time`（`queue-policy.c`、`write-policy.h`、`stream-state.c`） | counter、exit 8 semantics、10分以上受信、slow sink | E03 + E16 | 1 | **yes** |
| C7 | device enumeration/selection、`--list`、USB location（`device-selector.c`、`usb-location.h`） | `--list` 2台、index/port/bus:address 選択、不正 selector→exit 3 | E03 + E04 + E17 | 2 | no |
| C8 | disconnect/hotplug 分類、exit code（`stream-state.c`、`exit-codes.c`） | 受信中に片側切断→有限時間で exit 7、残存側 exit 0、再接続、再実行。1台/1 USB port しかない環境では有限 exit と再接続だけを確認でき、残存側 exit 0 は2台前提のため満たせない | E03 + E01 + E02 + E04 + E17 + E05 + E06 + E07（Termux 3 ABI は artifact が別個）。E13/E15 は claim 表示時。E16 は source-build-only で release artifact・current hardware claim がないため対象外（platform-specific な diff が i386 を直接対象とする場合を除く） | 2（Termux/E17 は port が1つなら1。1台 run は full 2台 disconnect test として数えない） | no |
| C9 | kernel-driver detach decision（`detach-decision.h`） | bound 時の拒否→exit 4。live unbind はしない | E03（blacklist 状態は reboot で復元） | 1 | no |
| C10 | `--control` parsing と retune | stdin `channel`/`tune`/`quit`、open 中の retune、exit 0 | E03 + E17 | 1 | no |
| C11 | access-setup 材料: `packaging/mdev/*`、docs の udev/OpenRC/procd 記述 | node 権限、非root 実行 | E12（mdev）または記述を変えた環境 | 2 | no |
| C12 | `tests/test_cli.sh` と unit test で覆われる CLI/引数変更 | canary のみ | — | — | no |
| C13 | docs、license、metadata | gate のみ | — | — | no |

platform-only の hunk は、その platform の環境だけで short test する。class が不明または複数にまたがる場合は affected として、影響し得る最小の path 集合を対象にする。

## 5. long soak の trigger と単一OS選定

long soak は毎 release の gate ではない。次のいずれかが成立したときだけ行う。

- **T1（長時間挙動変更）**: C1（transport）、C6（stream/queue/writer/drop）、または C2 のうち libusb version / link 方式が変わった場合、または変更の影響が unknown/ambiguous の場合。1つの class でも複数でも、release あたり **1回** の soak とする。
- **T2（異常）**: canary または short test の counter が、その path の baseline より悪化した場合（baseline で 0 の sync/TEI/continuity、queue drop、exit 8 等）。
- **T3（未認定 claim）**: 新規または拡張した長時間 claim（新 profile/runtime/feature）の認定。

時間経過・release 回数だけを理由にした trigger は設けない。T1–T3 のいずれも成立しなければ soak を行わない。RSS と FD 数は記録・傾向の対象であり、その増加それ自体は soak の trigger にしない。

### 単一OS選定

soak を実施する場合、release あたり1つの runtime/access path だけを選ぶ。

1. 変更が作用する path-specific な環境だけに絞る。guard があればその platform。T2 なら異常を観測した path。
2. 要求 topology（2台運用なら2台）を安全に実行できない環境を除く。
3. 残った中で canonical 環境順 **E03、E01、E02、E15、E04、E05、E06、E07、E17** の先頭を選ぶ。

選択しなかった影響環境は、その環境に該当する short test だけを行い、記録に `単一OS規則によりsoak非該当（選定=E-ID）` と理由を記録する。

### soak の内容

- 1台（SI-1）または2台（SI-2）で30分間連続受信する。2台を同時に使うのは、topology・device 選択・concurrency の変更が要求する場合だけ。それ以外は1台。
- T1 の class が C1 を含む場合は2台（SI-2）を選ぶ。
- 10秒以下の間隔で RSS と FD 数を記録し、傾向を示す。**数値の合否基準は設けない**。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。
- 各 cycle の出力を §6 で検査する。終了後にプロセス残存と `--list` の `ready` を確認する。
- S1UD は `port=` で個体を選ぶ。同一 host で2台を同時運用できることを確認できない場合、その host は選定から除く。

## 6. TS 検査

`siano-ts` は in-band の TS 整合性 counter を持たない。出力ファイルを解析 host へコピーし、両 host で SHA-256 が一致することを確認してから検査する。TS 検査に使う TSDuck の version を固定して記録し、`tsanalyze` の field 名を採用時に1回確認する。

1. `stat`（Linux）/ `stat -f`（macOS/BSD）で `size % 188 == 0`（188-byte remainder なし）。
2. `tsanalyze --normalized <file>` で invalid sync 0、TEI 0、PID ごとの continuity discontinuity 0。各 PID の先頭 packet、および `--control` の retune 境界2点は除外する。
3. `TS queue dropped` が stderr になく、exit code が 8 でないこと。正常は exit 0。

## 7. 環境別手順（E01–E17）

各項は次の共通部品と追加手順で構成する。物理操作（USB 抜差し、アンテナ、電源、WinUSB 割当、kernel module blacklist 変更）はユーザーが事前確認のうえ実施する。E01/E02 は production host であり、Supervisor 状態変更は承認・退避・復元を伴う。HA Core・アドオン・mirakc・サービスを再起動しない。

### 7.1 共通部品

```sh
D=<extracted dir>; B=$D/siano-ts; FW=$D/firmware/isdbt_rio.inp; LOG=<log dir>
sha256sum "$FW"                       # 054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818
"$B" --list > "$LOG/list.txt"          # model=PX-S1UD usb=3275:0080 ... status=ready receivers=1、port= を記録
```

```sh
# S-RX（$S 秒受信。$sel は port または bus:address）
"$B" -F "$FW" -d "$sel" -c 22 -t "$S" -o "$LOG/si.ts" 2> "$LOG/si.err"; echo $? > "$LOG/si.rc"
# S-ANA: §6 の 1–3 を si.ts に適用
```

```sh
# S-CTRL（open のまま retune）
{ sleep 20; echo 'channel 21'; sleep 20; echo 'channel 22'; sleep 20; echo quit; } \
  | "$B" -F "$FW" -d "$sel" --control -c 22 -o "$LOG/ctrl.ts" 2> "$LOG/ctrl.err"; echo $?
```

```sh
# S-SIG
"$B" -F "$FW" -d "$sel" -c 22 -o "$LOG/sig.ts" & p=$!; sleep 30; kill -INT "$p"; wait "$p"
# S-RESID: pgrep -fl '[s]iano-ts' が空、出力ファイルがクローズ済み、--list が ready
```

```sh
# S-DUAL（2台。$portA/$portB を明示）
"$B" -F "$FW" -d "$portA" -c 22 -t "$S" -o "$LOG/a.ts" 2> "$LOG/a.err" &
"$B" -F "$FW" -d "$portB" -c 21 -t "$S" -o "$LOG/b.ts" 2> "$LOG/b.err" &
wait
# 各 process が意図した node を持つことを /proc/<pid>/fd と /dev/bus/usb/BBB/AAA の対応で確認する
```

```sh
# S-DISC（物理。ユーザーが実施）
# S-DUAL 中に device A を抜く。A は有限時間で exit 7、B は受信を継続して exit 0。
# 再接続（reboot なし）後、--list → 60秒 S-RX で復帰確認。
# 1台/1 USB port しかない環境（例: Termux、Windows）では、その device の有限 exit と再接続だけを
#   確認できる。残存側 exit 0 は2台前提のため満たせず、1台 run を full 2台 disconnect test と数えない。
```

### 7.2 環境別

- **E01 HAOS x86_64 Debian/glibc SCS**: container root で実行し、そのことを記録する（非root claim に使わない）。`ha core info` で HAOS/kernel を記録。`linux-x86_64` を使い、base は §7.1。既存記録は revision がなく current claim ではない。
- **E02 HAOS x86_64 Alpine/musl add-on**: add-on options を現行値で退避し、ユーザー承認のうえ試験用 options で起動、終了後に復元・停止する。restart loop がないことを確認。base は §7.1。`linux-x86_64` と同じ static binary を使う。
- **E03 AnduinOS x86_64**: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し `lsusb -t` で未bind を確認。udev rule と `video` グループで非root 実行。base は §7.1。C9 は bound 状態で `--detach-kernel-driver` なしの拒否（exit 4）だけを確認し、live unbind はしない。blacklist の変更はユーザー確認のうえ reboot で復元する。canonical Linux、canary と soak の第一候補。
- **E04 macOS arm64**: `otool -L ./siano-ts` が system library のみであること、`sw_vers` を記録。base は §7.1。`darwin-arm64`。
- **E05–E07 Android Termux**: `termux-usb -r -e "$B -F $FW -c 22 -t $S -o $LOG/si.ts" /dev/bus/usb/BBB/AAA` を使う。Termux/Android の version を記録。E06 は armv7a、E07 は Bliss OS x86_64 で、E07 は検証中だけ画面常時点灯と Termux 前面表示を使い `stay_on_while_plugged_in` を元へ戻す。凍結した run は invalid として pass/fail に数えない。`--control` は stdin をファイルから渡す。
- **E08 Fedora x86_64（SELinux）**: [Fedora SELinux](platforms/fedora-selinux.md) の再検証手順（context、label、`ausearch -m AVC`、exit code）に従う。この環境の構成・再検証手順を含む Fedora SELinux 文書、または Fedora に関する in-repo 材料を変更したとき、もしくは Fedora claim を変更するときのみ実施する。
- **E09 Gentoo x86_64**: 記録がない。Gentoo 手順を新設・変更した場合に exact archive を OpenRC と udev `usb` グループ構成で実施し、doc を作る。それ以外は未確認。
- **E10 NixOS x86_64**: [NixOS](platforms/nixos.md) に従い 2台同時受信と cold boot 後の module 未ロードを確認。構成手順の変更時のみ。
- **E11 Chimera Linux x86_64**: [Chimera](platforms/chimera-linux.md) に従い Clang native build、全 test、static/dynamic binary の 2台同時受信。構成手順の変更時のみ。
- **E12 Alpine x86_64 / mdev**: [Alpine/mdev](platforms/alpine-mdev.md) に従い `mdev -s`、`siano-ts-mdev --scan`、非root 2台受信。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。
- **E13 OpenWrt x86_64**: 既存記録は `0315bd5` で exit 1（変更前コード）。current claim にしない。OpenWrt 手順を新設・変更した場合にだけ procd 起動停止・hotplug 権限を確認する。
- **E14 FreeBSD x86_64**: 対象外。記録は履歴のみで、gate・canary・soak に含めない。
- **E15 Fedora aarch64（L4T）**: `linux-aarch64` の canonical runtime。`uname -r` と `getenforce` を記録し、S-RX・S-DUAL・S-DISC を実施する。claim は特定 kernel に限定する。既存記録 `b2c9466` は履歴。
- **E16 Debian x86/i386**: 配布 artifact はない。source archive を native build して `make test`、nonblocking sink での10分以上の受信（S-RX、必要なら S-DUAL）、slow-sink の `--fail-on-drop`/exit 8 確認を行う（[Debian i386](platforms/debian-i686.md)）。これは short test であり soak の選定対象ではない。
- **E17 Windows 11 x86_64**: product 対象。zip を展開し、WinUSB 割当後に zip ルートの `siano-ts.exe` を使う。OS build と Zadig/WinUSB version を記録し、`$LASTEXITCODE` で終了コードを取る。EXE SHA を `packaging/windows-baseline.sha256` と照合する。S-CTRL はコンソール stdin から渡す。

## 8. 切断時の exit code（事実）

現行 v0.1.9 では、受信中の fatal transfer failure を `stream-state.c` の `siano_stream_disconnect_error()` が `LIBUSB_ERROR_IO` / `LIBUSB_ERROR_PIPE` / `LIBUSB_ERROR_NO_DEVICE` から `LIBUSB_ERROR_NO_DEVICE` に写像し、`exit-codes.c` が exit 7 を返す。README の「受信中の USB 切断 = exit 7」はこの受信経路について現行実装と一致しており、狭めない。v0.1.5 の記録にある exit 1 は変更前コード（`exit-codes.c` は v0.1.8 `d333ff5`、`IO`/`PIPE` の写像は v0.1.9 `9883025` で導入）の測定である。ただし v0.1.5 以降の exact candidate で受信中の切断を再試験していないため、disconnect の実機 claim は `未認定` とし、最初の C8 選定時に retest して exit 7 を確認する。

## 9. 記録

検証結果は [`platforms/validation-results.md`](platforms/validation-results.md) へ日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。release record には version、commit、workflow run、8 archive の checksum と audit、baseline tag、各 artifact の byte-identity 判定、hunk-level の class と影響、claim ごとの `継承` / `今回再検証` / `未認定` / `対象外`、canary/soak の選定理由（環境ID、固定順の位置、単一OS規則による非該当）、各 test の環境ID・host・USB `port=`・artifact SHA-256・UTC 時刻・コマンド・counter・終了コード・ログ保存先、未実施/非該当の物理操作と理由を記録する。soak の FD数・RSS記録系列と `安定` / `頭打ち` / `判定保留` の disposition および理由も記録する。失敗試行は消さず、原因切り分け後の再試験を別試行として残す。

次をすべて満たすまで Stable 公開へ進まない。

- CI・artifact audit・source/license・checksum gate が成功。
- exact final candidate の canary が成功し、trigger 付きの targeted 検証・soak（実施した場合は release あたり1 OS/runtime）が完了。soak の FD数・RSS が最終観測まで安定化しない持続的な増加を示し、外部要因を特定できない場合は `判定保留` とし、公開しない。
- README の各 claim が実証または適格な baseline 継承に対応し、未試験の組合せを認定表示していない。
- crash、hang、stale state、再接続不能、重大な未解決 issue がない。
- 記録が揃い、公開前レビュー済み。
