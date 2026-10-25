# Stable リリース前検証手順

この文書はSiano Stable releaseで再利用する準備・操作・判定・cleanupの正本です。版ごとの適用matrixと検証記録は [`release-validation/1.0.0.md`](release-validation/1.0.0.md) のように版別文書へ記録します。個別候補の実測値・結果は今回候補の合格を意味しません。

Status: v3 (2026-10-25)

この文書は `siano-userland` の Stable リリースごとの実行手順である。`README.md` の CLI・終了コードと実装が事実の正本であり、本書の記載と食い違う場合は両者を確認して先に本書を直す。Stable releaseは次の順で進める。

1. 候補source commitを固定してRC candidateをbuildする。
2. candidateで回帰・soakを完了する。毎releaseの回帰項目と適用頻度は「共通の検証項目と頻度」に従う。
3. 試験結果をversion別文書へ記録してcommitする。
4. 候補sourceから製品codeの変更がない文書only commitであることを確認し、そのcommitへのrelease tagから自動build/releaseする。

candidateのsource、package、build inputを回帰・soak完了前に変更した場合は新candidateを作成し、影響する試験を行う。結果記録commit後はcandidateと製品codeが同一であることを確認し、共通release方針のtag/build工程へ進む。ソークは一定時間以上の連続したTS取得を指し、具体的な時間は適用頻度と版別matrixに従う。物理抜差しをユーザーに依頼する場合、各操作の直前に通知し、依頼後最大5分待つ。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 変更記録

- v3 (2026-10-25): 各release全配布archiveのnative回帰を維持し、試験結果commit・製品code不変確認後のrelease工程へ共通手順を整合。
- v2 (2026-10-07): 当時の7 archive短時間確認・candidate bytes公開方針を記録。現行release頻度と最終release工程は上記の共通方針に従う。

## 共通の検証項目と頻度

用語は4製品で共通とする。代表機はLinux x86_64 glibc、機種はチューナー、製品はドライバ、ソークは一定時間以上の連続したTS取得を指す。時間条件は適用頻度と版別matrixに従う。Sianoの通常の無指定時検証機種はPX-S1UDだが、機種初回の対象を限定しない。Sianoが対象とする全機種で初回手順を個別に実施し、結果は[`release-validation/initial-model-checks.md`](release-validation/initial-model-checks.md)へ機種別に記録する。

| 区分 | 適用頻度 | Sianoでの確認 |
|---|---|---|
| 機種初回 | Sianoの各機種を個別に代表機で2時間連続受信し、その機種で毎release回帰項目を代表機だけで行う。必要時項目も通し、結果は[`release-validation/initial-model-checks.md`](release-validation/initial-model-checks.md)へ機種別に記録する。 | PX-S1UDでは地上波receiver 0を確認する。CARD、LNB、衛星は製品機能外。ほかの機種はその機種の能力に応じて確認する。 |
| 回帰（毎release） | 代表機2時間連続受信。全配布archiveの各OS/ABIで短時間実機回帰、`--list-json`、最大能力の同時受信とTS/counter/終了値を確認する。最大試験で3波が不足する場合は不足分の短時間TSを個別確認する。受信中status、反復制御、正常停止とprocess/FD/IPC残留なし、USB抜去/再挿入も行う。 | 受信中statusと停止を確認する。単一receiverなのでAPDU、CARD挿抜、WinSCard/PCSCは適用しない。 |
| 必要に応じて | LNB実測と非対応機種の拒否、複数チューナー列挙/daemon並列、変更機能に応じた機種×OS/ABIの追加確認。 | LNB/CARD/PCSCは対象外。複数台対応やselector/USB変更時はS-DUAL/S-DISC。 |
| 初回/major | 必要時項目を通し、代表機で全無料channelを各10秒TS取得。macOS/Windows各30分連続、残るOS/ABI各10分連続で取得する。全channel取得は代表機だけ。別途指示されたソークは別記する。 | 地上波の実地域channelを現地planから選ぶ。衛星channelは対象外。 |

全配布archiveは各archiveのnative OS/ABI上で回帰し、代表機用の全channel10秒試験は他archiveへ広げない。下記は具体的な候補準備・起動・操作・cleanup手順であり、版別文書に実施した操作と結果を記録する。

## 共通実行環境表

### 環境表データ辞書

