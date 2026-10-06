# Stable リリース前検証手順

Status: v2 (2026-10-07)

この文書は `siano-userland` の Stable リリースごとの実行手順である。`README.md` の CLI・終了コードと実装が事実の正本であり、本書の記載と食い違う場合は両者を確認して先に本書を直す。Stable releaseは次の順で進める。

1. リリース候補のsource commit（`VERSION`を更新した`main`のcommit）を固定し、そのcommitで`release.yml`を実行してcandidate artifactを作る。
2. candidate artifactを展開し、必要な全OS/architectureで実機試験と変更影響の試験を完了する。
3. 試験結果を文書に記録してcommitする。
4. 候補source commitにタグを付け、試験に使ったcandidate archiveと`SHA256SUMS`をそのままGitHub Releaseへ公開する。

工程2の全試験が完了するまではcandidate commitを固定して使う。candidateのsource、package、build inputを工程2の完了前に変更した場合は、その変更を含む新candidateをCIで作り、影響する試験を行う。工程3の記録commitはcandidateの差し替えではなく、タグは記録commitではなく候補source commitに付ける。工程3の後に工程2へ戻って実機試験を繰り返さない。公開物は試験したbytesそのものであり、release用に再buildしない。

配布する各主要OS/architecture binary artifactについて、工程2でfinal candidateの実機確認を行う。必須検証の一連の操作に総時間上限は設けない。物理抜差しをユーザーに依頼した時点から、その操作への応答を最大5分待つ。各依頼の直前にHAOS側でCodexは`beep`、Claude Codeは`vibe`を実行する。5分を超える連続負荷試験は別のsoak手順にする。安定性に影響し得る変更のsoak有無、時間（10分/30分/2時間）、対象OSはユーザーが決める。エージェントは変更差分・過去記録・影響し得る範囲を要約し、判断を待つ。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 変更記録

- v2 (2026-10-07): 7つの配布binary archiveごとの実機短時間確認を毎回必須化。短時間確認に総時間上限を設けず、5分は各物理操作の応答待ち上限とする。5分を超える連続負荷は別soakとし、soakの有無・時間（10/30/120分）・対象OSをユーザーが決める。release工程（candidate固定→試験→記録→候補commitへのタグとcandidateそのものの公開）と公開前後のbyte一致確認を明記。

## 0. 環境IDと状態語彙

短時間確認は配布artifactと対応する環境で実施する。soak対象OSはユーザー決定後に選ぶ。AppArmor、usbip、SELinux、mdev などはその環境の access path note として記録する。

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

このmatrixは現行release archiveの7 targetと、PX-S1UDを使った既存実機記録を対応させている。根拠となる歴史的な受入結果は[validation-results.md](platforms/validation-results.md)にあり、[PR #14](https://github.com/Khronos31/siano-userland/pull/14)には2台のport選択・制御試験と、release前にhardware validationを別途行う記録、[Issue #3](https://github.com/Khronos31/siano-userland/issues/3)にはディストリビューション以外の検証軸がある。これら過去結果は、今回のexact candidate試験を代替しない。

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
5. 前回の長時間試験の日付やrelease数は事実情報として記録できるが、エージェントがsoak有無・時間・OSを決める規則として使わない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。配布artifactごとの必須短時間matrixは影響判定に関係なく毎回実施する。

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
5. `release.yml` の `Package, audit, and compare deterministic archives` step が、同一run内で全archiveを2回packageしてbyte一致を確認していること、Windows jobの`reproducible-windows-build.ps1`が成功していることを確認する。正規化（timestamp等のマスク）による一致は認めない。
6. 工程4の公開後、GitHub Releaseからassetを新しい空ディレクトリへ取得し、試験に使ったcandidateの`SHA256SUMS`と`cmp`で一致すること、および各archiveが`sha256sum -c`を通ることを確認する。差があれば公開を取り下げて原因を調べる。比較したrunとrelease、判定を記録する。

```sh
gh workflow run release.yml --ref <candidate-ref>          # 未実行のときのみ
gh run list --workflow release.yml --limit 10
gh run view <run-id> --json headSha,conclusion
gh run download <run-id> --name release-candidate --dir <new-empty-dir>
(cd <new-empty-dir> && sha256sum -c SHA256SUMS)
# 工程4の公開後
gh release download v<version> --dir <another-new-empty-dir>
(cd <another-new-empty-dir> && sha256sum -c SHA256SUMS)
cmp <new-empty-dir>/SHA256SUMS <another-new-empty-dir>/SHA256SUMS
```

CI が行う build・audit・smoke を成功後にローカル再実行しない。新しい汎用検証スクリプトは作らない。

## 3. 配布artifactごとの実機短時間確認（毎回必須）

1. §2で固定したfinal candidateから、次の各archiveを対応する実機環境へ配布して確認する。これは同じx86_64実行ファイルを複数ディストリビューションで総当たりする手順ではない。環境IDの特異な名前にかかわらず、artifactのOS/ABIに対応した試験対象として扱う。

| 配布artifact | 必須実機環境 | 使用機器 | 毎回の短時間確認 |
|---|---|---|---|
| `linux-x86_64` | E03 AnduinOS x86_64（glibc系host上で配布static binaryを実行） | PX-S1UD 1台 | §7.1aの列挙→接続中受信→USB切断で有限終了→再接続後の再列挙と30秒受信→残留確認 |
| `linux-aarch64` | E15 Fedora aarch64 / L4T | PX-S1UD 1台 | 同上。現候補archiveをnative実行 |
| `darwin-arm64` | E04 macOS arm64 | PX-S1UD 1台 | 同上。macOS手順のhash/依存確認も含む |
| `android-aarch64` | E05 Termux aarch64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-armv7a` | E06 Termux armv7a | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-x86_64` | E07 Bliss OS Termux x86_64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `windows-x64` | E17 Windows 11 x86_64 / WinUSB | PX-S1UD 1台 | 同上。Windows固有手順で列挙、30秒受信、停止、切断/再接続、再受信 |

2. 必須確認の一連の操作に総時間上限は設けない。候補archiveのhash確認・展開、firmware hash確認、アンテナ/電源、host準備、ログ先作成は検証開始前に済ませる。`--list`、接続中受信、USBを物理的に抜いて有限時間で終了すること、再接続後の`--list`復帰と30秒受信、プロセス残留なしを確認する。USB抜差しはユーザーが実施する。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行し、依頼した時点から最大5分待つ。5分以内に操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。各archiveで別々のrunを記録し、別artifactや過去candidateの結果を今回の結果に代用しない。
3. 30秒受信のTSは§6で検査する。5分を超える連続負荷は短時間確認に混ぜず、§5のsoakとしてユーザーの決定を待つ。各runの記録にはarchive名/SHA-256、source commit、環境ID/OS version/architecture、USB `port=`、実施時刻（UTC）、コマンド、終了値、TS検査結果、残留の有無を残す。
4. host/deviceが使えない、archiveが起動しない、または必須物理操作を実施できない場合は該当artifactのgate未完了とする。環境を別のOS/ABIで黙って代替せず、ユーザーに状況と必要な選択肢を提示する。

## 4. 変更class と targeted qualification

v0.1.5..v0.1.9 のように差分が大きい場合は、変更したファイル名だけでなく hunk 単位で分類する。`siano-ts.c` の `#ifdef _WIN32` / `#if defined(__APPLE__)` / `SIANO_HAVE_WRAP_SYS_DEVICE` など platform guard 内の hunk はその platform のみに作用する。v0.1.5..v0.1.9 には platform-guarded change（`__APPLE__` の `MAX_URBS`、`_WIN32` の `--fd` 拒否、`control-input.c` の Windows pipe 等）が含まれるため、必ず guard ごとに判定する。

| # | class（現行コード例） | 追加で見る機能 | 対象環境の候補 | 台数の条件（機能に応じて確認） | 安定性影響の参考（soakはユーザー決定） |
|---|---|---|---|---|---|
| C1 | USB transport: ring/transfer size、event thread、async completion（`siano-ts.c` の `MAX_URBS` 等） | 30秒受信、切断/再接続 | guard があればその platform（`__APPLE__`→E04）。なければ該当matrix環境 | 1。変更機能に2台経路があればその構成 | 連続受信・transport寿命 |
| C2 | libusb version、static/dynamic link、link option | 列挙、firmware、30秒受信、切断/再接続 | rebuildされたbinaryを含む配布matrix | 1（列挙選択変更では2台の列挙も含める） | USB初期化・転送寿命 |
| C3 | compiler/toolchain/SDK/NDK/runner image、`packaging/windows-baseline.sha256` 変更 | 30秒受信、正常停止 | rebuildされたbinaryを含む配布matrix | 1 | ABI/targetへの影響 |
| C4 | platform shim: `siano-os.h`、Windows pipe（`control-input.c`）、QPC clock、`--fd`/wrap | shim された feature | Windows→E17。`--fd`/wrap→E05 + E06 + E07 と E03（Termux 3 ABI は artifact が別個） | 1 | platform shim / FD handoff |
| C5 | firmware hash/format/load | firmware load、lock、30秒受信 | 該当binary matrix環境 | 1 | firmware初期化・stream開始 |
| C6 | stream/queue/writer/drop policy、`--fail-on-drop`、clock/`--time`（`queue-policy.c`、`write-policy.h`、`stream-state.c`） | counter、exit 8 semantics、30秒受信。slow-sinkはoffline test | 該当binary matrix環境。source-only i386はclaim追加時 | 1（queue共有/競合変更なら同時構成も判断材料） | stream/queue/writer持続動作 |
| C7 | device enumeration/selection、`--list`、USB location（`device-selector.c`、`usb-location.h`） | `--list` 2台、index/port/bus:address 選択、不正 selector→exit 3 | E03 + E04 + E17 | 2 | 列挙・識別の経路 |
| C8 | disconnect/hotplug 分類、exit code（`stream-state.c`、`exit-codes.c`） | 受信中に片側切断→有限時間で exit 7、残存側 exit 0、再接続、再実行。1台/1 USB port しかない環境では有限 exit と再接続だけを確認でき、残存側 exit 0 は2台前提のため満たせない | E03 + E01 + E02 + E04 + E17 + E05 + E06 + E07（Termux 3 ABI は artifact が別個）。E13/E15 は claim 表示時。E16 は source-build-only で release artifact・current hardware claim がないため対象外（platform-specific な diff が i386 を直接対象とする場合を除く） | 2（Termux/E17 は port が1つなら1。1台 run は full 2台 disconnect test として数えない） | hotplug / cleanupの経路 |
| C9 | kernel-driver detach decision（`detach-decision.h`） | bound 時の拒否→exit 4。live unbind はしない | E03（blacklist 状態は reboot で復元） | 1 | driver bind/accessの安全性 |
| C10 | `--control` parsing と retune | stdin `channel`/`tune`/`quit`、open 中の retune、exit 0 | E03 + E17 | 1 | 受信中controlの継続性 |
| C11 | access-setup 材料: `packaging/mdev/*`、docs の udev/OpenRC/procd 記述 | node 権限、非root 実行 | E12（mdev）または記述を変えた環境 | 2 | access setup / 権限 |
| C12 | `tests/test_cli.sh` と unit test で覆われる CLI/引数変更 | CLI動作 | 変更対象に対応するmatrix環境 | 1 | CLI/API互換性 |
| C13 | docs、license、metadata | gate のみ | — | — | 安定性への直接影響なし（soak有無はユーザー判断） |

platform-only の hunk は、そのplatformのmatrix環境で機能確認を行う。classが不明または複数にまたがる場合は影響し得るpathをユーザー提示用に整理する。追加確認のうち5分を超える連続負荷はsoakとして扱い、ユーザーに実施有無・時間・対象OSを確認して決定を待つ。物理操作の待機は§3と同じく依頼ごとに最大5分とする。

## 5. Soak（5分を超える負荷試験。ユーザー決定）

短時間確認とは独立した手順である。エージェントはコード変更、差分、実機記録、影響し得るOS/artifact/USB path、既知の失敗・変化を要約してユーザーへ提示する。次の3点は必ずユーザーが決めるまで開始しない: **実施するか（実施しない選択を含む）、10分/30分/2時間のいずれか、実施対象OS**。エージェントはclass表、安定性影響の分析、決め打ちの順序を根拠に自動選択・既定化しない。releaseごとに最大1 OS/runtimeという旧来の制約も適用しない。

判断材料は既存の変更class C1–C13、過去の長時間試験記録、アーキテクチャ固有変更、実機counterの変化である。これらはsoakを自動発火させる条件ではない。ユーザーがsoakなしを選んだ場合は、その決定を記録してrelease gateを継続する。

### soak の内容

- ユーザーが選んだ10分、30分、または2時間を実施する。受信台数/topologyは、選んだOSとユーザー承認済みの物理構成に合わせる。過去記録から台数を自動で決めない。
- 10秒以下の間隔で RSS と FD 数を記録し、傾向を示す。**数値の合否基準は設けない**。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。
- 各 cycle の出力を §6 で検査する。終了後にプロセス残存と `--list` の `ready` を確認する。
- S1UD は `port=` で個体を選ぶ。ユーザー指定OSで必要な台数/topologyを用意できない場合は、制約を報告してユーザーに次の判断を求める。他OSへの振替やsoak中止をagentが決めない。

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

### 7.1a 毎回の短時間実機確認（1台）

E01/E02/E03/E04/E15/E05/E06/E07/E17の各配布archiveで、各環境固有準備を済ませてから個別に行う。PX-S1UDを接続し、`--list`の`model=PX-S1UD usb=3275:0080 status=ready`と`port=`を記録する。候補archiveの同じ`$B`と同梱firmwareを使う。

```sh
# 接続中受信。-tを付けず、deviceを開いた状態で維持する
"$B" -F "$FW" -d "$sel" -c 22 -o "$LOG/hotplug.ts" 2> "$LOG/hotplug.err" & p=$!
sleep 20
# vibe/beep の直後に利用者へUSBを物理的に抜くよう依頼する（応答待ちは最大5分）。
# 抜去の観測から120秒以内に終了し、exit 7であることを確認:
for i in $(seq 1 120); do kill -0 "$p" 2>/dev/null || break; sleep 1; done
hung=0
if kill -0 "$p" 2>/dev/null; then hung=1; kill -TERM "$p"; fi
if wait "$p"; then rc=0; else rc=$?; fi
echo "hung=$hung exit=$rc" > "$LOG/hotplug.rc"  # 期待: hung=0 exit=7
# vibe/beep の直後に再接続を依頼し（応答待ちは最大5分）、再接続された後:
"$B" --list > "$LOG/relist.txt"       # 再列挙後のport=から $sel を更新する
"$B" -F "$FW" -d "$sel" -c 22 -t 30 -o "$LOG/reconnected.ts" 2> "$LOG/reconnected.err"
echo $? > "$LOG/reconnected.rc"
# 期待: 再列挙ready、30秒受信exit 0、TS §6 pass、終了後process残留なし
```

一連の操作に総時間上限はない。各物理操作の依頼から操作完了までの待機だけを最大5分とする。抜去後に有限時間内に終了しない場合はhangとして失敗を記録し、processを止めて復旧する。WindowsとTermuxでは次の環境別起動法を使い、同じ物理順・待機条件を守る。2台を使う追加topology試験はこの1台必須試験の代用ではない。

### 7.2 環境別

- **E01 HAOS x86_64 Debian/glibc SCS**: container root で実行し、そのことを記録する（非root claim に使わない）。`ha core info` で HAOS/kernel を記録。`linux-x86_64` を使い、base は §7.1。既存記録は revision がなく current claim ではない。
- **E02 HAOS x86_64 Alpine/musl add-on**: add-on options を現行値で退避し、ユーザー承認のうえ試験用 options で起動、終了後に復元・停止する。restart loop がないことを確認。base は §7.1。`linux-x86_64` と同じ static binary を使う。
- **E03 AnduinOS x86_64**: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し `lsusb -t` で未bind を確認。udev rule と `video` グループで非root 実行。base は §7.1。C9 は bound 状態で `--detach-kernel-driver` なしの拒否（exit 4）だけを確認し、live unbind はしない。blacklist の変更はユーザー確認のうえ reboot で復元する。`linux-x86_64` archiveの必須matrix環境。
- **E04 macOS arm64**: `otool -L ./siano-ts` が system library のみであること、`sw_vers` を記録。base は §7.1。`darwin-arm64` archiveの必須matrix環境。
- **E05–E07 Android Termux**: 短時間試験の受信起動は `termux-usb -r -e "$B -F $FW -c 22 -o $LOG/hotplug.ts" /dev/bus/usb/BBB/AAA` とし、受信を止めずにユーザーがUSBを抜く。Termux/Android versionを記録。E06はarmv7a、E07はBliss OS x86_64で、E07は検証中だけ画面常時点灯とTermux前面表示を使い`stay_on_while_plugged_in`を元へ戻す。再接続後にUSB nodeを取り直して`termux-usb`で30秒受信。凍結したrunはinvalid。`--control`はstdinをファイルから渡す。
- **E08 Fedora x86_64（SELinux）**: [Fedora SELinux](platforms/fedora-selinux.md) の再検証手順（context、label、`ausearch -m AVC`、exit code）に従う。この環境の構成・再検証手順を含む Fedora SELinux 文書、または Fedora に関する in-repo 材料を変更したとき、もしくは Fedora claim を変更するときのみ実施する。
- **E09 Gentoo x86_64**: 記録がない。Gentoo 手順を新設・変更した場合に exact archive を OpenRC と udev `usb` グループ構成で実施し、doc を作る。それ以外は未確認。
- **E10 NixOS x86_64**: [NixOS](platforms/nixos.md) に従い 2台同時受信と cold boot 後の module 未ロードを確認。構成手順の変更時のみ。
- **E11 Chimera Linux x86_64**: [Chimera](platforms/chimera-linux.md) に従い Clang native build、全 test、static/dynamic binary の 2台同時受信。構成手順の変更時のみ。
- **E12 Alpine x86_64 / mdev**: [Alpine/mdev](platforms/alpine-mdev.md) に従い `mdev -s`、`siano-ts-mdev --scan`、非root 2台受信。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。
- **E13 OpenWrt x86_64**: 既存記録は `0315bd5` で exit 1（変更前コード）。current claim にしない。OpenWrt 手順を新設・変更した場合にだけ procd 起動停止・hotplug 権限を確認する。
- **E14 FreeBSD x86_64**: 対象外。記録は履歴のみで、gate・canary・soak に含めない。
- **E15 Fedora aarch64（L4T）**: `linux-aarch64` の canonical runtime。`uname -r` と `getenforce` を記録し、S-RX・S-DUAL・S-DISC を実施する。claim は特定 kernel に限定する。既存記録 `b2c9466` は履歴。
- **E16 Debian x86/i386**: 配布artifactはなく、毎回必須matrix外。source-build claimを変更する場合は[Debian i386](platforms/debian-i686.md)のnative buildと`make test`、30秒のnonblocking receive、slow-sinkの`--fail-on-drop`/exit 8を確認する。5分を超える実機負荷が必要なら他OSと同じユーザー決定soak手順にする。
- **E17 Windows 11 x86_64**: product対象。ZIPを展開し、WinUSB割当後にZIP rootの`siano-ts.exe`を使う。OS build、Zadig/WinUSB version、EXE SHA（`packaging/windows-baseline.sha256`と照合）を記録する。USB IDを確認して、PowerShellで次の操作を行う。S-CTRLはconsole stdinから渡す。

```powershell
$B = (Resolve-Path .\siano-ts.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
& $B --list                         # PX-S1UD / 3275:0080 / readyを記録
$p = Start-Process -FilePath $B -ArgumentList ("-F `"$FW`" -d 0 -c 22 -o hotplug.ts") -PassThru -NoNewWindow -RedirectStandardError hotplug.err
Start-Sleep -Seconds 20
# ここで利用者がUSBを抜く。2分以内に終了し、ExitCode 7であることを確認する
if ($p.WaitForExit(120000)) { $p.Refresh(); $p.ExitCode }
else { 'HANG'; Stop-Process -Id $p.Id -Force }
# 利用者がUSBを戻してから:
& $B --list
& $B -F $FW -d 0 -c 22 -t 30 -o reconnected.ts 2> reconnected.err
$LASTEXITCODE                 # 0を期待
Get-Process siano-ts -ErrorAction SilentlyContinue
```

USBを物理的に外しても有限終了しない場合はhangとして失敗を記録する。各物理操作は依頼直前に通知し、依頼から最大5分待つ。

## 8. 切断時の exit code（事実）

現行 v0.1.9 では、受信中の fatal transfer failure を `stream-state.c` の `siano_stream_disconnect_error()` が `LIBUSB_ERROR_IO` / `LIBUSB_ERROR_PIPE` / `LIBUSB_ERROR_NO_DEVICE` から `LIBUSB_ERROR_NO_DEVICE` に写像し、`exit-codes.c` が exit 7 を返す。README の「受信中の USB 切断 = exit 7」はこの受信経路について現行実装と一致している。v0.1.5のexit 1は変更前コード（`exit-codes.c`はv0.1.8 `d333ff5`、`IO`/`PIPE`写像はv0.1.9 `9883025`で導入）の測定である。現行candidateのexit 7は、必須matrixで各OS/architectureごとに毎回実機確認し、そのcandidate recordに結果を記す。過去結果は今回の確認を代替しない。

## 9. 記録

検証結果は [`platforms/validation-results.md`](platforms/validation-results.md) へ日付付きで追記する。新しい records directory や template framework、汎用スクリプトは作らない。release recordにはversion、commit、workflow run、7 binary archiveとsource archiveのchecksum/audit、baseline tag、各artifactのbyte-identity判定、hunk-levelの変更影響、claimごとの状態、必須matrix各runの環境ID・host・USB `port=`・archive SHA-256・UTC時刻・コマンド・counter・終了コード・ログ保存先、Soakについてユーザー決定（実施有無・時間・OS）と結果、soakのFD数・RSS記録系列と`安定` / `頭打ち` / `判定保留`のdispositionおよび理由、物理操作を行えなかった場合の理由、公開assetとcandidateのbyte一致判定、残存blocker・既知の非blocking制限を記録する。失敗試行は消さず、原因切り分け後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまで Stable 公開へ進まない。

- CI・artifact audit・source/license・checksum gate が成功。
- final candidateの全配布artifactで短時間matrixが成功し、ユーザーが選択した場合はSoakの指定試験も完了している。soakのFD数・RSSが最終観測まで安定化しない持続増加を示し、外部要因を特定できない場合は`判定保留`とし、公開しない。
- README の各 claim が実証または適格な baseline 継承に対応し、未試験の組合せを認定表示していない。
- crash、hang、stale state、再接続不能、重大な未解決 issue がない。
- 短時間matrixに未完了の配布artifactがある場合はStable gate未完了とし、support表示だけで完了扱いしない。
- §2.5の同一run内byte一致確認が成功している。
- README、COPYING、LICENCE.siano、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。