| 列 | 意味・含める属性 | 含めない属性 | 表記・許容値 |
|---|---|---|---|
| ID | OS/ABIと必要なruntime/access境界を持つ実行環境シナリオの識別子 | candidate状態、過去実績 | `E` + 番号。4製品で共通するシナリオは同じID、別runtime/access scenarioは別ID |
| OS / ABI | OS系統、CPU ABI、libcまたはOS runtime | host名、依存、実施状態 | `OS / CPU ABI / libc・runtime` の順。根拠文書にlibc記載がない場合は `未特定` |
| 依存 / driver / permission | 実行時USB library、OS側USB access手段と権限条件 | firmware、CARD API、検証結果 | `libusb:`、`USB access:`、`FD handoff:` の属性名を使う |
| firmware / input | 実行時に必要なfirmwareまたは製品入力と放送方式 | archiveへの同梱・埋込完了、candidate実績 | `firmware:` と `input:` を分け、該当しない属性は `なし` |
| 製品CLIからの内蔵CARD操作 | 製品固有CLI/APIの操作範囲 | PC/SC/WinSCard adapter、実機合否 | 操作名とmodel依存条件。機能なしは `なし` |
| native PC/SC/WinSCard adapter | OS標準APIへ接続する製品adapterの種類・配布形態 | 製品CLI CARD、adapter実機試験結果 | `Linux PC/SC:`、`macOS PC/SC:`、`Android:`、`Windows WinSCard:` を列挙 |
| 配布artifact / 手順 | 対象archive識別子とこの正本内の該当手順 | artifact hash、host、試験結果 | `artifact: <target>; procedure: §...` の順 |

共通IDは E02=Linux/x86_64/musl、E03=Linux/x86_64/glibc、E04=macOS/arm64、E05=Android/aarch64、E06=Android/armv7a、E07=Android/x86_64、E15-G=Linux/aarch64/glibc、E15-M=Linux/aarch64/musl、E17=Windows 11/x86_64を表す。IDはhost名や試験済み状態を表さない。SianoのLinux binaryは各architectureにつき1つの静的musl archiveであり、glibc/musl別artifactではない。

| ID | OS / ABI | 依存 / driver / permission | firmware / input | 製品CLIからの内蔵CARD操作 | native PC/SC/WinSCard adapter | 配布artifact / 手順 |
|---|---|---|---|---|---|---|
| E01 | Linux / x86_64 / glibc | libusb: 静的1.0.30; USB access: HAOS/SCS containerからの許可済みUSB node | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E02 | Linux / x86_64 / musl | libusb: 静的1.0.30; USB access: HAOS Supervisor add-on経路 | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E03 | Linux / x86_64 / glibc | libusb: 静的1.0.30; USB access: native OSの許可済みUSB node | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E04 | macOS / arm64 / Apple system runtime | libusb: 静的1.0.30; USB access: macOSの許可済み経路 | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `darwin-arm64`; procedure: §7 |
| E05 | Android (Termux) / aarch64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `android-aarch64`; procedure: §7 |
| E06 | Android (Termux) / armv7a / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `android-armv7a`; procedure: §7 |
| E07 | Android (Termux) / x86_64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `android-x86_64`; procedure: §7 |
| E08 | Linux / x86_64 / glibc (Fedora) | libusb: 静的1.0.30; USB access: Fedoraのudev permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E09 | Linux / x86_64 / 未特定 (Gentoo) | libusb: 静的1.0.30; USB access: Gentooのudev permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E10 | Linux / x86_64 / glibc (NixOS) | libusb: 静的1.0.30; USB access: NixOSのudev permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E11 | Linux / x86_64 / musl (Chimera Linux) | libusb: 静的1.0.30; USB access: Chimera Linuxのudev permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E12 | Linux / x86_64 / musl (Alpine Linux) | libusb: 静的1.0.30; USB access: Alpineのmdev permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E13 | Linux / x86_64 / 未特定 (OpenWrt) | libusb: 静的1.0.30; USB access: OpenWrtのplatform permissionとkernel-module ownership | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-x86_64`; procedure: §7 |
| E15-G | Linux / aarch64 / glibc | libusb: 静的1.0.30; USB access: platformのnative USB claimとpermission | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `linux-aarch64`; procedure: §7 |
| E17 | Windows 11 / x86_64 / Win32 | libusb: 静的1.0.30; USB access: WinUSB driver | firmware: `isdbt_rio.inp`; input: ISDB-T channel | なし | Linux PC/SC: なし; macOS PC/SC: なし; Android: なし; Windows WinSCard: なし | artifact: `windows-x64`; procedure: §7 |

行は一般の実行環境と必要なaccess条件を記す。各版の実際のhost名と実施結果は版別記録に残す。E08–E13は各OS固有手順が適用される場合の環境であり、E14 FreeBSDとE16 Debian i386はbinary archive対象に含めない。SianoにはCARD、PC/SC、WinSCardのbackendはない。

## 共通試験シナリオ

各項目は「適用頻度/対象能力、準備/起動、操作/利用者への依頼、期待値、停止/cleanup、記録」の順に記す。HAOS/SCSの物理操作は各依頼直前にCodexが`beep`、Claude Codeが`vibe`を実行し、利用者へ具体的操作を依頼してから最大5分待つ。期限内に完了しなければ未完了として物理機器を使う工程を中断する。

### A. 列挙と最大同時受信

| 適用頻度 / 対象能力 | 準備 / 起動 | 操作 / 利用者への依頼 | 期待値 | 停止 / cleanup | 記録 |
|---|---|---|---|---|---|
| 毎release。単一PX-S1UD receiver | candidate native archive、firmware hash、USB `port=`、空のruntimeを用意 | `sianod --list-json`で列挙後、`sianod`を起動し`sianoctl status/list`、短時間`--receiver 0`受信を実施 | daemon/client exit、TS同期・TEI/continuity、packet/USB counterを確認。公開receiver 1以外の能力を要求しない | clientを正常停止しdaemonを正常停止。process/FD/IPC残留なし | source/candidate/archive SHA、host/OS/ABI、USB port、firmware hash、command/exit、TS/counters |

client稼働中に別terminalから`"$D/sianoctl" --instance "$INSTANCE" --runtime-dir "$RT" status`と`list`を照会する。受信中control/retuneが変更対象または指定されたときは、§7.1のS-CTRL手順で`--control` clientへ`channel`/`tune`/`quit`を入力し、各応答、stream状態、client/daemon exitを対応付けて記録する。

### B. CARD適用性

SianoはCARD reader、ATR/APDU、PC/SC/WinSCard機能を提供しない。CARD抜去・再挿入コマンドは実行せず、結果欄は「対象外」とする。USB抜去/再接続は次の独立シナリオで行う。

### C. USB抜去・再接続

| 適用頻度 / 対象能力 | 準備 / 起動 | 操作 / 利用者への依頼 | 期待値 | 停止 / cleanup | 記録 |
|---|---|---|---|---|---|
| 毎release。idle時および単一receiver streaming時 | `sianod --list-json`でUSB portを固定し、idle/streamingを別runにする | 親が対象機器を抜くよう利用者へ依頼し、依頼後最大5分待つ。抜去後、同じ物理portへ戻すよう依頼する | client/daemon切断表示とexitを記録。切断clientが有限時間で終了し、再列挙後に短時間取得できること。hotplug自動復帰は期待しない | 旧daemonを通常停止後、新しいdaemon/runtimeで起動し、短時間取得・正常停止・process/FD/IPC残留なしを確認 | USB操作時刻、host/port、client/daemon exit、再列挙/再受信、log/cleanup |

| 条件付き。2台列挙/selector/切断処理変更時、または2 device試験を指定された場合 | `sianod --list-json`で`PORT_A`/`PORT_B`を選び、別runtime `RT_A`/`RT_B`を用意 | 下記のように2 daemon/runtime/clientを開始し、親がdevice A抜去を利用者へ依頼して最大5分待つ | A clientは有限時間で切断exit 7、B clientは受信を継続して正常終了exit 0。A再接続後に再列挙し、新daemon/runtimeから短時間取得 | A/B clientを正常停止し各daemonを別々に停止・wait。両runtime/process/FD cleanupを確認 | port/device別daemon・client PID、切断側/残存側exit、stream継続、再列挙/再取得、cleanup |

```sh
"$D/sianod" --device "usb:$PORT_A" --instance A --firmware "$FW" --runtime-dir "$RT_A" & PID_A=$!
"$D/sianod" --device "usb:$PORT_B" --instance B --firmware "$FW" --runtime-dir "$RT_B" & PID_B=$!
"$D/siano-ts" --instance A --runtime-dir "$RT_A" --receiver 0 -c 22 -t 30 -o "$LOG/A.ts" & CLIENT_A=$!
"$D/siano-ts" --instance B --runtime-dir "$RT_B" --receiver 0 -c 22 -t 30 -o "$LOG/B.ts" & CLIENT_B=$!
"$D/sianoctl" --instance A --runtime-dir "$RT_A" status
"$D/sianoctl" --instance B --runtime-dir "$RT_B" status
```

device Aを戻した後は`"$D/sianod" --list-json`で再列挙し、Aの旧client/daemonを停止・waitしてから同じruntimeを通常cleanupする。新しい`RT_A2`で`sianod --device "usb:$PORT_A" --instance A2 --firmware "$FW" --runtime-dir "$RT_A2"`を起動し、`sianoctl status/list`と30秒`-c 22 -t 30`受信を行う。試験終了時はclientごとの終了値を保存し、A/B daemonをそれぞれSIGINTで通常停止・waitしてprocess/IPC/USB FD残留を確認する。

1台しか使えない環境では2台同時/残存側継続を未実施と記録し、単体切断手順を2台試験の代替にしない。

### D. teardown・結果

USB切断時の期待終了値はREADMEと候補実測を照合する。停止/cleanupの各結果は成功値を仮定せず保存する。頻度別の実施状況・未実施・失敗は版別記録へ置き、物理操作の応答がない工程は未完了とする。

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
| E15-G | Fedora aarch64（L4T kernel） | canonical runtime（`linux-aarch64` のnative execution environment） | §7 / validation-results.md |
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

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。選択する短時間実機確認のarchiveは影響判定に関係なくcandidateそのものを使い、適用するarchive/OS頻度は該当版の `docs/release-validation/<version>.md` に従う。

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

## 3. 配布artifactごとの実機短時間確認手順

1. 該当releaseの全配布archiveを対応するnative OS/ABIで確認する。下表はarchiveごとの操作方法とnative環境の対応表である。同じx86_64実行ファイルを複数のaccess pathで重複実行する場合はOS/ABIのnative確認と分けて記録する。

| 配布artifact | 環境例 | 使用機器 | 選択時の短時間確認 |
|---|---|---|---|
| `linux-x86_64` | E03 AnduinOS x86_64（glibc系host上で配布static binaryを実行） | PX-S1UD 1台 | §7.1aの列挙→接続中受信→USB切断で有限終了→再接続後の再列挙と30秒受信→残留確認 |
| `linux-aarch64` | E15-G Fedora aarch64 / L4T | PX-S1UD 1台 | 同上。現候補archiveをnative実行 |
| `darwin-arm64` | E04 macOS arm64 | PX-S1UD 1台 | 同上。macOS手順のhash/依存確認も含む |
| `android-aarch64` | E05 Termux aarch64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-armv7a` | E06 Termux armv7a | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-x86_64` | E07 Bliss OS Termux x86_64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `windows-x64` | E17 Windows 11 x86_64 / WinUSB | PX-S1UD 1台 | 同上。Windows固有手順で列挙、30秒受信、停止、切断/再接続、再受信 |

2. 必須確認の一連の操作に総時間上限は設けない。候補archiveのhash確認・展開、firmware hash確認、アンテナ/電源、host準備、ログ先作成は検証開始前に済ませる。`sianod --list-json`でUSB discoveryを確認し、daemon起動後は`sianoctl status/list`、単体receiverの短時間受信、通常client停止、USB切断終了と再接続後の再列挙/受信、process残留なしを確認する。USB抜差しはユーザーが実施する。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行し、依頼した時点から最大5分待つ。5分以内に操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。各archiveで別々のrunを記録し、別artifactや過去candidateの結果を今回の結果に代用しない。
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
| C7 | device enumeration/selection、`--list-json`、USB location（`device-selector.c`、`usb-location.h`） | `sianod --list-json`で2台を列挙し、index/port/bus:address選択、不正selector→exit 3 | E03 + E04 + E17 | 2 | 列挙・識別の経路 |
| C8 | disconnect/hotplug 分類、exit code（`stream-state.c`、`exit-codes.c`） | 受信中に片側切断→有限時間で exit 7、残存側 exit 0、再接続、再実行。1台/1 USB port しかない環境では有限 exit と再接続だけを確認でき、残存側 exit 0 は2台前提のため満たせない | E03 + E01 + E02 + E04 + E17 + E05 + E06 + E07（Termux 3 ABI は artifact が別個）。E13/E15-G は claim 表示時。E16 は source-build-only で release artifact・current hardware claim がないため対象外（platform-specific な diff が i386 を直接対象とする場合を除く） | 2（Termux/E17 は port が1つなら1。1台 run は full 2台 disconnect test として数えない） | hotplug / cleanupの経路 |
| C9 | kernel-driver detach decision（`detach-decision.h`） | bound 時の拒否→exit 4。live unbind はしない | E03（blacklist 状態は reboot で復元） | 1 | driver bind/accessの安全性 |
| C10 | `--control` parsing と retune | stdin `channel`/`tune`/`quit`、open 中の retune、exit 0 | E03 + E17 | 1 | 受信中controlの継続性 |
| C11 | access-setup 材料: `packaging/mdev/*`、docs の udev/OpenRC/procd 記述 | node 権限、非root 実行 | E12（mdev）または記述を変えた環境 | 2 | access setup / 権限 |
| C12 | `tests/test_cli.sh` と unit test で覆われる CLI/引数変更 | CLI動作 | 変更対象に対応するmatrix環境 | 1 | CLI/API互換性 |
| C13 | docs、license、metadata | gate のみ | — | — | 安定性への直接影響なし（追加soakはユーザー指示による） |

platform-only の hunk は、そのplatformのmatrix環境で機能確認を行う。classが不明または複数にまたがる場合は影響し得るpathをユーザー提示用に整理する。追加確認のうち5分を超える連続負荷はsoakとして扱う。毎releaseの代表機2時間と初回/majorの時間条件は「共通の検証項目と頻度」に従い、別途の追加soakが指定された場合はその時間・OSを記録する。物理操作の待機は§3と同じく依頼ごとに最大5分とする。

## 5. 連続受信・soak

短時間回帰とは分けて連続受信を行う。毎releaseの代表Linux x86_64 2時間と初回/majorのmacOS/Windows 30分、残るOS/ABI 10分は「共通の検証項目と頻度」で定める。ユーザーから別途soakが指示された場合は、追加分の実施有無・時間・OSをその指示に従う。

各連続受信では実行archive、OS/ABI、モデル、channel、実行時間、client/daemon終了値、TS品質、process/FD/RSSの観測値、cleanupを記録する。追加試験の選択や、頻度表の対象外へ追加する条件はユーザーの指示に従う。

### soak の内容

- 共通の頻度表またはユーザーの追加指示で指定された時間、OS、topologyを用いる。
- 10秒以下の間隔で RSS と FD 数を記録し、傾向を示す。**数値の合否基準は設けない**。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。
- 各 cycle の出力を §6 で検査する。終了後にprocess残存と`sianoctl status/list`を確認する。
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
D=<extracted dir>; DAEMON=$D/sianod; C=$D/sianoctl; B=$D/siano-ts
FW=$D/firmware/isdbt_rio.inp; LOG=<log dir>; RT=<runtime dir>; INSTANCE=s1ud
mkdir -p "$RT" "$LOG"
sha256sum "$FW"                       # 054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818
"$DAEMON" --list-json > "$LOG/discovery.json"  # enclosures[].devices[].portからPORTを選択
PORT=<enumerated USB port>
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  2> "$LOG/sianod.err" & DAEMON_PID=$!
"$C" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/status.txt"
"$C" --instance "$INSTANCE" --runtime-dir "$RT" list > "$LOG/list.txt"
```

USB discoveryの期待値は `sianod --list-json` の `enclosures[].devices[]` にPX-S1UD、USB ID、`port`、受信機情報があること。通常の一覧・稼働状態は `sianoctl list/status` でdaemonへ照会し、discoveryとdaemon状態の記録を区別する。

```sh
# S-RX（$S 秒受信。Sianoのreceiverは0のみ）
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/si.ts" \
  2> "$LOG/si.err"; echo $? > "$LOG/si.rc"
# S-ANA: §6 の 1–3 を si.ts に適用
```

```sh
# S-CTRL（daemonは維持し、client stdinからretune）
{ sleep 20; echo 'channel 21'; sleep 20; echo 'channel 22'; sleep 20; echo quit; } \
  | "$B" --instance "$INSTANCE" --runtime-dir "$RT" --control -o "$LOG/ctrl.ts" \
    2> "$LOG/ctrl.err"; echo $?
```

```sh
# S-SIG（clientを止め、daemonは維持）
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -o "$LOG/sig.ts" \
  2> "$LOG/sig.err" & CLIENT_PID=$!
sleep 30; kill -INT "$CLIENT_PID"; wait "$CLIENT_PID"
"$C" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/status-after-client-stop.txt"
# S-RESID: client processなし、TS出力close、ctl status/listが同じdaemonへ接続
```

```sh
# S-DUAL（2台。deviceごとにdaemon/instanceを分け、runtime rootは共有）
"$DAEMON" --device "usb:$PORT_A" --instance a --firmware "$FW" --runtime-dir "$RT" \
  2> "$LOG/daemon-a.err" & PID_A=$!
"$DAEMON" --device "usb:$PORT_B" --instance b --firmware "$FW" --runtime-dir "$RT" \
  2> "$LOG/daemon-b.err" & PID_B=$!
"$B" --instance a --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/a.ts" 2> "$LOG/a.err" & CLIENT_A=$!
"$B" --instance b --runtime-dir "$RT" -c 21 -t "$S" -o "$LOG/b.ts" 2> "$LOG/b.err" & CLIENT_B=$!
wait "$CLIENT_A"; RC_A=$?
wait "$CLIENT_B"; RC_B=$?
# 各processが意図したUSB nodeをdaemon経由で所有していることを /proc/<pid>/fd で確認
```

```sh
# S-DISC（物理。ユーザーが実施）
# S-DUAL中にdevice Aを抜く。Aのclient終了値とBの継続を個別記録する。
# 切断後は該当daemonを通常停止し、USB再接続後に--list-jsonでportを確認して同じinstance/runtimeで再取得する。
# 1台/1 USB portしかない環境ではそのdeviceの挙動のみを確認し、2台試験と混同しない。
kill -INT "$PID_A"; wait "$PID_A"
"$DAEMON" --list-json > "$LOG/discovery-reconnected.json"
# portを再確認した後、上記S-DUALと同じdaemon起動・S-RXを実行
```

### 7.1a 選択された短時間実機確認（1台）

E01/E02/E03/E04/E15-G/E05/E06/E07/E17の各配布archiveで、各環境固有準備を済ませてから個別に行う。PX-S1UDを接続し、`$DAEMON --list-json`の`enclosures[].devices[]`から`model=PX-S1UD`, `usb=3275:0080`, `port=`を記録する。`sianoctl list/status`のdaemon inventoryがreadyであることも確認する。候補archiveの同じdaemon/clientと同梱firmwareを使う。

```sh
# 接続中受信。-tを付けず、deviceを開いた状態で維持する
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  2> "$LOG/hotplug-daemon.err" & DAEMON_PID=$!
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -o "$LOG/hotplug.ts" \
  2> "$LOG/hotplug.err" & p=$!
sleep 20
# vibe/beep の直後に利用者へUSBを物理的に抜くよう依頼する（応答待ちは最大5分）。
# 抜去の観測から120秒以内に終了し、exit 7であることを確認:
for i in $(seq 1 120); do kill -0 "$p" 2>/dev/null || break; sleep 1; done
hung=0
if kill -0 "$p" 2>/dev/null; then hung=1; kill -TERM "$p"; fi
if wait "$p"; then rc=0; else rc=$?; fi
echo "hung=$hung exit=$rc" > "$LOG/hotplug.rc"  # 期待: hung=0 exit=7
# vibe/beep の直後に再接続を依頼し（応答待ちは最大5分）、再接続された後:
kill -INT "$DAEMON_PID"; wait "$DAEMON_PID"
"$DAEMON" --list-json > "$LOG/discovery-reconnected.json" # 新しいportを選ぶ
PORT=<reconnected USB port>
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  2> "$LOG/reconnected-daemon.err" & DAEMON_PID=$!
"$C" --instance "$INSTANCE" --runtime-dir "$RT" status
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -t 30 -o "$LOG/reconnected.ts" 2> "$LOG/reconnected.err"
echo $? > "$LOG/reconnected.rc"
# 期待: 再列挙ready、30秒受信exit 0、TS §6 pass、終了後process残留なし
kill -INT "$DAEMON_PID"; wait "$DAEMON_PID"
```

一連の操作に総時間上限はない。各物理操作の依頼から操作完了までの待機だけを最大5分とする。抜去後に有限時間内に終了しない場合はhangとして失敗を記録し、processを止めて復旧する。WindowsとTermuxでは次の環境別起動法を使い、同じ物理順・待機条件を守る。2台を使う追加topology試験はこの1台必須試験の代用ではない。

### 7.2 環境別

- **E01 HAOS x86_64 Debian/glibc SCS**: container root で実行し、そのことを記録する（非root claim に使わない）。`ha core info` で HAOS/kernel を記録。`linux-x86_64` を使い、base は §7.1。既存記録は revision がなく current claim ではない。
- **E02 HAOS x86_64 Alpine/musl add-on**: add-on options を現行値で退避し、ユーザー承認のうえ試験用 options で起動、終了後に復元・停止する。restart loop がないことを確認。base は §7.1。`linux-x86_64` と同じ static binary を使う。
- **E03 AnduinOS x86_64**: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し `lsusb -t` で未bind を確認。udev rule と `video` グループで非root 実行。base は §7.1。C9 は bound 状態で `--detach-kernel-driver` なしの拒否（exit 4）だけを確認し、live unbind はしない。blacklist の変更はユーザー確認のうえ reboot で復元する。`linux-x86_64` archiveのnative確認環境例。
- **E04 macOS arm64**: `otool -L ./siano-ts` が system library のみであること、`sw_vers` を記録。base は §7.1。`darwin-arm64` archiveのnative確認環境例。
- **E05–E07 Android Termux**: daemonのUSB起動は `termux-usb -r -e "$DAEMON --instance $INSTANCE --runtime-dir $RT --firmware $FW" /dev/bus/usb/BBB/AAA` とし、`termux-usb -e` が渡すFDをdaemonが所有する。別terminalの`$C`/`$B`には同じinstance/runtimeを指定し、clientへUSB FDやfirmwareを渡さない。受信中にユーザーがUSBを抜く。Termux/Android versionを記録。E06はarmv7a、E07はBliss OS x86_64で、E07は検証中だけ画面常時点灯とTermux前面表示を使い`stay_on_while_plugged_in`を元へ戻す。再接続後にUSB nodeを取り直し、daemonを同じinstance/runtimeで起動し直して30秒受信。凍結したrunはinvalid。`--control`はstdinをファイルから渡す。
- **E08 Fedora x86_64（SELinux）**: [Fedora SELinux](platforms/fedora-selinux.md) の再検証手順（context、label、`ausearch -m AVC`、exit code）に従う。この環境の構成・再検証手順を含む Fedora SELinux 文書、または Fedora に関する in-repo 材料を変更したとき、もしくは Fedora claim を変更するときのみ実施する。
- **E09 Gentoo x86_64**: 記録がない。Gentoo 手順を新設・変更した場合に exact archive を OpenRC と udev `usb` グループ構成で実施し、doc を作る。それ以外は未確認。
- **E10 NixOS x86_64**: [NixOS](platforms/nixos.md) に従い 2台同時受信と cold boot 後の module 未ロードを確認。構成手順の変更時のみ。
- **E11 Chimera Linux x86_64**: [Chimera](platforms/chimera-linux.md) に従い Clang native build、全 test、static/dynamic binary の 2台同時受信。構成手順の変更時のみ。
- **E12 Alpine x86_64 / mdev**: [Alpine/mdev](platforms/alpine-mdev.md) に従い `mdev -s`、`siano-ts-mdev --scan`、非root 2台受信。`packaging/mdev/**` は配布物に含まれるため、変更時はこの環境を実施する。
- **E13 OpenWrt x86_64**: 既存記録は `0315bd5` で exit 1（変更前コード）。current claim にしない。OpenWrt 手順を新設・変更した場合にだけ procd 起動停止・hotplug 権限を確認する。
- **E14 FreeBSD x86_64**: 対象外。記録は履歴のみで、gate・canary・soak に含めない。
- **E15-G Fedora aarch64（L4T）**: `linux-aarch64` の canonical runtime。`uname -r` と `getenforce` を記録し、S-RX・S-DUAL・S-DISC を実施する。claim は特定 kernel に限定する。既存記録 `b2c9466` は履歴。
- **E16 Debian x86/i386**: 配布artifactはなく、配布binaryのnative matrix対象外。source-build claimを変更する場合は[Debian i386](platforms/debian-i686.md)のnative buildと`make test`、30秒のnonblocking receive、slow-sinkの`--fail-on-drop`/exit 8を確認する。5分を超える実機負荷が必要なら他OSと同じユーザー決定soak手順にする。
- **E17 Windows 11 x86_64**: product対象。ZIPを展開し、WinUSB割当後にZIP rootの3 programを使う。OS build、Zadig/WinUSB version、各EXEのcandidate固有SHA（同じZIPのmanifestと照合）を記録する。旧`packaging/windows-baseline.sha256`は単体版の履歴であり、現行candidateとの一致を要求しない。USB IDとselectorを確認して、PowerShellで次の操作を行う。S-CTRLは`--control`付きconsumerのconsole stdinから渡す。[今回の統合3program記録](platforms/validation-results.md#2026-10-21-windows統合3program短時間受信)では1台の30秒受信・TS整合性・lease解放・正常停止を確認した。hotplug・soak・最終RC認定は未実施であり、以下の手順の記載自体を成功記録と扱わない。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$RT = Join-Path $env:TEMP "siano-rc-runtime-$PID"
New-Item -ItemType Directory -Path $RT | Out-Null
$manifest = Get-Content .\manifest.json -Raw | ConvertFrom-Json
foreach ($name in 'sianod.exe','sianoctl.exe','siano-ts.exe') {
    $actual = (Get-FileHash $name -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $manifest.files.$name.sha256) { throw "candidate hash mismatch: $name" }
    "$name $actual"
}
& $D --list-json                    # PX-S1UD / 3275:0080とenclosures[].devices[].portを記録し、usb:<port>を使う
$selector = 'usb:BUS-PORT'          # 実際に列挙されたselectorへ置換
& $D --device $selector --instance s1ud --firmware $FW --runtime-dir $RT
# daemonがreadyになったら別のPowerShell（同じZIP root）で:
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
& $C --instance s1ud --runtime-dir $RT status
$p = Start-Process -FilePath $B -ArgumentList "--instance s1ud --runtime-dir `"$RT`" -c 22 -o hotplug.ts" -PassThru -NoNewWindow -RedirectStandardError hotplug.err
Start-Sleep -Seconds 20
# ここで利用者がUSBを抜く。2分以内に終了し、ExitCode 7であることを確認する
if ($p.WaitForExit(120000)) { $p.Refresh(); $p.ExitCode }
else { 'HANG'; Stop-Process -Id $p.Id -Force }
# daemon consoleでCtrl+C。利用者がUSBを戻してから同consoleで
# --list-jsonでselectorを確認し、上と同じdaemon起動を行う。
# daemonがreadyになったらconsumer consoleで:
& $C --instance s1ud --runtime-dir $RT status
& $B --instance s1ud --runtime-dir $RT -c 22 -t 30 -o reconnected.ts 2> reconnected.err
$LASTEXITCODE                 # 0を期待
Get-Process siano-ts -ErrorAction SilentlyContinue
# 終了時はdaemon consoleでCtrl+C。
```

USBを物理的に外しても有限終了しない場合はhangとして失敗を記録する。各物理操作は依頼直前に通知し、依頼から最大5分待つ。

## 8. USB切断時の期待値

受信中にUSBが切断されたclientはREADMEのUSB error contractに従ってexit 7を返すことを期待する。対象candidateのOS/ABIごとに実測し、他版の終了値で代用しない。

## 9. 記録

検証結果は各版の [`release-validation/<version>.md`](release-validation/1.0.0.md) へ記録する。release recordにはversion、commit、workflow run、各binary archiveとsource archiveのchecksum/audit、baseline tag、各artifactのbyte-identity判定、hunk-levelの変更影響、claimごとの状態、matrix runの環境ID・host・USB `port=`・archive SHA-256・UTC時刻・コマンド・counter・終了コード・ログ保存先、連続受信の実施時間/OS/結果、FD/RSS系列とdisposition、物理操作を行えなかった場合の理由、公開assetとcandidateのbyte一致判定、残存blocker・既知の制限を記録する。失敗試行は消さず、原因切り分け後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまで Stable 公開へ進まない。

- CI・artifact audit・source/license・checksum gate が成功。
- final candidateについて、該当版の `docs/release-validation/<version>.md` で当該releaseに適用される短時間matrixが成功し、ユーザーが選択した場合はSoakの指定試験も完了している。soakのFD数・RSSが最終観測まで安定化しない持続増加を示し、外部要因を特定できない場合は`判定保留`とし、公開しない。
- README の各 claim が実証または適格な baseline 継承に対応し、未試験の組合せを認定表示していない。
- crash、hang、stale state、再接続不能、重大な未解決 issue がない。
- 短時間matrixに未完了の配布artifactがある場合はStable gate未完了とし、support表示だけで完了扱いしない。
- §2.5の同一run内byte一致確認が成功している。
- README、COPYING、LICENCE.siano、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。
