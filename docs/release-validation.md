# 安定版リリース前検証手順

この文書はSiano Stable releaseで再利用する準備・操作・判定・cleanupの正本です。版ごとの適用matrixと検証記録は版別文書へ記録します。個別候補の実測値・結果は今回候補の合格を意味しません。

Status: v3 (2026-10-25)

この文書は `siano-userland` の Stable リリースごとの実行手順である。`README.md` の CLI・終了コードと実装が事実の正本であり、本書の記載と食い違う場合は両者を確認して先に本書を直す。Stable releaseは次の順で進める。

1. 候補source commitを固定してRC candidateをbuildする。
2. candidateで回帰・soakを完了する。毎releaseの回帰項目と適用頻度は「共通の検証項目と頻度」に従う。
3. 試験結果を、candidate inventoryに含まれるversion別文書へ記録してcommitする。
4. release notesをcommitし、そのcommitにrelease tagを付けて自動build/releaseする。候補commitから結果記録・release notes・tagまでの各commitは文書onlyとし、許可path以外を変更しない。

candidateのsource、package、build inputを回帰・soak完了前に変更した場合は新candidateを作成し、影響する試験を行う。結果記録commit・release notes commit後も、tag対象まで製品codeとbuild inputがcandidateから変わっていないことを確認し、共通release方針のtag/build工程へ進む。結果記録・release notes・tagの文書commitはcandidateの後続commitとして扱い、tag commitがcandidateの直後であることや結果記録commitとtag commitが同一であることを前提にしない。ソークは一定時間以上の連続したTS取得を指し、具体的な時間は適用頻度と版別matrixに従う。物理抜差しをユーザーに依頼する場合、各操作の直前に通知し、依頼後最大5分待つ。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 共通の検証項目と頻度

用語は4製品で共通とする。代表機はLinux x86_64 glibc、機種はチューナー、製品はドライバ、ソークは一定時間以上の連続したTS取得を指す。時間条件は下の適用頻度と版別matrixに従う。製品・機種ごとの能力、対応機能、実コマンドは続く製品固有節と手順節に記し、版別記録にはcandidateごとの実施結果を記す。

| 区分 | 適用頻度 | 共通の確認 |
|---|---|---|
| 機種初回 | 各製品の全対象機種で、機種ごとに代表機の2時間連続受信と毎release項目を実施する。必要時項目も通し、結果は機種別に初回記録へ残す。 | 当該機種の公開能力・対応機能に沿って実行し、別機種の結果で代替しない。 |
| 回帰（毎release） | 代表機で2時間連続受信する。全配布archiveをそれぞれ対応するnative OS/ABIで短時間実機回帰し、`--list-json`、機種の公開同時受信能力、TS/counter/終了値を確認する。最大同時受信で地上波・衛星の対象3波を確認できない場合は不足波を短時間取得する。受信中の状態と反復制御、正常停止、process/FD/IPC残留なし、対応機能のCARD挿抜、USB抜去/再挿入を確認する。native PC/SC/WinSCard経路がある製品・OSでは製品CLI経路と分けて確認する。 | 各archive/OS/ABIのrunと各機種の結果を分ける。未対応機能は理由付きでN/Aとし、未実施や失敗を合格に読み替えない。 |
| release前TS品質（毎release） | T15の指定機種・patternを代表Linux x86_64 glibc、Windows x86_64、macOS arm64で各300秒取得し、各TSでTEI率と採用CCを別判定する。 | captureごとのTS/hash/packet数/TEI数/採用CC/終了値とT15 JSONを記録し、既存T09結果と人間判断を別に保つ。 |
| 必要に応じて | 対応機能ではLNB出力電圧を実測し、非対応機種の拒否を確認する。複数チューナーの列挙/daemon並列と、変更機能に応じた機種×OS/ABIの追加確認も行う。 | 適用する機種・機能・OS/ABIと実測値を記録する。 |
| 初回/major | 必要時項目を通し、代表機で全対象の無料channelを各10秒TS取得する。ソークはmacOS/Windows各30分、残るOS/ABI各10分とする。全channel取得は代表機だけで行い、別途指示されたソークは別に記録する。 | 30分/10分は各OS/ABIの個別時間であり、共通の一般定義である「一定時間以上の連続TS取得」を置き換えない。 |

全配布archiveのnative短時間回帰と代表機だけの全channel取得は別の確認である。候補の操作・期待値・cleanupは各archiveの具体手順に従い、実施状態・結果・未達は版別記録に残す。

## 製品固有の適用条件

Sianoの通常の無指定時検証機種はPX-S1UDだが、機種初回の対象を限定しない。Sianoが対象とする全機種で初回手順を個別に実施し、結果は[`release-validation/initial-model-checks.md`](release-validation/initial-model-checks.md)へ機種別に記録する。

| 区分 | 製品固有の確認 |
|---|---|
| 機種初回 | PX-S1UDでは地上波receiver 0を確認する。CARD、LNB、衛星は製品機能外。ほかの機種はその機種の能力に応じて確認する。 |
| 回帰（毎release） | 受信中statusと停止を確認する。単一receiverなのでAPDU、CARD挿抜、WinSCard/PCSCは適用しない。 |
| 必要に応じて | LNB/CARD/PCSCは対象外。複数台対応やselector/USB変更時はS-DUAL/S-DISC。 |
| 初回/major | 地上波の実地域channelを現地planから選ぶ。衛星channelは対象外。 |

## 共通実行環境表

### 環境表データ辞書

| 列 | 意味・含める属性 | 含めない属性 | 表記・許容値 |
|---|---|---|---|
| ID | OS/ABIと必要なruntime/access境界を持つ実行環境シナリオの識別子 | candidate状態、過去実績 | `E` + 番号。4製品で共通するシナリオは同じID、別runtime/access scenarioは別ID |
| OS / ABI | OS系統、CPU ABI、libcまたはOS runtime | host名、依存、実施状態 | `OS / CPU ABI / libc・runtime` の順。根拠文書にlibc記載がない場合は `未特定` |
| 依存 / driver / permission | 実行時USB library、OS側USB access手段と権限条件 | firmware、CARD API、検証結果 | `libusb:`、`USB access:`、`FD handoff:` の属性名を使う |
| firmware / 補助data | 機器へ渡す実行時firmwareと、RF channelとは別の機種固有データ | RF試験channel、archiveへの同梱・埋込完了、candidate実績 | `firmware:` と `補助data:` を分け、ない属性は `なし` |
| RF試験入力（system / channel） | 実際に受信させる放送systemと現地channel | firmware、機種data、candidate実績 | `system:` と `channel:`。対応波がない場合は `なし（理由）` |
| 製品CLIからの内蔵CARD操作 | 製品固有CLI/APIの操作範囲 | PC/SC/WinSCard adapter、実機合否 | 操作名とmodel依存条件。機能なしは `なし` |
| native PC/SC/WinSCard adapter | その行のOS標準APIへ接続する製品adapterの種類・配布形態 | 製品CLI CARD、adapter実機試験結果、他OSのadapter | OS/ABI行に該当する`Linux PC/SC:`、`macOS PC/SC:`、`Android:`または`Windows WinSCard:`だけを記載 |
| 配布artifact / 手順 | 対象archive識別子とこの正本内の該当手順 | artifact hash、host、試験結果 | `artifact: <target>; procedure: [archive別手順](#3-配布アーカイブ別手順)` の順 |

共通IDは E02=Linux/x86_64/musl、E03=Linux/x86_64/glibc、E04=macOS/arm64、E05=Android/aarch64、E06=Android/armv7a、E07=Android/x86_64、E15-G=Linux/aarch64/glibc、E15-M=Linux/aarch64/musl、E17=Windows 11/x86_64を表す。IDはhost名や試験済み状態を表さない。SianoのLinux binaryは各architectureにつき1つの静的musl archiveであり、glibc/musl別artifactではない。

| ID | OS / ABI | 依存 / driver / permission | firmware / 補助data | RF試験入力（system / channel） | 製品CLIからの内蔵CARD操作 | native PC/SC/WinSCard adapter | 配布artifact / 手順 |
|---|---|---|---|---|---|---|---|
| E01 | Linux / x86_64 / glibc | libusb: 静的1.0.30; USB access: HAOS/SCS containerからの許可済みUSB node | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Linux PC/SC: なし | artifact: `linux-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E02 | Linux / x86_64 / musl | libusb: 静的1.0.30; USB access: HAOS Supervisor add-on経路 | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Linux PC/SC: なし | artifact: `linux-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E03 | Linux / x86_64 / glibc | libusb: 静的1.0.30; USB access: native OSの許可済みUSB node | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Linux PC/SC: なし | artifact: `linux-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E04 | macOS / arm64 / Apple system runtime | libusb: 静的1.0.30; USB access: macOSの許可済み経路 | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | macOS PC/SC: なし | artifact: `darwin-arm64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E05 | Android (Termux) / aarch64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Android: なし | artifact: `android-aarch64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E06 | Android (Termux) / armv7a / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Android: なし | artifact: `android-armv7a`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E07 | Android (Termux) / x86_64 / Android NDK ABI | libusb: 静的1.0.30; USB access: Termux USB permission; FD handoff: daemonへ1 FD | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Android: なし | artifact: `android-x86_64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E15-G | Linux / aarch64 / glibc | libusb: 静的1.0.30; USB access: platformのnative USB claimとpermission | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Linux PC/SC: なし | artifact: `linux-aarch64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |
| E17 | Windows 11 / x86_64 / Win32 | libusb: 静的1.0.30; USB access: WinUSB driver | firmware: `isdbt_rio.inp`; 補助data: なし | system: ISDB-T; channel: 現地channel planから選択 | なし | Windows WinSCard: なし | artifact: `windows-x64`; procedure: [archive別手順](#3-配布アーカイブ別手順) |

行は正式配布archiveのnative/canonical runtimeと必要なaccess条件を記す。追加のOS/ABI・access path確認は[移植性確認手順](platforms/portability-checks.md)へ分離する。各版のhost名と実施結果は版別記録に残す。SianoにはCARD、PC/SC、WinSCardのbackendはない。

## 端末nodeと環境状態の退避・復元（T01前）

Termuxの`termux-usb -l`はUSB node path文字列のJSON配列を返す（例: `["/dev/bus/usb/001/004"]`）。配列要素をVID/PID辞書として扱わない。次をbootstrap時と再接続後に毎回実行し、選択結果・一覧をLOGに残す。製品/機種ごとに`TARGET_USB`と`EXPECTED_NODES`を表のUSB IDと当該stage（loader/runtime、単機能/primary+sibling）から設定する。ASICENはstage遷移ごとに期待ID/数を切り替える。0件、JSON型不正、対象一致数の不一致、sysfsで対象pathを対応付けできない場合はpermissionを要求せず停止する。無関係nodeは一覧に記録して対象選択から除外する。

```sh
: "${TARGET_USB:?set model/stage VID:PID, e.g. 3275:0080}"
: "${EXPECTED_NODES:?set exact node count from model/stage}"
case "$TARGET_USB" in [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]) ;; *) echo "invalid VID:PID" >&2; exit 2;; esac
case "$EXPECTED_NODES" in ''|*[!0-9]*|0) echo "invalid expected node count" >&2; exit 2;; esac
termux-usb -l > "$LOG/termux-usb-paths.json"
SYSFS_ROOT=${SYSFS_ROOT:-/sys/bus/usb/devices}
python - "$LOG/termux-usb-paths.json" "$TARGET_USB" "$EXPECTED_NODES" "$LOG/termux-usb-map.json" "$SYSFS_ROOT" <<'PY'
import glob, json, os, re, sys
src, target, expected, out, sysroot = sys.argv[1:]
paths = json.load(open(src, encoding="utf-8"))
if not isinstance(paths, list) or any(not isinstance(p, str) for p in paths):
    raise SystemExit("termux-usb -l must be a JSON array of USB path strings")
matches, rows = [], []
for path in paths:
    m = re.fullmatch(r"/dev/bus/usb/(\d{3})/(\d{3})", path)
    row = {"path": path, "vid_pid": None, "product": None}
    if m:
        bus, dev = (str(int(v)) for v in m.groups())
        for node in glob.glob(sysroot + "/*"):
            try:
                if open(node + "/busnum").read().strip() == bus and open(node + "/devnum").read().strip() == dev:
                    vid = open(node + "/idVendor").read().strip().lower()
                    pid = open(node + "/idProduct").read().strip().lower()
                    row["vid_pid"] = vid + ":" + pid
                    product = node + "/product"
                    row["product"] = open(product).read().strip() if os.path.exists(product) else ""
                    break
            except OSError:
                continue
    rows.append(row)
    if row["vid_pid"] == target.lower():
        matches.append(path)
json.dump({"api_paths": paths, "mapped": rows, "target": target.lower(), "selected": matches}, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
if len(matches) != int(expected):
    raise SystemExit(f"expected {expected} {target} node(s), found {len(matches)}; see {out}")
PY
cat "$LOG/termux-usb-map.json"
python3 -c 'import json,sys; [print(p) for p in json.load(open(sys.argv[1]))["selected"]]' "$LOG/termux-usb-map.json" > "$LOG/termux-usb-selected-paths.txt"
```

Sianoは`TARGET_USB=3275:0080`、`EXPECTED_NODES=1`とする。

結果が期待数と完全一致した場合だけ、JSONの`selected`に記録された各pathへ`termux-usb -r <path>`（または製品固有の`-r -e`）を実行する。USB permission後にdaemonを開始する。再接続時は古いpath/FDを使わず、同じ列挙・sysfs照合を再実行する。sysfs未読・node未対応・無許可候補複数は合格ではなく未達として記録する。

正式検証でE02/E03/E07等により環境状態を変更する場合は、ユーザー承認済みの対象・値・影響範囲を確定し、保護されたrun専用LOGと復元可能な状態保存を準備してから実行する。適用後に必要な試験を行い、終了時に保存値へ戻してreadback/比較する。承認範囲を超える変更やHA Core/mirakcの再起動は行わない。

- **E02 HAOS/Supervisor options:** `/addons/self/info`の`options`は対象addon自身のapp recordに保存されたraw optionsで、`/options/config`のdefaults・secret解決後の値とは異なる。Supervisor APIの実装は[apps.py](https://github.com/home-assistant/supervisor/blob/main/supervisor/api/apps.py#L1620-L1700)および[options endpoints](https://github.com/home-assistant/supervisor/blob/main/supervisor/api/apps.py#L1832-L2015)を参照。対象addon内で認可済みtokenを使い、raw表現はmode 600のprivate backupにだけ保存する。値を画面・共有log・evidenceへ出さない。validateの`valid`がtrue以外、または読み出し/HTTP/JSON処理が失敗した場合はoptions POST前に終了する。承認済み範囲内で対象addonの起動・停止とoptions復元/比較を行い、HA Core/mirakcは再起動しない。
  ```sh
  set -eu
  umask 077
  mkdir -m 700 -p "$LOG/private"
  : "${SUPERVISOR_TOKEN:?run inside the target add-on with its token}"
  API=http://supervisor/addons/self
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" "$API/info" -o "$LOG/private/info.before.json"
  python3 - "$LOG/private/info.before.json" "$LOG/private/options.before.json" <<'PY'
  import json, sys
  x=json.load(open(sys.argv[1], encoding="utf-8")); x=x.get("data", x)
  options=x.get("options")
  if not isinstance(options, dict): raise SystemExit("raw app options unavailable")
  json.dump(options, open(sys.argv[2], "w", encoding="utf-8"), sort_keys=True)
  PY
  chmod 600 "$LOG/private/"*.json
  cp "$LOG/private/options.before.json" "$LOG/private/options.restore.json"
  cp "$LOG/private/options.before.json" "$LOG/private/options.trial.json"
  : "${TRIAL_OPTION_KEY:?set one approved non-secret top-level option key}"
  : "${TRIAL_OPTION_JSON:?set its approved JSON value}"
  python3 - "$LOG/private/options.trial.json" "$TRIAL_OPTION_KEY" "$TRIAL_OPTION_JSON" <<'PY'
  import json, sys
  p, key, value=sys.argv[1:]
  options=json.load(open(p, encoding="utf-8"))
  if key not in options: raise SystemExit("trial key is not an existing option")
  options[key]=json.loads(value)
  json.dump(options, open(p, "w", encoding="utf-8"), sort_keys=True)
  PY
  python3 -m json.tool "$LOG/private/options.trial.json" > /dev/null
  python3 - "$LOG/private/options.trial.json" "$LOG/private/options-trial.request.json" <<'PY'
  import json, sys
  json.dump({"options":json.load(open(sys.argv[1], encoding="utf-8"))}, open(sys.argv[2], "w", encoding="utf-8"))
  PY
  # /options/validate accepts the raw options object; only /options uses the {"options": ...} request schema.
  cp "$LOG/private/options.trial.json" "$LOG/private/options-trial.validate.json"
  chmod 600 "$LOG/private/"*.json
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-trial.validate.json" "$API/options/validate" -o "$LOG/private/options-trial.validation-response.json"
  if ! python3 -c 'import json,sys; x=json.load(open(sys.argv[1])); x=x.get("data",x); sys.exit(0 if x.get("valid") is True else 1)' "$LOG/private/options-trial.validation-response.json"; then echo 'options validation rejected; no trial POST was sent' >&2; exit 1; fi
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-trial.request.json" "$API/options" -o "$LOG/private/options-trial.post.json"
  ```
  run終了後、保存したraw optionsを同じ`/options` endpointへ戻し、`/info`のraw `options`とprivate backupの完全一致を確認する。`/options/config`のvalidated値との比較で復元を判定しない。
  ```sh
  set -eu
  python3 - "$LOG/private/options.restore.json" "$LOG/private/options-restore.request.json" <<'PY'
  import json, sys
  json.dump({"options":json.load(open(sys.argv[1], encoding="utf-8"))}, open(sys.argv[2], "w", encoding="utf-8"))
  PY
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" -H 'Content-Type: application/json' --data-binary @"$LOG/private/options-restore.request.json" "$API/options" -o "$LOG/private/options-restore.post.json"
  curl -fsS -H "Authorization: Bearer $SUPERVISOR_TOKEN" "$API/info" -o "$LOG/private/info.after.json"
  python3 - "$LOG/private/info.after.json" "$LOG/private/options.after.json" <<'PY'
  import json, sys
  x=json.load(open(sys.argv[1], encoding="utf-8")); x=x.get("data", x)
  json.dump(x.get("options"), open(sys.argv[2], "w", encoding="utf-8"), sort_keys=True)
  PY
  cmp "$LOG/private/options.restore.json" "$LOG/private/options.after.json"
  ```


optionsを適用した後と元値へ戻した後に、それぞれ別の認可済みHAOS CLI sessionから対象add-onだけを再起動する。HA Core/mirakcは再起動しない。

```sh
ADDON_SLUG='<target-addon-slug>'
ha addons restart "$ADDON_SLUG"
```

- **E03 Linux module/udev/group:**対象udev rule fileの元内容/不在状態、対象userのsupplementary groups、対象USB nodeのmode/UID/GIDを保存する。`DEVICE_NODE`と`TARGET_USB`は同一候補機器の実値に限定し、sysfs pathとVID/PIDが一致しない場合は何も適用せず停止する。rule再読込時のtriggerはそのsysfs device pathだけへ指定する。変更は別途承認された試験でのみ実施する。
  ```sh
  set -eu
  TEST_USER='<non-root-test-user>'; LOG='<private-run-log>'
  DEVICE_NODE='/dev/bus/usb/BBB/AAA'; TARGET_USB='<selected-vid>:<selected-pid>'
  for f in /etc/modprobe.d/smsusb-blacklist.conf /etc/udev/rules.d/99-usb-access.rules; do
    n=$(basename "$f")
    if [ -e "$f" ]; then cp -a "$f" "$LOG/$n.before"; printf 'present\n' > "$LOG/$n.state"; else printf 'absent\n' > "$LOG/$n.state"; fi
  done
  id "$TEST_USER" > "$LOG/test-user.before.txt"; PRIMARY_GROUP=$(id -gn "$TEST_USER")
  id -nG "$TEST_USER" | python3 -c 'import sys; p=sys.argv[1]; print(",".join(g for g in sys.stdin.read().split() if g != p))' "$PRIMARY_GROUP" > "$LOG/groups.before.txt"
  test -c "$DEVICE_NODE"
  SYSFS_PATH=$(udevadm info --query=path --name="$DEVICE_NODE")
  SYSFS_DEVICE="/sys$SYSFS_PATH"
  test -f "$SYSFS_DEVICE/idVendor" -a -f "$SYSFS_DEVICE/idProduct"
  ACTUAL_USB="$(cat "$SYSFS_DEVICE/idVendor"):$(cat "$SYSFS_DEVICE/idProduct")"
  test "$(printf '%s' "$ACTUAL_USB" | tr '[:upper:]' '[:lower:]')" = "$(printf '%s' "$TARGET_USB" | tr '[:upper:]' '[:lower:]')"
  stat -c '%a %u %g' "$DEVICE_NODE" > "$LOG/usb-node.before.txt"
  printf '%s\n' "$SYSFS_PATH" > "$LOG/usb-sysfs-path.before.txt"
  # 承認済みrunでのみ適用する。適用は隔離subshell内で行い、途中失敗でも親shellの復元へ戻る:
  apply_trial_access() (
    set -eu
    : "${BLACKLIST_MODULES:?set only the modules required by the selected product/model}"
    : > /etc/modprobe.d/smsusb-blacklist.conf || exit $?
    for module in $BLACKLIST_MODULES; do printf 'blacklist %s\n' "$module" >> /etc/modprobe.d/smsusb-blacklist.conf || exit $?; done
    VID=${TARGET_USB%:*}; PID=${TARGET_USB#*:}
    printf 'SUBSYSTEM=="usb", ATTR{idVendor}=="%s", ATTR{idProduct}=="%s", MODE="0660", GROUP="video"\n' "$VID" "$PID" > /etc/udev/rules.d/99-usb-access.rules || exit $?
    udevadm control --reload-rules || exit $?
    udevadm trigger --action=change "$SYSFS_DEVICE" || exit $?
    udevadm settle || exit $?
    usermod -aG video "$TEST_USER" || exit $?
  )
  if apply_trial_access; then APPLY_RC=0; else APPLY_RC=$?; fi
  printf '%s\n' "$APPLY_RC" > "$LOG/access-apply.rc"
  RESTORE_RC=0
  set +e
  for f in /etc/modprobe.d/smsusb-blacklist.conf /etc/udev/rules.d/99-usb-access.rules; do
    n=$(basename "$f")
    if [ "$(cat "$LOG/$n.state")" = present ]; then
      cp -a "$LOG/$n.before" "$f" || RESTORE_RC=1
      cmp "$LOG/$n.before" "$f" || RESTORE_RC=1
    else
      rm -f "$f" || RESTORE_RC=1
      test ! -e "$f" || RESTORE_RC=1
    fi
  done
  udevadm control --reload-rules || RESTORE_RC=1
  if test -c "$DEVICE_NODE" && test "$(udevadm info --query=path --name="$DEVICE_NODE")" = "$SYSFS_PATH" && test "$(cat "$SYSFS_DEVICE/idVendor"):$(cat "$SYSFS_DEVICE/idProduct")" = "$ACTUAL_USB"; then :; else RESTORE_RC=1; fi
  udevadm trigger --action=change "$SYSFS_DEVICE" || RESTORE_RC=1
  udevadm settle || RESTORE_RC=1
  if set -- $(cat "$LOG/usb-node.before.txt") && test "$#" -eq 3; then
    chown "$2:$3" "$DEVICE_NODE" || RESTORE_RC=1
    chmod "$1" "$DEVICE_NODE" || RESTORE_RC=1
  else RESTORE_RC=1; fi
  if stat -c '%a %u %g' "$DEVICE_NODE" > "$LOG/usb-node.after.txt"; then cmp "$LOG/usb-node.before.txt" "$LOG/usb-node.after.txt" || RESTORE_RC=1; else RESTORE_RC=1; fi
  if GROUPS_BEFORE=$(cat "$LOG/groups.before.txt"); then
    if [ -n "$GROUPS_BEFORE" ]; then usermod -G "$GROUPS_BEFORE" "$TEST_USER" || RESTORE_RC=1; else usermod -G '' "$TEST_USER" || RESTORE_RC=1; fi
    id "$TEST_USER" > "$LOG/test-user.after.txt" || RESTORE_RC=1
    if PRIMARY_AFTER=$(id -gn "$TEST_USER") && id -nG "$TEST_USER" | python3 -c 'import sys; p=sys.argv[1]; print(",".join(g for g in sys.stdin.read().split() if g != p))' "$PRIMARY_AFTER" > "$LOG/groups.after.txt"; then cmp "$LOG/groups.before.txt" "$LOG/groups.after.txt" || RESTORE_RC=1; else RESTORE_RC=1; fi
  else RESTORE_RC=1; fi
  printf '%s\n' "$RESTORE_RC" > "$LOG/access-restore.rc"
  set -e
  if [ "$APPLY_RC" -ne 0 ]; then exit "$APPLY_RC"; fi
  if [ "$RESTORE_RC" -ne 0 ]; then echo 'access state restoration incomplete; retain evidence and stop' >&2; exit "$RESTORE_RC"; fi
  ```

- **E07 Android stay-awake:**初回ADB readが非0/空/不正値なら書込み前に停止する。`null`は未設定、十進整数は保存値として区別する。権限・readback失敗を無視しない。正式検証では承認済み端末で設定を適用し、試験後に保存値へ戻してreadback/比較する。synthetic fixtureは局所確認に限り、実端末での検証を代替しない。
  ```sh
  set -eu
  LOG='<private-run-log>'
  if ! BEFORE=$(adb shell settings get global stay_on_while_plugged_in); then echo 'initial Android settings read failed; no write attempted' >&2; exit 1; fi
  BEFORE=$(printf '%s' "$BEFORE" | tr -d '\r')
  case "$BEFORE" in null) printf 'absent\n' > "$LOG/stay-awake.before-state.txt";; ''|*[!0-9]*) echo 'initial Android settings value is invalid; no write attempted' >&2; exit 1;; *) printf 'present\n' > "$LOG/stay-awake.before-state.txt";; esac
  printf '%s\n' "$BEFORE" > "$LOG/stay-awake.before.txt"
  : "${TRIAL_VALUE:?set an approved decimal test value}"
  case "$TRIAL_VALUE" in ''|*[!0-9]*) echo 'trial value must be decimal' >&2; exit 2;; esac
  if adb shell settings put global stay_on_while_plugged_in "$TRIAL_VALUE"; then TRIAL_RC=0; else TRIAL_RC=$?; fi
  if [ "$BEFORE" = null ]; then adb shell settings delete global stay_on_while_plugged_in; else adb shell settings put global stay_on_while_plugged_in "$BEFORE"; fi
  if ! AFTER=$(adb shell settings get global stay_on_while_plugged_in); then echo 'Android settings readback failed' >&2; exit 1; fi
  AFTER=$(printf '%s' "$AFTER" | tr -d '\r')
  case "$AFTER" in null) printf 'absent\n' > "$LOG/stay-awake.after-state.txt";; ''|*[!0-9]*) echo 'Android settings readback is invalid' >&2; exit 1;; *) printf 'present\n' > "$LOG/stay-awake.after-state.txt";; esac
  printf '%s\n' "$AFTER" > "$LOG/stay-awake.after.txt"
  cmp "$LOG/stay-awake.before-state.txt" "$LOG/stay-awake.after-state.txt"
  cmp "$LOG/stay-awake.before.txt" "$LOG/stay-awake.after.txt"
  test "$TRIAL_RC" -eq 0
  ```

## 共通試験シナリオ T00–T14

### run別candidateの取得と作業directory

手順書commitとcandidate source SHAは一致不要。成功した`release.yml` runのhead SHA、attempt、結論、artifact ID/name/expiryを照合し、版別matrixのtargetを選ぶ。手動dispatchや正式Releaseを候補代用に実行しない。

```sh
set -eu
gh run list --repo Khronos31/siano-userland --workflow release.yml --limit 20
RUN_ID='<successful-candidate-run-id>'; TARGET='<linux-x86_64|linux-aarch64|darwin-arm64|android-aarch64|android-armv7a|android-x86_64|windows-x64>'
RUN_JSON=$(gh run view "$RUN_ID" --repo Khronos31/siano-userland --json headSha,attempt,conclusion)
SOURCE_SHA=$(printf '%s\n' "$RUN_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["headSha"])')
printf '%s\n' "$RUN_JSON" | python3 -c 'import json,sys; r=json.load(sys.stdin); assert r["conclusion"] == "success" and r["headSha"] and r["attempt"] > 0'
test -n "$SOURCE_SHA"
gh api "repos/Khronos31/siano-userland/actions/runs/$RUN_ID/artifacts" --jq '.artifacts[] | [.id,.name,.expired] | @tsv'
WORK=$(mktemp -d "${TMPDIR:-$HOME}/siano-run-${RUN_ID}.XXXXXX"); chmod 700 "$WORK"; mkdir -m 700 "$WORK/download"
gh run download "$RUN_ID" --repo Khronos31/siano-userland --name release-candidate --dir "$WORK/download"
cd "$WORK/download"; sha256sum -c SHA256SUMS || shasum -a 256 -c SHA256SUMS
VERSION='<candidate-version>'; ARCHIVE="siano-ts-$VERSION-$TARGET.tar.gz"
if [ "$TARGET" = windows-x64 ]; then ARCHIVE="siano-ts-$VERSION-windows-x64.zip"; fi
mkdir "$WORK/candidate"; case "$ARCHIVE" in *.tar.gz) tar -xzf "$WORK/download/$ARCHIVE" -C "$WORK/candidate";; *.zip) unzip -q "$WORK/download/$ARCHIVE" -d "$WORK/candidate";; esac
D="$WORK/candidate"; LOG="$WORK/log"; RT="$WORK/runtime"; mkdir -m 700 "$LOG" "$RT"; cd "$D"; export PATH="$D:$PATH"
python3 -c 'import json,sys; m=json.load(open("manifest.json")); assert m.get("source_ref")==sys.argv[1] and m.get("platform")==sys.argv[2]' "$SOURCE_SHA" "$TARGET"
```

Windows PowerShell unpacks the candidate ZIP with `Expand-Archive` to a new empty `$D` and compares `Get-FileHash -Algorithm SHA256` with the matching entry in the downloaded `SHA256SUMS`. On Linux/macOS/Termux verify the outer `SHA256SUMS`, then hash the selected archive and compare it with that manifest entry. Record target archive, artifact ID/name, source/run/attempt, manifest provenance, and archive SHA. Termux uses `$PREFIX/tmp` or `$HOME`, never assumes OS `/tmp`; acquire `termux-usb -l` output and request permission only for the node whose VID/PID/name matches the Siano tuner. Check `gh`, Python 3.11+, `tar`, `unzip`, and SHA tool (`python --version` on Windows); Termux dependencies use `pkg install gh python coreutils tar unzip`. Host package changes require host owner approval. Linux permissions are first observed with `id`, `stat`, and `lsusb -t`; any approved udev/group change saves original state and restores it after the run. HAOS/Supervisor settings are not changed.

Windows PowerShell candidate retrieval and extraction:

```powershell
$ErrorActionPreference = 'Stop'
$RunId = '<successful-candidate-run-id>'; $Target = 'windows-x64'; $Version = '<candidate-version>'
$Work = Join-Path $HOME "siano-run-$RunId"; $Download = Join-Path $Work 'download'; $D = Join-Path $Work 'candidate'
if (Test-Path $Work) { throw "refusing to reuse $Work" }
New-Item -ItemType Directory -Path $Download,$D,(Join-Path $Work 'log'),(Join-Path $Work 'runtime') | Out-Null
$run = gh run view $RunId --repo Khronos31/siano-userland --json headSha,attempt,conclusion | ConvertFrom-Json
if ($run.conclusion -ne 'success') { throw 'candidate run is not successful' }
$CandidateSourceSha = $run.headSha
gh api "repos/Khronos31/siano-userland/actions/runs/$RunId/artifacts" --jq '.artifacts[] | [.id,.name,.expired] | @tsv'
gh run download $RunId --repo Khronos31/siano-userland --name release-candidate --dir $Download
$ZIP = Join-Path $Download "siano-ts-$Version-$Target.zip"
if (-not (Test-Path $ZIP)) { throw "candidate ZIP missing: $ZIP" }
$sum = Get-Content (Join-Path $Download 'SHA256SUMS') | Where-Object { $_ -match "  $([regex]::Escape((Split-Path $ZIP -Leaf)))$" }
if (-not $sum) { throw 'candidate ZIP checksum entry missing' }
$expected = ($sum -split '\s+')[0]; $actual = (Get-FileHash -Algorithm SHA256 $ZIP).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'candidate ZIP checksum mismatch' }
Expand-Archive -LiteralPath $ZIP -DestinationPath $D
$manifest = Get-Content (Join-Path $D 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.source_ref -ne $CandidateSourceSha) { throw 'candidate source commit mismatch' }
$LOG = Join-Path $Work 'log'; $RT = Join-Path $Work 'runtime'; Set-Location $D
```

各IDを同じ順序・同じ6項目で実施する。$RT/$LOG/$BUS-PORT/$CHANNEL_PLANはrunごとの実値に置換し、空のcandidate専用runtime/logを使う。物理操作を依頼する各回は既定通知を行い、最大5分で応答がなければ未完了として停止する。OS/API固有の長い操作は同じT-IDから後続recipeを参照する。

### T00 候補/配布物固定

- **適用対象/頻度:** 毎releaseのcandidate作成時。
- **準備:** source SHA、CI run/attempt、archive targetとhashを固定。POSIXではgit rev-parse HEADを保存し、macOSはshasum -a 256、Linuxはsha256sum、WindowsはGet-FileHashでhashを確認する。
- **実操作/利用者への依頼:** 固定したsource commitとCI runから候補archiveを取得し、macOSでは`shasum -a 256 -c SHA256SUMS`、Linuxでは`sha256sum -c SHA256SUMS`、WindowsではGet-FileHashでarchive hashを照合する。対象archive個別のdigestもmacOSでは`shasum -a 256 "$ARCHIVE"`、Linuxでは`sha256sum "$ARCHIVE"`で表示し、manifest entryと一致することを確認する。archive manifest/source refがcandidate commit/runと一致することを確認する。
- **期待値/終了値:** provenance/hash一致の候補だけ次へ進む。候補不在は未完了。
- **停止/cleanup:** deviceを開かず、archive/logを保持。
- **記録:** source SHA、CI run/attempt、artifact ID/target/hash、取得日時、未取得理由。

### T01 実行環境・アクセス権・入力

- **適用対象/頻度:** 全native archive。初回は全modelごと。
- **準備:** E-ID/OS/ABI、USB access、firmware/model data、channel planを環境表と照合。
- **実操作/利用者への依頼:** Linux uname -a/uname -m/ldd --version、macOS sw_vers、Android getprop、Windows PowerShell OS。firmware isdbt_rio.inpとUSB portを確認。
- **期待値/終了値:** OS/ABIがarchiveに一致し、アクセス許可/入力が揃う。不一致はdevice操作前に止める。
- **停止/cleanup:** 権限不足やdriver変更が必要なら操作せず未実施記録。
- **記録:** E-ID/host/OS/ABI/model、USB topology/permission、firmware/data、channel plan。

### T02 列挙・selector・ready確認

- **適用対象/頻度:** 毎release、対象model/device。
- **準備:** 候補archiveのlist-json/models入口とdevice selectorを準備しdaemon専用runtimeを作る。
- **実操作/利用者への依頼:** sianod --list-json; sianod --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT"; sianoctl --instance "$INSTANCE" --runtime-dir "$RT" status; sianoctl --instance "$INSTANCE" --runtime-dir "$RT" list。
- **期待値/終了値:** JSON/portが一意、status ready、receiver数が公開能力と一致。不正selectorは成功でない。
- **停止/cleanup:** daemon通常停止しprocess/FD/IPC/socket残留を確認。
- **記録:** JSON、selector、status/list、各exit、residual。

### T03 最大同時受信

- **適用対象/頻度:** 毎release、各modelの公開最大能力。
- **準備:** 各公開receiver/deviceを一意に割り当て、各clientに別TS出力/ログを用意する。`CAPTURE_SECONDS=10`のように正の有限秒数を明示設定し、記録する。
- **実操作/利用者への依頼:** `siano-ts --instance "$INSTANCE" --runtime-dir "$RT" --receiver 0 -c "$CHANNEL" -t "$CAPTURE_SECONDS" -o "$LOG/one.ts"`。receiver 0は1 stream。
- **期待値/終了値:** 各clientの稼働時間が重なり、各exit 0とTS/counter合格。順次取得を同時試験の代用にしない。
- **停止/cleanup:** 全client通常終了後daemon通常停止、残留なし。
- **記録:** model/receiver/system/channel、各start/end/exit、TS/counter、cleanup。

### T04 不足波・全無料channel

- **適用対象/頻度:** 毎releaseは不足波。初回/majorは全機種の代表機で全無料channel各10秒。
- **準備:** 現地channel planとmodel能力から対象system/channel一覧を作る。
- **実操作/利用者への依頼:** ISDB-Tのみ、channelごと "$D/siano-ts" --instance "$INSTANCE" --runtime-dir "$RT" --receiver 0 -c "$CHANNEL" -t 10 -o "$LOG/$CHANNEL.ts"。
- **期待値/終了値:** 各対象channelのTS/exit合格。未取得は未完了、非対応波だけ根拠付きN/A。
- **停止/cleanup:** client wait/exit保存、daemon通常停止。
- **記録:** model/system/channel/全channel list、TS解析/counter/exit、未取得理由。

### T05 受信中の状態確認・制御

- **適用対象/頻度:** 毎release。CARD/controlは製品能力に従う。
- **準備:** capture clientを起動し、別terminalでstatus/list/controlを行えるようにする。
- **実操作/利用者への依頼:** stream中sianoctl status/list。control時は§7.1 S-CTRLの--control client stdinでchannel/tune/quit。
- **期待値/終了値:** 照会/control時刻がstreamと対応し各response/exitを保存。機能外操作はN/A。
- **停止/cleanup:** client/daemonを通常停止し残留確認。
- **記録:** 照会/control時刻、response class、各exit、streamとの重なり。

### T06 CARD物理挿抜・復帰

- **適用対象/頻度:** CARD reader/status対応model。毎release。
- **準備:** CARD reader/APIがないためCARD用daemonやreaderを準備しない。
- **実操作/利用者への依頼:** SianoはCARD reader/status/APIを持たないためCARD挿抜試験は理由付きN/Aとし、利用者へカード操作を依頼しない。
- **期待値/終了値:** CARD非対応のためN/A。reader/APIやCARD由来の終了値は要求しない。
- **停止/cleanup:** daemon通常停止、FD/IPC/runtimeを確認。
- **記録:** 依頼時刻、状態/response/exit、復帰、cleanup。

### T07 USB待機中・受信中の切断と再取得

- **適用対象/頻度:** 毎release。USB対応deviceのidle/streamを別run。
- **準備:** `sianod --list-json`でUSB pathを特定する。USBをdaemon起動時にclaimして保持する実装なので、streamなしのdaemonを起動しstatus/list、daemon PIDとUSB owner状態を記録する。有限capture時間は明示設定する。2-deviceはT12。
- **実操作/利用者への依頼:** idle daemon保持中に利用者へ抜去を依頼し、応答後にstatus/list、daemon診断と実際の終了値を採取する。daemonを通常停止し、再接続を依頼して再列挙したpathで新runtime/daemonを起動し、status/listと短い`S-CTRL` captureを実行する。別runではstream中に抜去を依頼し、client/daemon診断と各実終了値を採取する。
- **期待値/終了値:** idle時はdaemon保持中USBの切断が状態/診断に現れ、process終了値は実値を記録する。stream時は切断診断とclient exit 7を確認する。time満了exit 0は切断合格でない。新daemonでの短TSはexit 0とTS/counter合格。
- **停止/cleanup:** idle/streamそれぞれでowned client/daemonを通常停止してwaitし、再接続後の新daemonも通常停止する。Androidはpermission/FD再取得。
- **記録:** idle/stream、daemon USB owner状態、依頼/応答時刻、path、status/診断、各実終了値、再取得TS/counter、cleanup。

OS/ABI別の具体操作は [正式アーカイブの環境別手順](#siano-os-t07-recipes) を参照する。

### T08 通常停止・cleanup

- **適用対象/頻度:** 全test終了時。
- **準備:** 各owned client/daemon PIDとruntimeを記録してから起動する。
- **実操作/利用者への依頼:** client signal/EOF後wait、daemon SIGINT/console stop後wait。client/daemon終了値は別々に記録。
- **期待値/終了値:** 全owned process終了、TS close、FD/IPC/socket/runtime残留なし。timeoutはfail。
- **停止/cleanup:** 無関係processへsignalしない。test専用runtimeだけ片付け失敗log保持。
- **記録:** PID/role/exit、process/FD/IPC/socket/runtime、復旧状態。

### 共通helperの固定取得（T09/T15前提）

T09を実行する前に、候補sourceとは独立した共通runbook helperをSiano runbookのimmutable commit `571491777d93cfbfb9b7f6238f2ab6c368b4e46d`から取得する。candidate checkout/archive内のhelper有無に依存しない。POSIX/Termuxでは次を一度実行し、以後の全captureで`$TOOL/ts-check.py`を使う。

```sh
RUNBOOK_REF='571491777d93cfbfb9b7f6238f2ab6c368b4e46d'; HELPER_SHA256='22d06db9833b7bfd74f6da76e6e229fba5a005913630c37bd601626fac47b566'
TOOL="$WORK/tooling"; mkdir -m 700 -p "$TOOL"
gh api -H 'Accept: application/vnd.github.raw' "repos/Khronos31/siano-userland/contents/docs/release-validation/ts-check.py?ref=$RUNBOOK_REF" > "$TOOL/ts-check.py"
if command -v sha256sum >/dev/null 2>&1; then printf '%s  %s\n' "$HELPER_SHA256" "$TOOL/ts-check.py" | sha256sum -c -; else printf '%s  %s\n' "$HELPER_SHA256" "$TOOL/ts-check.py" | shasum -a 256 -c -; fi
printf '%s\n' "$RUNBOOK_REF $HELPER_SHA256" > "$LOG/ts-helper-version.txt"
QUALITY_REF='a41e1ff9bd059b1d77441664380a338de683cd9b'; QUALITY_SHA256='69db89cbae511fe849e3eec48524ed8177490002a5c18109acbbeda77a71e57d'
gh api -H 'Accept: application/vnd.github.raw' "repos/Khronos31/siano-userland/contents/docs/release-validation/ts-quality-check.py?ref=$QUALITY_REF" > "$TOOL/ts-quality-check.py"
if command -v sha256sum >/dev/null 2>&1; then printf '%s  %s\n' "$QUALITY_SHA256" "$TOOL/ts-quality-check.py" | sha256sum -c -; else printf '%s  %s\n' "$QUALITY_SHA256" "$TOOL/ts-quality-check.py" | shasum -a 256 -c -; fi
printf '%s\n' "$QUALITY_REF $QUALITY_SHA256" > "$LOG/ts-quality-helper-version.txt"
```

Windows PowerShell 5.1でも同じ固定版を新しいtooling directoryへ取得し、hash一致後にのみ使う。

```powershell
$RunbookRef = '571491777d93cfbfb9b7f6238f2ab6c368b4e46d'; $HelperSha256 = '22d06db9833b7bfd74f6da76e6e229fba5a005913630c37bd601626fac47b566'
$Tool = Join-Path $Work 'tooling'; New-Item -ItemType Directory -Path $Tool | Out-Null
$Helper = Join-Path $Tool 'ts-check.py'
Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/Khronos31/siano-userland/$RunbookRef/docs/release-validation/ts-check.py" -OutFile $Helper
$Actual = (Get-FileHash -LiteralPath $Helper -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Actual -ne $HelperSha256) { throw 'runbook TS helper hash mismatch' }
"$RunbookRef $Actual" | Set-Content (Join-Path $LOG 'ts-helper-version.txt')
$QualityRef = 'a41e1ff9bd059b1d77441664380a338de683cd9b'; $QualitySha256 = '69db89cbae511fe849e3eec48524ed8177490002a5c18109acbbeda77a71e57d'
$QualityGate = Join-Path $Tool 'ts-quality-check.py'
Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/Khronos31/siano-userland/$QualityRef/docs/release-validation/ts-quality-check.py" -OutFile $QualityGate
$QualityActual = (Get-FileHash -LiteralPath $QualityGate -Algorithm SHA256).Hash.ToLowerInvariant()
if ($QualityActual -ne $QualitySha256) { throw 'release TS quality helper hash mismatch' }
"$QualityRef $QualityActual" | Set-Content (Join-Path $LOG 'ts-quality-helper-version.txt')
```

helperのversionはrunbook Git commitとSHA-256の組で固定する。candidate source SHA、artifact digest、helper ref/hashを別々に記録する。

### T09 TS・counter・終了値判定

- **適用対象/頻度:** 全capture/soak。
- **準備:** Python 3.11以上を`python3 --version`で確認し、固定取得した`$TOOL/ts-check.py`（Windowsは`$Helper`）を使う。TSDuck 3.45-4798は§6の追加確認にも使う。
- **実操作/利用者への依頼:** 各TSで`python3 $TOOL/ts-check.py "$LOG/<capture>.ts" > "$LOG/<capture>.ts-check.json"`を実行する。§6のTSDuck `tsanalyze --normalized`も実行し、同じTSの結果を併記する。Android TermuxからのcaptureはTermux内でhelperを実行し、TSDuck追加確認用の転送では送受信前後のSHA-256一致を確認する。
- **期待値/終了値:** helperは固定4096 packet chunkでTSを走査し、null PIDはcontinuity判定から除外して`null_packet_count`をJSONへ出す。詳細indexが必要なrunだけ`--null-indices "$LOG/<capture>.null-indices.txt"`を追加し、zero-based indexを別fileへ1行ずつ出力する。出力先とTSを同じpathにしない。同一PIDの隣接payload duplicate pairはPCR timestamp以外の全byte一致時だけ1組を許容し、duplicate indexをJSONへ記録する。3個目、内容差、非連続再送はcontinuity error。retune境界を確認できるrunはzero-basedの最初の出力packet indexを`--retune-boundary-at`へ渡し、境界後の各PID初回packetだけCC比較を除外する。TEI、sync、framing、invalid adaptationは除外しない。PCR reserved bits/extension違反とinvalid adaptationはfail。全TSが製品SPEC基準を満たし通常取得exit 0。非空file/counter 0だけでpassにしない。
- **停止/cleanup:** 元TS/hash/logを残してT08 cleanup。
- **記録:** Python version、helperのGit SHA/hash、TSDuck version、TS hash、両parserの結果/終了値、duplicate/invalid packet index、null packet countと必要時の別index file、sync/TEI/continuity/drop/counter、client exit。

Windowsでの固定helper呼出し。TSDuckの追加確認は§6の同一archive hostまたは転送先で別途実行する。

```powershell
$helper = Join-Path $Tool 'ts-check.py'; $ts = Join-Path $LOG '<capture>.ts'
$json = Join-Path $LOG '<capture>.ts-check.json'; $err = Join-Path $LOG '<capture>.ts-check.err'
& python $helper $ts 1> $json 2> $err; $parserRc = $LASTEXITCODE
$parserRc | Set-Content (Join-Path $LOG '<capture>.ts-check.rc')
if ($parserRc -ne 0) { throw "TS parser failed: $parserRc" }
```

### T10 native PC/SC・WinSCard

- **適用対象/頻度:** 対応OS/APIのみ。非対応製品は理由付きN/A。
- **準備:** native OS adapter/consumerとcandidate IFD/DLL pathを用意。
- **実操作/利用者への依頼:** Siano external PC/SC/WinSCard adapterなし、consumerを起動せずN/A。
- **期待値/終了値:** Sianoはnative PC/SC/WinSCard adapterを持たないため理由付きN/A。reader/APIやtransaction終了値を要求しない。
- **停止/cleanup:** consumerは起動しない。別の試験で起動したdaemonだけを通常停止し、system PCSC serviceを変更しない。
- **記録:** OS/ABI、module/DLL absolute path、consumer、reader/card、operation/exit。

### T11 LNB・非対応要求の拒否

- **適用対象/頻度:** 必要時。給電対応profileだけ実測。
- **準備:** Sianoは衛星/LNB機能がないためmeter/給電設備を接続しない。
- **実操作/利用者への依頼:** LNB/衛星機能なし、操作しない。
- **期待値/終了値:** 衛星/LNB機能がないため理由付きN/A。LNB操作の終了値は要求しない。
- **停止/cleanup:** LNB操作なし。capture/daemonは通常停止する。
- **記録:** model/profile、command/exit、meter値/条件、cleanupまたはN/A根拠。

### T12 複数device・daemon・access

- **適用対象/頻度:** 必要時、複数device/selector/access変更時。
- **準備:** USB port/selectorとinstance/runtimeをdeviceごとに割り当てる。
- **実操作/利用者への依頼:** PORT_A/B別runtime/instanceでsianodとsiano-tsを起動し各TSを保存する。受信中に利用者へAの抜去を依頼し、A/Bのclient終了値とstatusを別々に採取する。再接続後Aを再列挙して新daemon/runtimeから再取得する。
- **期待値/終了値:** Aの切断clientはexit 7、Bの継続clientはexit 0でTS/counter合格。A再接続後の新規受信はexit 0。Bの結果をAの復帰証拠にしない。
- **停止/cleanup:** 各client/daemon個別wait/stopしFD/IPC/socket確認。
- **記録:** device/port/PID/runtime、各exit/継続/再取得/access/cleanup。

### T13 連続受信・頻度

- **適用対象/頻度:** 毎releaseおよび全model初回/major。
- **準備:** 確定頻度表の対象archive/model/timeをmatrixへ展開する。
- **実操作/利用者への依頼:** 各runで適用頻度表の時間を`CAPTURE_SECONDS='<matrix seconds>'; S="$CAPTURE_SECONDS"`へ代入し、`test "$S" -gt 0`の後にS-RXをその秒数以上連続実行する。毎release代表Linux x86_64 glibc 2h/全archive native short; 初回全modelの地上channel 10s、macOS/Windows 30m、他ABI 10m。
- **期待値/終了値:** 指定連続時間完了かつTS条件合格。追加soakは明示ユーザー決定分のみ。
- **停止/cleanup:** T08通常stopと全resource cleanup。
- **記録:** model/OS/ABI/archive、時間、TS/counter、FD/RSS、未実施理由。

### T14 失敗・リリース判定

- **適用対象/頻度:** 全candidate gate完了後。
- **準備:** source/archive SHAと全T-IDの結果を版別記録へ用意。
- **実操作/利用者への依頼:** 各T-IDのstateをversion recordへ記録。過去結果/未実施/失敗を今回passにしない。
- **期待値/終了値:** failure/unsupported/未実施/環境不能/candidate欠落をpassにしない。
- **停止/cleanup:** candidateと失敗証拠を保持。tag/releaseは別依頼。
- **記録:** T-ID、candidate/host/model、state、log、blocker、判定根拠。

### T15 release前5分TS品質判定

- **適用対象/頻度:** 毎release。T00で固定した同一candidateを、代表Linux x86_64 glibc、Windows x86_64、macOS arm64で実施する。既存の2時間soak、archive回帰、T09を置き換えない。
- **準備:** 各環境のnative candidate、通常の指定機種PX-S1UD、T03のreceiver/channel条件と新しい`$LOG`を用意する。receiver 0を単独で300秒取得する。
- **実操作/利用者への依頼:** 300秒captureの実開始/終了時刻、client exit、TS hashを記録する。既存T09 `ts-check.py`をcaptureのTSへ実行してJSONと終了値を保存する。parserが非0でも出力を残し、元のT09結果とT15の結果を分ける。scan JSONをquality toolへ一つ渡す。

| profile | pattern / 個別TS | TEI合格 | 採用CC合格 |
|---|---|---:|---:|
| `siano-s1ud` | PX-S1UD receiver 0 | `TEI × 100000 < packets`（0.001%未満） | 10件未満 |

各環境で次を一度実行する。
Windows PowerShellでは`python $QualityGate --profile siano-s1ud --environment windows <scan-json>`、macOSでは`python3 "$TOOL/ts-quality-check.py" --profile siano-s1ud --environment macos <scan-json>`を使い、JSONと終了値を保存する。

```sh
PROFILE='siano-s1ud'; ENVIRONMENT='representative-linux'
if python3 "$TOOL/ts-quality-check.py" --profile "$PROFILE" --environment "$ENVIRONMENT" "$LOG/capture.ts-check.json" > "$LOG/t15-$PROFILE.json"; then T15_RC=0; else T15_RC=$?; fi
printf '%s\n' "$T15_RC" > "$LOG/t15-$PROFILE.rc"
```

- **期待値/終了値:** TEI率はTEI件数/packet数×100で、境界値を含まず整数の元値で判定する。CCは採用`tei_null_excluded`定義。CCは10件未満。0 packet、scan JSON欠落/不正はFAILとし、0%/0件へ補完しない。T15はTEI/CC判定のみで、既存T09 parser exitやsync/framing判定を上書きしない。
- **停止/cleanup:** 自動再取得・自動修復はしない。不合格で停止して人間の逐次判断を待つ。
- **記録:** profile/pattern、環境、実測capture秒数、candidate/archive/helper ref/hash、TS path/hash、packet数/TEI数/TEI率/採用CC、clientとparser終了値、T15 JSON/終了値を版別記録から参照する。人間が不合格を受け入れる場合は理由・判断者・コメント等の参照を別に記録し、測定値・T15 JSONの自動判定を編集しない。

N/Aは製品機能上該当しない項目だけに使う。OS/ABI候補のunsupported、candidate欠落、未実施、失敗、環境不能は版別結果で未達とし、passに読み替えない。手順に記した期待値は実測結果ではない。

## 対象アーカイブ・環境から全試験への対応

各正式archive/E-IDで適用されるT00–T14を独立して実行する。試験項目の準備、操作、期待値/終了値、停止/cleanup、記録は「共通試験シナリオ T00–T14」を唯一の定義とし、下表はarchiveごとのOS/ABI環境手順への参照を示す。異なるarchive、libc、OS/ABI、USB access boundaryの結果で代替しない。機能対象外はT本文と製品固有条件に理由を記し、未実施・unsupported・候補未入手・環境不足は未達として記録する。

T15はこのarchive matrixと別に、指定candidateの代表Linux x86_64 glibc、Windows x86_64、macOS arm64で実施する。T15の3環境は他archive/OS/ABIの結果で代替しない。

| archive / E-ID / 実行環境とprocedure |
|---|
| `linux-x86_64` / [E01](#72-環境別) / Linux x86_64/glibc container profile |
| `linux-x86_64` / [E02](#72-環境別) / Linux x86_64/musl add-on profile |
| `linux-x86_64` / [E03](#72-環境別) / Linux x86_64/glibc native profile |
| `linux-aarch64` / [E15-G](#72-環境別) / Linux aarch64/glibc |
| `darwin-arm64` / [E04](#72-環境別) / macOS arm64 |
| `android-aarch64` / [E05](#72-環境別) / Android Termux/aarch64 |
| `android-armv7a` / [E06](#72-環境別) / Android Termux/armv7a |
| `android-x86_64` / [E07](#72-環境別) / Android Termux/x86_64 |
| `windows-x64` / [E17](#e17-windowsのt00t14操作) / Windows 11 x86_64 |

## 0. 共通配布アーカイブと共通手順

各冊の「共通実行環境表」は、Stable release gateで確認する正式配布artifactとnative/canonical runtimeの対応matrixである。環境IDはOS/ABIとUSB access boundaryを識別し、host名や試験状態を表さない。正式な対象archiveはREADME/SPECと各版のmatrixに従う。

| 共通要素 | 役割 |
|---|---|
| 環境ID | OS/ABIとruntime/access条件の識別子。host名・実施状態は版別記録に残す。 |
| 配布artifact matrix | 上の環境表で正式archiveと試験環境を対応付ける。追加の移植性確認環境はこのmatrixへ含めない。 |
| T00–T14 | 共通の試験項目・操作・期待値・cleanup・記録項目を示す。 |
| 共通コマンド | CLIやOS固有の実コマンドは製品ごとに異なる。各環境表の「配布artifact / 手順」リンク先と続くrecipeを使い、製品間で同じCLIを仮定しない。 |
| 状態語彙 | 実施状態は下の共通語彙を版別記録で使う。試験結果の合否とは別に記録する。 |

### 検証状態の共通語彙

| 状態 | 意味 |
|---|---|
| `継承` | 同一model・runtime/access path・featureで、artifact同一性または非影響の根拠がある既存証拠を適用する。 |
| `今回再検証` | 今回のcandidateで対象を実行し、結果を記録する。 |
| `未認定` | 対象の現行証拠がなく、合格を示せない。 |
| `対象外` | 製品仕様の対象外に限る。未実施・環境不能・candidate欠落・失敗には使わない。 |

## 1. 候補とbaselineを固定する

1. 候補の `VERSION`、source commit、`release.yml` の run を特定する。dirty な作業ツリーや未 push の変更を候補として試験しない。
2. baseline は完全な記録を持つ直近 Stable とする。現在は v0.1.5（その後 v0.1.6–v0.1.9 の tag はあるが実機記録が v0.1.5 で止まっている）。baseline 以後の累積差分を次のように把握する。

   ```sh
   : "${BASELINE:?export the recorded baseline revision}"
   : "${CANDIDATE:?export the fixed candidate revision}"
   case "$BASELINE:$CANDIDATE" in *'<'*|*'>'*|:|:*|*:) echo 'set both actual revisions before comparison' >&2; exit 2 ;; esac
   git diff --stat "$BASELINE" "$CANDIDATE"
   ```
3. 各 archive の member（`siano-ts`、`libusb-1.0.dll`、`firmware/isdbt_rio.inp`、mdev 等）を baseline の実機検証 artifact と比較する。**byte-identical な archive はその実機証拠を直接継承する**。byte が異なる場合は §4 の class で hunk 単位に分類する。
4. 対象 claim ごとに `継承` / `今回再検証` / `未認定` / `対象外` を根拠付きで記録する。時間経過・release 回数だけを理由にした再検証はしない。
5. 前回の長時間試験の日付やrelease数は事実情報として記録できるが、エージェントがsoak有無・時間・OSを決める規則として使わない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。適用対象の各archiveは影響判定に関係なくcandidateそのものを使い、適用するarchive/OS頻度は該当版の `docs/release-validation/<version>.md` に従う。

## 2. source / CI / 候補アーカイブ判定（毎回）

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
5. **同一runのpackage・manifest・checksum整合（必須）:** 同じ成功runのsource jobがsource archiveを2回packageして`cmp`し、binary package stepが7 binary archiveを2回packageしてそれぞれ`cmp`し、Windowsの2回clean buildも成功していることを確認する。同じrunの`release-candidate`から8 archiveを取り出し、binary manifestの`source_ref`とsource manifestの`source_ref`/`resolved_commit`がrunの`headSha`を示し、source manifestの`tree`がそのcommitのtreeであること、各archiveの内側manifest/checksumが全memberの実値と整合すること、外側`SHA256SUMS`が8 archiveを過不足なく列挙して実archive hashと一致することを確認する。これは同一run内の再package確認であり、別runの再現性確認ではない。
6. **同一candidate commitの独立2 run（必須）:** 成功した別run IDの`release-candidate`を別々の空directoryへ取得する。両runの`headSha`は固定candidate commitと同一、run IDは異なり、clean CI runであることを確認し、IDを版別記録の`candidate_run_id`/`reproduction_run_id`へ記録する。各runの外側checksumを各archiveの実hashと照合してから、8 archiveそれぞれと`SHA256SUMS`全体を`cmp`で比較する。8 archiveと外側checksumのいずれか1 byteでも異なれば失敗とし、timestamp等の正規化比較で合格にしない。実効toolchain/build inputが同じと記録できないpairはinconclusiveであり、合格にしない。その場合はそのpairを不合格扱いで通さず、実効入力を揃えた独立2 run pairを取り直す。同じcandidate commitで入力を揃えられない場合は、入力を固定した新candidateを作成し、影響する試験を再実施してから独立pairを取り直す。
7. **結果文書・release notes commit後のtag build member比較（必須）:** 検査6で確認したcandidate commitから、§3の回帰/soak結果を記録した文書commit、release notes commit、tag commitへ進む工程とする。各後続commitで製品code・script・workflow・build input・firmware・VERSIONがcandidateから変わっておらず、差分が明示した文書許可pathのみに限られることを確認する。結果文書commitやrelease notes commitを省略せず、tag commitがcandidateの直後commitであることも要件にしない。tag CIが生成した8 archiveと検査6を通ったcandidateの8 archiveを別directoryへ取得し、archive member inventory、member type、mode、symlink targetを照合したうえで、以下の欄限定許可差分以外のpayload bytesが完全一致することを確認する。tag bundle自身の`SHA256SUMS`は8 archiveの実hashと照合するが、candidateとtag bundleの外側`SHA256SUMS`同士は`cmp`しない。両者のarchive hashと許可差分一覧を記録する。

   Binary archiveでは、`README.md`、`manifest.json`、内側`SHA256SUMS`を既決の範囲で比較対象とし、`manifest.json.source_ref`がtag commit、READMEのfiles記録が実際のREADME bytesと一致することを確認する。`DEPENDENCY-NOTICE.txt`では`source.ref`行だけ、`evidence/binary-audit.json`、`evidence/sianod-binary-audit.json`、`evidence/sianoctl-binary-audit.json`では`source_ref`値だけをtag commitに更新可能とする。`manifest.json.files`ではREADMEのsize/sha256と、上記4 memberの実bytesから再計算されるsha256記録だけを差分可能とし、上記4 memberのsizeと他memberのrecordは一致必須とする。内側`SHA256SUMS`の変更もこれら許可memberの実hashを反映する行に限り、member一覧と全hashの自己整合を確認する。各evidence JSONのschema、platform、binary nameとbinary SHA-256、manifestの他field、firmwareとそのhash、noticeの他行を含む、列挙外の値やpayloadはcandidateと一致しなければならない。許可member自体も指定identity欄以外のbyte変更を認めない。

   Source archive内のpathはすべてarchive root相対で記す。PX4の既決DOCUMENTS/allowed_documentsの文書pathは、Siano source archiveでは`repository/README.md`、`repository/SPEC.md`、`repository/docs/platforms/validation-results.md`、`repository/docs/release-validation.md`、`repository/docs/releasing.md`、`repository/release-notes/<tag>.md`、`repository/release-notes/<tag>.json`に対応し、candidate inventoryに存在するpathだけを適用する。source package rootの`README.md`はこの明示集合に含め、`repository/README.md`のcopyとして両memberのpayload bytesが一致することを必須とする。`BUILD-RELINK.md`は許可pathに含めず候補とのbyte一致を必須とする。source archiveの`source-manifest.json`と内側`SHA256SUMS`もその比較結果と整合しなければならない。結果、tagで差分可能なsource pathはこの明示集合内に限り、files記録は許可文書とnoticeの実hash/size以外すべて一致しなければならない。`source-manifest.json`の`source_ref`、`resolved_commit`、`tree`はtag commit/treeと一致する。`DEPENDENCY-NOTICE.txt`では`source.ref`、`source.resolved_commit`、`source.tree`の3行だけをtag identityへ更新可能とする。source archive内側`SHA256SUMS`は許可された文書/noticeの実hashを反映する行だけが変わり得るため、member一覧と全hashの自己整合を確認する。その他のsource manifest metadataおよびpayloadはbyte一致しなければならない。`docs/`全体、他versionの記録、helper/scriptは許可差分にしない。

   固定docs tree（PR #45 head `4f4c14e`）の当時の`release.yml`は`workflow_dispatch`のみで、tag build/member comparisonと`release_verify.py` CLIは未実装だった。これは現在のmainを説明しない。現在のmain `2c6579b` には`workflow_dispatch`と`v*` tag pushの両方があり、tag workflowが版別記録に固定されたcandidate run IDsを取得し、`scripts/release_verify.py`でcandidate/reproduction/tag bundleのmemberを比較する。0.1.0の記録とPR #45の固定treeは当時の状態として維持し、ここに示す現在main用CLI例は実artifact合格の証拠ではない。1.0.0正式Core tag/pin後の独立8B pairおよび実artifact比較は未完了として扱う。

```sh
set -eu
: "${RUN_A:?set the recorded candidate_run_id}"
: "${RUN_B:?set the recorded reproduction_run_id}"
: "${DIR_A:?set a new empty directory for candidate_run_id}"
: "${DIR_B:?set a new empty directory for reproduction_run_id}"
: "${VERSION:?set the recorded candidate version}"
: "${CANDIDATE_SHA:?set the recorded fixed candidate commit}"
case "$RUN_A:$RUN_B" in *[!0-9:]*|:*|*:) echo 'run IDs must be decimal' >&2; exit 2 ;; esac
test "$RUN_A" != "$RUN_B"; test ! -e "$DIR_A"; test ! -e "$DIR_B"
mkdir -m 700 "$DIR_A" "$DIR_B"
# RUN_A/RUN_B are the exact candidate_run_id/reproduction_run_id recorded for this version.
HEAD_SHA_A=$(gh run view "$RUN_A" --json headSha --jq '.headSha')
HEAD_SHA_B=$(gh run view "$RUN_B" --json headSha --jq '.headSha')
CONCLUSION_A=$(gh run view "$RUN_A" --json conclusion --jq '.conclusion')
CONCLUSION_B=$(gh run view "$RUN_B" --json conclusion --jq '.conclusion')
test "$RUN_A" != "$RUN_B"
test "$HEAD_SHA_A" = "$CANDIDATE_SHA"
test "$HEAD_SHA_B" = "$CANDIDATE_SHA"
test "$CONCLUSION_A" = success
test "$CONCLUSION_B" = success
gh run view "$RUN_A" --json databaseId,attempt,headSha,conclusion,event
gh run view "$RUN_B" --json databaseId,attempt,headSha,conclusion,event
gh run download "$RUN_A" --name release-candidate --dir "$DIR_A"
gh run download "$RUN_B" --name release-candidate --dir "$DIR_B"
for dir in "$DIR_A" "$DIR_B"; do
  if [ "$(uname -s)" = Darwin ]; then
    (cd "$dir" && shasum -a 256 -c SHA256SUMS)
  else
    (cd "$dir" && sha256sum -c SHA256SUMS)
  fi
done
for archive in \
  "siano-ts-$VERSION-linux-x86_64.tar.gz" \
  "siano-ts-$VERSION-linux-aarch64.tar.gz" \
  "siano-ts-$VERSION-darwin-arm64.tar.gz" \
  "siano-ts-$VERSION-android-aarch64.tar.gz" \
  "siano-ts-$VERSION-android-armv7a.tar.gz" \
  "siano-ts-$VERSION-android-x86_64.tar.gz" \
  "siano-ts-$VERSION-windows-x64.zip" \
  "siano-ts-$VERSION-source.tar.gz"; do
  cmp "$DIR_A/$archive" "$DIR_B/$archive"
done
cmp "$DIR_A/SHA256SUMS" "$DIR_B/SHA256SUMS"
: "${TAG_RUN_ID:?export the successful tag workflow run ID}"
: "${TAG_DIR:?export a new empty tag-bundle directory}"
: "${TAG:?export the exact release tag}"
: "${TAG_SHA:?export the exact tag commit SHA}"
case "$TAG_RUN_ID" in ''|*[!0-9]*) echo 'tag workflow run ID must be decimal' >&2; exit 2 ;; esac
python3 - "$TAG_SHA" <<'PY'
import re,sys
if not re.fullmatch(r'[0-9a-fA-F]{40}',sys.argv[1]): raise SystemExit('tag commit SHA must be a 40-character hexadecimal commit')
PY
test ! -e "$TAG_DIR"; mkdir -m 700 "$TAG_DIR"
TAG_RUN_JSON=$(gh run view "$TAG_RUN_ID" --json headSha,conclusion,event)
printf '%s\n' "$TAG_RUN_JSON" | python3 -c 'import json,sys; r=json.load(sys.stdin); assert r["conclusion"] == "success" and r["event"] == "push" and r["headSha"]'
TAG_RUN_SHA=$(printf '%s\n' "$TAG_RUN_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin)["headSha"])')
test "$TAG_SHA" = "$TAG_RUN_SHA"
gh run download "$TAG_RUN_ID" --name release-candidate --dir "$TAG_DIR"
if [ "$(uname -s)" = Darwin ]; then
  (cd "$TAG_DIR" && shasum -a 256 -c SHA256SUMS)
else
  (cd "$TAG_DIR" && sha256sum -c SHA256SUMS)
fi
python3 scripts/release_verify.py \
  --candidate-dir "$DIR_A" \
  --reproduction-dir "$DIR_B" \
  --tag-dir "$TAG_DIR" \
  --tag "$TAG" \
  --candidate-sha "$CANDIDATE_SHA" \
  --tag-sha "$TAG_SHA"
```

CI が行う build・audit・smoke を成功後にローカル再実行しない。新しい汎用検証スクリプトは作らない。

## 3. 配布アーカイブ別手順

1. 毎release、全配布archiveを対応するnative OS/ABIで確認する。下表はarchiveごとの操作方法とnative環境の対応表であり、版別matrixは具体candidateと環境を割り当てる。archiveの対象集合は部分選択にしない。同じx86_64実行ファイルを複数のaccess pathで重複実行する場合はOS/ABIのnative確認と分けて記録する。

| 配布artifact | 環境例 | 使用機器 | 毎release必須の短時間確認 |
|---|---|---|---|
| `linux-x86_64` | E03 AnduinOS x86_64（glibc系host上で配布static binaryを実行） | PX-S1UD 1台 | §7.1aの列挙→接続中受信→USB切断で有限終了→再接続後の再列挙と30秒受信→残留確認 |
| `linux-aarch64` | E15-G Fedora aarch64 / L4T | PX-S1UD 1台 | 同上。現候補archiveをnative実行 |
| `darwin-arm64` | E04 macOS arm64 | PX-S1UD 1台 | 同上。macOS手順のhash/依存確認も含む |
| `android-aarch64` | E05 Termux aarch64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-armv7a` | E06 Termux armv7a | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `android-x86_64` | E07 Bliss OS Termux x86_64 | PX-S1UD 1台 | 同上。Termux USB permission / `--fd` handoffを含む |
| `windows-x64` | E17 Windows 11 x86_64 / WinUSB | PX-S1UD 1台 | 同上。Windows固有手順で列挙、30秒受信、停止、切断/再接続、再受信 |

2. 必須確認の一連の操作に総時間上限は設けない。候補archiveのhash確認・展開、firmware hash確認、アンテナ/電源、host準備、ログ先作成は検証開始前に済ませる。`sianod --list-json`でUSB discoveryを確認し、daemon起動後は`sianoctl status/list`、単体receiverの短時間受信、通常client停止、USB切断終了と再接続後の再列挙/受信、process残留なしを確認する。USB抜差しはユーザーが実施する。各物理操作を依頼する直前にHAOS/SCS側でCodexは`beep`、Claude Codeは`vibe`を実行し、依頼した時点から最大5分待つ。5分以内に操作が行われなければ、その操作を未完了として記録し、物理deviceを使う工程を中断する。各archiveで別々のrunを記録し、別artifactや過去candidateの結果を今回の結果に代用しない。
3. 30秒受信のTSは§6で検査する。毎release必須の代表Linux x86_64 2時間soakと初回/major時間条件は共通頻度表に従う。これらとは別の5分を超える追加連続負荷は短時間確認に混ぜず、§5のsoakとしてユーザーの決定を待つ。各runの記録にはarchive名/SHA-256、source commit、環境ID/OS version/architecture、USB `port=`、実施時刻（UTC）、コマンド、終了値、TS検査結果、残留の有無を残す。
4. host/deviceが使えない、archiveが起動しない、または必須物理操作を実施できない場合は該当artifactのgate未完了とする。環境を別のOS/ABIで黙って代替せず、ユーザーに状況と必要な選択肢を提示する。

## 4. 変更分類と対象別適格性確認

v0.1.5..v0.1.9 のように差分が大きい場合は、変更したファイル名だけでなく hunk 単位で分類する。`siano-ts.c` の `#ifdef _WIN32` / `#if defined(__APPLE__)` / `SIANO_HAVE_WRAP_SYS_DEVICE` など platform guard 内の hunk はその platform のみに作用する。v0.1.5..v0.1.9 には platform-guarded change（`__APPLE__` の `MAX_URBS`、`_WIN32` の `--fd` 拒否、`control-input.c` の Windows pipe 等）が含まれるため、必ず guard ごとに判定する。

| # | class（現行コード例） | 追加で見る機能 | 対象環境の候補 | 台数の条件（機能に応じて確認） | 安定性影響の参考（soakはユーザー決定） |
|---|---|---|---|---|---|
| C1 | USB transport: ring/transfer size、event thread、async completion（`siano-ts.c` の `MAX_URBS` 等） | 30秒受信、切断/再接続 | guard があればその platform（`__APPLE__`→E04）。なければ該当matrix環境 | 1。変更機能に2台経路があればその構成 | 連続受信・transport寿命 |
| C2 | libusb version、static/dynamic link、link option | 列挙、firmware、30秒受信、切断/再接続 | rebuildされたbinaryを含む配布matrix | 1（列挙選択変更では2台の列挙も含める） | USB初期化・転送寿命 |
| C3 | compiler/toolchain/SDK/NDK/runner image、`packaging/windows-baseline.sha256` 変更 | 30秒受信、正常停止 | rebuildされたbinaryを含む配布matrix | 1 | ABI/targetへの影響 |
| C4 | platform shim: `siano-os.h`、Windows pipe（`control-input.c`）、QPC clock、`--fd`/wrap | shim された feature | Windows→E17。`--fd`/wrap→E05 + E06 + E07 と E03（Termux 3 ABI は artifact が別個） | 1 | platform shim / FD handoff |
| C5 | firmware hash/format/load | firmware load、lock、30秒受信 | 該当binary matrix環境 | 1 | firmware初期化・stream開始 |
| C6 | stream/queue/writer/drop policy、`--fail-on-drop`、clock/`--time`（`queue-policy.c`、`write-policy.h`、`stream-state.c`） | counter、exit 8 semantics、30秒受信。slow-sinkはoffline test | 該当binary matrix環境 | 1（queue共有/競合変更なら同時構成も判断材料） | stream/queue/writer持続動作 |
| C7 | device enumeration/selection、`--list-json`、USB location（`device-selector.c`、`usb-location.h`） | `sianod --list-json`で2台を列挙し、index/port/bus:address選択、不正selector→exit 3 | E03 + E04 + E17 | 2 | 列挙・識別の経路 |
| C8 | disconnect/hotplug 分類、exit code（`stream-state.c`、`exit-codes.c`） | 受信中に片側切断→有限時間で exit 7、残存側 exit 0、再接続、再実行。1台/1 USB port しかない環境では有限 exit と再接続だけを確認でき、残存側 exit 0 は2台前提のため満たせない | E01–E07、E15-G、E17（正式archiveのnative環境） | 2（Termux/E17 は port が1つなら1。1台 run は full 2台 disconnect test として数えない） | hotplug / cleanupの経路 |
| C9 | kernel-driver detach decision（`detach-decision.h`） | bound 時の拒否→exit 4。live unbind はしない | E03（blacklist 状態は reboot で復元） | 1 | driver bind/accessの安全性 |
| C10 | `--control` parsing と retune | stdin `channel`/`tune`/`quit`、open 中の retune、exit 0 | E03 + E17 | 1 | 受信中controlの継続性 |
| C12 | `tests/test_cli.sh` と unit test で覆われる CLI/引数変更 | CLI動作 | 変更対象に対応するmatrix環境 | 1 | CLI/API互換性 |
| C13 | docs、license、metadata | gate のみ | — | — | 安定性への直接影響なし（追加soakはユーザー指示による） |

platform-only の hunk は、そのplatformのmatrix環境で機能確認を行う。classが不明または複数にまたがる場合は影響し得るpathをユーザー提示用に整理する。追加確認のうち5分を超える連続負荷はsoakとして扱う。毎releaseの代表機2時間と初回/majorの時間条件は「共通の検証項目と頻度」に従い、別途の追加soakが指定された場合はその時間・OSを記録する。物理操作の待機は§3と同じく依頼ごとに最大5分とする。

## 5. 連続受信

短時間回帰とは分けて連続受信を行う。毎release必須の代表Linux x86_64 2時間と初回/majorのmacOS/Windows 30分、残るOS/ABI 10分は「共通の検証項目と頻度」で定める。これらとは別の追加soakはユーザーから指示された場合に実施有無・時間・OSを記録する。

各連続受信では実行archive、OS/ABI、モデル、channel、実行時間、client/daemon終了値、TS品質、process/FD/RSSの観測値、cleanupを記録する。追加試験の選択や、頻度表の対象外へ追加する条件はユーザーの指示に従う。

### 連続受信の内容

- 共通の頻度表またはユーザーの追加指示で指定された時間、OS、topologyを用いる。
- 10秒以下の間隔で RSS と FD 数を記録し、傾向を示す。**数値の合否基準は設けない**。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。
- 各 cycle の出力を §6 で検査する。終了後にprocess残存と`sianoctl status/list`を確認する。
- S1UD は `port=` で個体を選ぶ。ユーザー指定OSで必要な台数/topologyを用意できない場合は、制約を報告してユーザーに次の判断を求める。他OSへの振替やsoak中止をagentが決めない。

## 6. TS 検査

`siano-ts` は in-band の TS 整合性 counter を持たない。出力ファイルを解析 host へコピーし、両 host で SHA-256 が一致することを確認してから検査する。TS 検査に使う TSDuck の version を固定して記録し、`tsanalyze` の field 名を採用時に1回確認する。

1. `stat`（Linux）/ `stat -f`（macOS/BSD）で `size % 188 == 0`（188-byte remainder なし）。
2. 固定helperでinvalid sync 0、TEI 0、PIDごとのcontinuity discontinuity 0。各PIDの先頭packetはcontinuity比較から除外する。`--control` runではper-selectionの実出力packet countからretune境界を算出し、各境界後のPID初回packetだけ`--retune-boundary-at`でCC比較を除外する。TSDuckの未補正continuity表示は診断として保存する。
3. `TS queue dropped` が stderr になく、exit code が 8 でないこと。正常は exit 0。

<a id="siano-os-t07-recipes"></a>
## 7. 正式アーカイブの環境別手順

各項は次の共通部品と追加手順で構成する。物理操作（USB 抜差し、アンテナ、電源、WinUSB 割当、kernel module blacklist 変更）はユーザーが事前確認のうえ実施する。E01/E02 は production host であり、Supervisor 状態変更は承認・退避・復元を伴う。HA Core・アドオン・mirakc・サービスを再起動しない。

### 7.1 共通部品

```sh
set -eu
D="$WORK/candidate"; DAEMON="$D/sianod"; C="$D/sianoctl"; B="$D/siano-ts"
FW="$D/firmware/isdbt_rio.inp"; LOG="$WORK/log"; RT="$WORK/runtime"; INSTANCE='s1ud'
CAPTURE_SECONDS='10'; S="$CAPTURE_SECONDS"; PORT='1-2.3'
require_value() { name=$1 value=$2; case "$value" in ''|*'<'*|*'>'*) echo "invalid or unreplaced $name" >&2; return 1 ;; esac; }
for name in WORK D FW LOG RT INSTANCE CAPTURE_SECONDS PORT; do eval "value=\$$name"; require_value "$name" "$value"; done
case "$S" in *[!0-9]*|0) echo 'capture duration must be positive integer seconds' >&2; exit 2 ;; esac
# 10秒は短いTS確認用。頻度表の連続受信は代表機7200秒、macOS/Windows 1800秒、他OS/ABI 600秒をCAPTURE_SECONDSへ設定し、短時間確認で代用しない。
mkdir -m 700 -p "$RT" "$LOG"; test -x "$DAEMON"; test -x "$C"; test -x "$B"; test -s "$FW"
if [ "$(uname -s)" = Darwin ]; then
  printf '%s  %s\n' '054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818' "$FW" | shasum -a 256 -c -
else
  printf '%s  %s\n' '054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818' "$FW" | sha256sum -c -
fi
"$DAEMON" --list-json > "$LOG/discovery.json"  # enclosures[].devices[].portからPORTを選択
python3 - "$LOG/discovery.json" "$PORT" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8'))
if not any(d.get('port') == sys.argv[2] and d.get('usb') == '3275:0080' for e in x.get('enclosures',[]) for d in e.get('devices',[])):
 raise SystemExit('selected port is not a discovered PX-S1UD 3275:0080')
PY
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  > "$LOG/sianod.out" 2> "$LOG/sianod.err" & DAEMON_PID=$!
cleanup_sianod() {
  original=$?; trap - EXIT
  if kill -0 "$DAEMON_PID" 2>/dev/null; then kill -INT "$DAEMON_PID"; fi
  if wait "$DAEMON_PID"; then daemon_rc=0; else daemon_rc=$?; fi
  echo "$daemon_rc" > "$LOG/sianod.rc"
  if [ "$original" -eq 0 ] && [ "$daemon_rc" -ne 0 ]; then original=$daemon_rc; fi
  exit "$original"
}
trap cleanup_sianod EXIT
i=0; READY=0
while [ "$i" -lt 60 ]; do
  if "$C" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/status.txt" 2> "$LOG/status.err" && grep -q ready "$LOG/status.txt"; then READY=1; break; fi
  kill -0 "$DAEMON_PID" || { echo 'sianod exited before ready' >&2; exit 1; }
  i=$((i + 1)); sleep 1
done
test "$READY" -eq 1 || { echo 'sianod did not become ready' >&2; exit 1; }
"$C" --instance "$INSTANCE" --runtime-dir "$RT" list > "$LOG/list.txt" 2> "$LOG/list.err"
```

USB discoveryの期待値は `sianod --list-json` の `enclosures[].devices[]` にPX-S1UD、USB ID、`port`、受信機情報があること。通常の一覧・稼働状態は `sianoctl list/status` でdaemonへ照会し、discoveryとdaemon状態の記録を区別する。

```sh
# S-RX（$S 秒受信。Sianoのreceiverは0のみ）
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/si.ts" \
  2> "$LOG/si.err"; echo $? > "$LOG/si.rc"
# S-ANA: §6 の 1–3 を si.ts に適用
```

代表機の必須2時間runは、7.1のcandidate daemonを維持した同じshellで短いS-RXとは別に実行する。初回/majorのmacOS・Windowsは1800秒、他OS/ABIは600秒を設定し、OSごとに別runで記録する。

```sh
CAPTURE_SECONDS='7200'; S="$CAPTURE_SECONDS"
case "$S" in ''|*[!0-9]*|0) echo 'soak duration must be positive integer seconds' >&2; exit 2 ;; esac
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/representative-soak.ts" \
  > "$LOG/representative-soak.out" 2> "$LOG/representative-soak.err" || { RC=$?; echo "$RC" > "$LOG/representative-soak.rc"; exit "$RC"; }
echo 0 > "$LOG/representative-soak.rc"
```

```sh
# S-CTRL（daemonは維持し、後からper-selection実出力量でretune境界を確定）
set -eu
CTRL_FIFO="$LOG/ctrl.stdin"
test ! -e "$CTRL_FIFO"; test ! -e "$LOG/ctrl.ts"; test ! -e "$LOG/ctrl-boundaries.txt"
mkfifo "$CTRL_FIFO"
"$B" --instance "$INSTANCE" --runtime-dir "$RT" --control -o "$LOG/ctrl.ts" \
  < "$CTRL_FIFO" 2> "$LOG/ctrl.err" & CTRL_PID=$!
exec 3>"$CTRL_FIFO"
cleanup_control_client() {
  cleanup_rc=$?
  trap - EXIT
  exec 3>&- || :
  if [ -n "${CTRL_PID:-}" ]; then
    kill -INT "$CTRL_PID" 2>/dev/null || :
    if wait "$CTRL_PID"; then CTRL_RC=0; else CTRL_RC=$?; fi
    printf '%s\n' "$CTRL_RC" > "$LOG/ctrl.rc"
    CTRL_PID=
  fi
  rm -f "$CTRL_FIFO"
  exit "$cleanup_rc"
}
trap cleanup_control_client EXIT
sleep 20
printf '%s\n' 'channel 21' >&3
sleep 20
printf '%s\n' 'channel 22' >&3
sleep 20
printf '%s\n' quit >&3
exec 3>&-
if wait "$CTRL_PID"; then CTRL_RC=0; else CTRL_RC=$?; fi
printf '%s\n' "$CTRL_RC" > "$LOG/ctrl.rc"
CTRL_PID=
rm "$CTRL_FIFO"
```

S-CTRLの切替境界はcommand送信時刻/file offsetやcontinuity errorから逆算しない。`ctrl.err`の完了したper-selection統計を使う。各行は`control: selection=<1-based> frequency_hz=<Hz> packets=<packets_written> bytes=<packets_written*188> status=complete`の形式でselection番号・選局周波数・packet/byte数・完了状態を記録する。`control: NOT_READY`、run-error/output-error、`bytes=unknown`、欠落/重複/順序違い、0 packet、stats合計とTSファイルbyte数の不一致はfailまたは判定保留とし、境界除外しない。今回のrelease入力はchannel 21、channel 22の2 selectionなので、後続selection開始indexは先行selectionの`packets`合計であり、周波数は521142857 Hz、527142857 Hzで照合する。境界後はPIDごとに最初に現れるpacketのCC比較だけを除外し、ほかのCC不良は通常どおりfailする。helperのTEI/sync/framing検査は境界でも除外しない。

このper-selection出力はSiano product sourceの修正が必要である。stats-only PR #47単独head `f069205e95868d8d3f6fca8f55fc5e9933573604`の通常`--control`では`NOT_READY`/`bytes=unknown status=run-error`となり、release gateを満たさない。approved minimal profile修復PR #48 head `82d7c5b7db9f15b6f635da0594c0c73d8f8bb74d`はCI/reviewが完了するまでmain/candidateへ採用済みと扱わない。作者のdevice-free CMake runtime fixtureでは2 selectionと合計752 stdout bytes、exit 0を確認した。PR47固定sourceへprofile差分を一時適用した診断fixtureでは`473142857 Hz`と`479142857 Hz`の各2 packet/376 bytes、stdout 752 bytes、exit 0を確認したが、これはrelease channel 21/22の周波数照合でも通常candidate実測でもない。runbookの実機/候補合否は上記channel 21/22条件の実際のcandidate出力にのみ基づく。

```sh
python3 - "$LOG/ctrl.err" "$LOG/ctrl.ts" "$LOG/ctrl-boundaries.txt" <<'PY'
import re, sys
from pathlib import Path
pattern = re.compile(r'^control: selection=(\d+) frequency_hz=(\d+) packets=(\d+) bytes=(\d+) status=complete$')
stats = []
for line in Path(sys.argv[1]).read_text(encoding='utf-8').splitlines():
    if line.startswith('control: selection='):
        match = pattern.fullmatch(line)
        if match is None:
            raise SystemExit('incomplete or failed per-selection statistics')
        selection, frequency, packets, byte_count = map(int, match.groups())
        if byte_count != packets * 188 or packets <= 0:
            raise SystemExit('per-selection packet/byte count is invalid')
        stats.append((selection, frequency, packets, byte_count))
if [row[0] for row in stats] != [1, 2]:
    raise SystemExit('expected exactly two completed selections in order')
if [(row[0], row[1]) for row in stats] != [(1, 521142857), (2, 527142857)]:
    raise SystemExit('selected frequency does not match channel 21 then channel 22')
ts_bytes = Path(sys.argv[2]).stat().st_size
if ts_bytes % 188 or sum(row[3] for row in stats) != ts_bytes:
    raise SystemExit('per-selection byte total does not match the TS file')
boundary = stats[0][2]
if not 0 < boundary < ts_bytes // 188:
    raise SystemExit('retune boundary is outside the TS packet range')
Path(sys.argv[3]).write_text(f'{boundary}\n', encoding='ascii')
PY
CTRL_BOUNDARY=$(cat "$LOG/ctrl-boundaries.txt")
python3 "$TOOL/ts-check.py" "$LOG/ctrl.ts" --retune-boundary-at "$CTRL_BOUNDARY" > "$LOG/ctrl-ts-check.json"
tsanalyze --normalized "$LOG/ctrl.ts" > "$LOG/ctrl-tsanalyze.txt" 2>&1
```

helperの`excluded_cc_mismatches`にはretune境界後、PIDごとの最初のpacketで実際にCC差があったindexだけが残る。境界外CC不良、sync/remainder/TEI/invalid packetはexit 1で拒否される。raw TSDuck continuity値も保存し、helperが記録したPID別境界indexと区別する。

```sh
# S-SIG（clientを止め、daemonは維持）
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -o "$LOG/sig.ts" \
  2> "$LOG/sig.err" & CLIENT_PID=$!
sleep 30
kill -INT "$CLIENT_PID"
if wait "$CLIENT_PID"; then CLIENT_RC=0; else CLIENT_RC=$?; fi
printf '%s\n' "$CLIENT_RC" > "$LOG/sig-client.rc"
"$C" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/status-after-client-stop.txt"
# S-RESID: client processなし、TS出力close、ctl status/listが同じdaemonへ接続。CLIENT_RCはsignal後の実値として記録し、daemon/cleanupの終了値と混ぜない。非zeroでも後続の記録とcleanupを続ける。
```

```sh
# S-DUAL（2台。deviceごとにdaemon/instanceを分け、runtime rootは共有）
set -eu
require_value() { name=$1 value=$2; case "$value" in ''|*'<'*|*'>'*) echo "invalid or unreplaced $name" >&2; return 1 ;; esac; }
"$DAEMON" --list-json > "$LOG/dual-discovery.json"
PORT_A='1-2.3'; PORT_B='1-2.4'
require_value PORT_A "$PORT_A"; require_value PORT_B "$PORT_B"
test "$PORT_A" != "$PORT_B" || { echo 'PORT_A and PORT_B must identify distinct devices' >&2; exit 1; }
python3 - "$LOG/dual-discovery.json" "$PORT_A" "$PORT_B" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8')); devices=[d for e in x.get('enclosures',[]) for d in e.get('devices',[])]
for port in sys.argv[2:]:
 if not any(d.get('port') == port and d.get('usb') == '3275:0080' for d in devices): raise SystemExit(f'not a discovered PX-S1UD: {port}')
PY
if [ "$?" -ne 0 ]; then exit 1; fi
"$DAEMON" --device "usb:$PORT_A" --instance a --firmware "$FW" --runtime-dir "$RT" \
  > "$LOG/daemon-a.out" 2> "$LOG/daemon-a.err" & PID_A=$!
cleanup_dual() {
  original=$?; trap - EXIT
  for entry in "${PID_A:-}:daemon-a" "${PID_B:-}:daemon-b"; do
    pid=${entry%%:*}; name=${entry#*:}
    case "$pid" in ''|*[!0-9]*) continue ;; esac
    if kill -0 "$pid" 2>/dev/null; then kill -INT "$pid"; fi
    if wait "$pid"; then daemon_rc=0; else daemon_rc=$?; fi
    printf '%s\n' "$daemon_rc" > "$LOG/$name.rc"
    if [ "$original" -eq 0 ] && [ "$daemon_rc" -ne 0 ]; then original=$daemon_rc; fi
  done
  exit "$original"
}
trap cleanup_dual EXIT
"$DAEMON" --device "usb:$PORT_B" --instance b --firmware "$FW" --runtime-dir "$RT" \
  > "$LOG/daemon-b.out" 2> "$LOG/daemon-b.err" & PID_B=$!
"$B" --instance a --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/a.ts" > "$LOG/a.out" 2> "$LOG/a.err" & CLIENT_A=$!
"$B" --instance b --runtime-dir "$RT" -c 21 -t "$S" -o "$LOG/b.ts" > "$LOG/b.out" 2> "$LOG/b.err" & CLIENT_B=$!
FAIL=0
if wait "$CLIENT_A"; then RC_A=0; else RC_A=$?; FAIL=1; fi; printf '%s\n' "$RC_A" > "$LOG/a.rc"
if wait "$CLIENT_B"; then RC_B=0; else RC_B=$?; FAIL=1; fi; printf '%s\n' "$RC_B" > "$LOG/b.rc"
test "$FAIL" -eq 0 || { echo 'one or more S-DUAL clients failed; inspect both client logs' >&2; exit 1; }
# 各processが意図したUSB nodeをdaemon経由で所有していることを /proc/<pid>/fd で確認
```

S-DISCはS-DUALとは別runである。§7.1のcandidate path/LOG/runtime値を保持し、今回の`--list-json`から選んだ異なる2 portを`PORT_A`/`PORT_B`として同じshellに設定してから実行する。

```sh
# S-DISC（2台。S-DUALの別run。物理抜去/再接続はユーザーが実施）
require_value() { name=$1 value=$2; case "$value" in ''|*'<'*|*'>'*) echo "invalid or unreplaced $name" >&2; return 1 ;; esac; }
for name in DAEMON B C FW RT LOG PORT_A PORT_B; do eval "value=\${$name-}"; require_value "$name" "$value" || exit 2; done
S=600
"$DAEMON" --list-json > "$LOG/disc-discovery.json"
python3 - "$LOG/disc-discovery.json" "$PORT_A" "$PORT_B" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8')); devices=[d for e in x.get('enclosures',[]) for d in e.get('devices',[])]
if sys.argv[2] == sys.argv[3]: raise SystemExit('PORT_A and PORT_B must be distinct')
for port in sys.argv[2:]:
 if not any(d.get('port') == port and d.get('usb') == '3275:0080' for d in devices): raise SystemExit(f'not a discovered PX-S1UD: {port}')
PY
if [ "$?" -ne 0 ]; then exit 1; fi
"$DAEMON" --device "usb:$PORT_A" --instance a --firmware "$FW" --runtime-dir "$RT" > "$LOG/disc-daemon-a.out" 2> "$LOG/disc-daemon-a.err" & PID_A=$!
"$DAEMON" --device "usb:$PORT_B" --instance b --firmware "$FW" --runtime-dir "$RT" > "$LOG/disc-daemon-b.out" 2> "$LOG/disc-daemon-b.err" & PID_B=$!
"$B" --instance a --runtime-dir "$RT" -c 22 -t "$S" -o "$LOG/disc-a.ts" > "$LOG/disc-a.out" 2> "$LOG/disc-a.err" & CLIENT_A=$!
"$B" --instance b --runtime-dir "$RT" -c 21 -t "$S" -o "$LOG/disc-b.ts" > "$LOG/disc-b.out" 2> "$LOG/disc-b.err" & CLIENT_B=$!
sleep 20
# beep/vibeの直後にユーザーへdevice AのUSB物理抜去を依頼し、依頼時刻と抜去確認時刻を記録する（応答待ちは最大5分）。
# 抜去確認から120秒以内にAが自然終了すること。timeout時は未達として記録し、Aのowned clientをcleanupする。
A_HUNG=1
FAIL=0
for i in $(seq 1 120); do
  A_STATE=$(ps -o stat= -p "$CLIENT_A" | tr -d ' ')
  case "$A_STATE" in ''|Z*) A_HUNG=0; break ;; esac
  sleep 1
done
if [ "$A_HUNG" -eq 1 ]; then kill -TERM "$CLIENT_A"; fi
if wait "$CLIENT_A"; then RC_A=0; else RC_A=$?; fi
printf 'hung=%s exit=%s\n' "$A_HUNG" "$RC_A" > "$LOG/disc-a.rc"
if [ "$A_HUNG" -ne 0 ] || [ "$RC_A" -ne 7 ]; then echo "expected device A client exit 7 inside 120 seconds, got hung=$A_HUNG exit=$RC_A" >&2; FAIL=1; fi
# 期待: hung=0 exit=7。device Bのclientが生存し、A抜去後も継続していることを記録する。
B_STATE=$(ps -o stat= -p "$CLIENT_B" | tr -d ' ')
case "$B_STATE" in Z*|'') B_CONTINUED=0 ;; *) B_CONTINUED=1 ;; esac
printf '%s\n' "$B_CONTINUED" > "$LOG/disc-b-continued-after-a.txt"
if [ "$B_CONTINUED" -ne 1 ]; then echo 'device B client did not remain active after A removal' >&2; FAIL=1; fi
# A daemonを通常停止してwaitし、B client/daemonは停止せずS=600秒完了まで動作させる。
if kill -0 "$PID_A" 2>/dev/null; then kill -INT "$PID_A"; fi
if wait "$PID_A"; then RC_DAEMON_A=0; else RC_DAEMON_A=$?; fi
printf '%s\n' "$RC_DAEMON_A" > "$LOG/disc-daemon-a.rc"
if [ "$RC_DAEMON_A" -ne 0 ]; then echo "device A daemon cleanup exit=$RC_DAEMON_A" >&2; FAIL=1; fi
if wait "$CLIENT_B"; then RC_B=0; else RC_B=$?; fi
printf '%s\n' "$RC_B" > "$LOG/disc-b.rc"
if [ "$RC_B" -ne 0 ]; then echo "device B client exit=$RC_B" >&2; FAIL=1; fi
# 期待: B_CONTINUED=1、B client exit 0。BのTSを§6で検査する。
python3 "$TOOL/ts-check.py" "$LOG/disc-b.ts" > "$LOG/disc-b.ts-check.json"
if kill -0 "$PID_B" 2>/dev/null; then kill -INT "$PID_B"; fi
if wait "$PID_B"; then RC_DAEMON_B=0; else RC_DAEMON_B=$?; fi
printf '%s\n' "$RC_DAEMON_B" > "$LOG/disc-daemon-b.rc"
if [ "$RC_DAEMON_B" -ne 0 ]; then echo "device B daemon cleanup exit=$RC_DAEMON_B" >&2; FAIL=1; fi
# beep/vibeの直後にユーザーへdevice Aの物理再接続を依頼し、依頼/確認時刻を記録する（応答待ちは最大5分）。
"$DAEMON" --list-json > "$LOG/discovery-reconnected.json"
# 新しいportを特定してから、旧runtimeを再利用せず新しいinstance/runtimeでAを再取得する。
PORT_A='1-2.5'
require_value PORT_A "$PORT_A" || exit 2
python3 - "$LOG/discovery-reconnected.json" "$PORT_A" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8'))
if not any(d.get('port') == sys.argv[2] and d.get('usb') == '3275:0080' for e in x.get('enclosures',[]) for d in e.get('devices',[])):
 raise SystemExit('reconnected port is not a discovered PX-S1UD')
PY
if [ "$?" -ne 0 ]; then FAIL=1; exit 1; fi
RT_A_RECOVERY="$RT/a-recovery"
"$DAEMON" --device "usb:$PORT_A" --instance a-recovery --firmware "$FW" --runtime-dir "$RT_A_RECOVERY" 2> "$LOG/disc-recovery-daemon.err" & PID_A_RECOVERY=$!
"$C" --instance a-recovery --runtime-dir "$RT_A_RECOVERY" status > "$LOG/disc-recovery-status.txt"
"$B" --instance a-recovery --runtime-dir "$RT_A_RECOVERY" -c 22 -t 60 -o "$LOG/disc-reconnected-a.ts" 2> "$LOG/disc-reconnected-a.err" & CLIENT_A_RECOVERY=$!
if wait "$CLIENT_A_RECOVERY"; then RC_A_RECOVERY=0; else RC_A_RECOVERY=$?; fi
printf '%s\n' "$RC_A_RECOVERY" > "$LOG/disc-reconnected-a.rc"
# 期待: 新runtimeで60秒受信exit 0、TS §6 pass。その後daemonを通常停止し全process/runtime残留なし。
if kill -0 "$PID_A_RECOVERY" 2>/dev/null; then kill -INT "$PID_A_RECOVERY"; fi
if wait "$PID_A_RECOVERY"; then RC_DAEMON_A_RECOVERY=0; else RC_DAEMON_A_RECOVERY=$?; fi
printf '%s\n' "$RC_DAEMON_A_RECOVERY" > "$LOG/disc-recovery-daemon.rc"
python3 "$TOOL/ts-check.py" "$LOG/disc-reconnected-a.ts" > "$LOG/disc-reconnected-a.ts-check.json"
find "$RT" "$RT_A_RECOVERY" \( -type s -o -type p \) -print > "$LOG/disc-ipc-remnants.txt"
ps -p "$PID_A" -o pid=,stat=,command= > "$LOG/disc-process-a-after.txt" 2>&1 || :
ps -p "$PID_B" -o pid=,stat=,command= > "$LOG/disc-process-b-after.txt" 2>&1 || :
ps -p "$PID_A_RECOVERY" -o pid=,stat=,command= > "$LOG/disc-process-recovery-after.txt" 2>&1 || :
if [ -s "$LOG/disc-ipc-remnants.txt" ]; then echo 'IPC endpoints remain after owned daemon cleanup' >&2; FAIL=1; fi
for path in "$LOG/disc-process-a-after.txt" "$LOG/disc-process-b-after.txt" "$LOG/disc-process-recovery-after.txt"; do
  if [ -s "$path" ]; then echo "owned process remains: $path" >&2; FAIL=1; fi
done
test "$FAIL" -eq 0 || { echo 'S-DISC expected exit/continuation or owned cleanup check failed' >&2; exit 1; }
# 1台/1 USB portの7.1a hotplug試験はこの2台試験の代用にならず、本手順もその代用にならない。
```

### 7.1a 配布archiveごとの必須短時間実機確認（1台）

E01/E02/E03/E04/E15-G/E05/E06/E07/E17の各配布archiveで、各環境固有準備を済ませてから個別に行う。PX-S1UDを接続し、`$DAEMON --list-json`の`enclosures[].devices[]`から`model=PX-S1UD`, `usb=3275:0080`, `port=`を記録する。`sianoctl list/status`のdaemon inventoryがreadyであることも確認する。候補archiveの同じdaemon/clientと同梱firmwareを使う。

```sh
# 接続中受信。-tを付けず、deviceを開いた状態で維持する
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  > "$LOG/hotplug-daemon.out" 2> "$LOG/hotplug-daemon.err" & DAEMON_PID=$!
"$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -o "$LOG/hotplug.ts" \
  > "$LOG/hotplug.out" 2> "$LOG/hotplug.err" & p=$!
sleep 20
# vibe/beep の直後に利用者へUSBを物理的に抜くよう依頼する（応答待ちは最大5分）。
# 抜去の観測から120秒以内に終了し、exit 7であることを確認:
FAIL=0
for i in $(seq 1 120); do kill -0 "$p" 2>/dev/null || break; sleep 1; done
hung=0
if kill -0 "$p" 2>/dev/null; then hung=1; kill -TERM "$p"; fi
if wait "$p"; then rc=0; else rc=$?; fi
echo "hung=$hung exit=$rc" > "$LOG/hotplug.rc"  # 期待: hung=0 exit=7
if [ "$hung" -ne 0 ] || [ "$rc" -ne 7 ]; then echo "expected hotplug client exit 7 inside 120 seconds, got hung=$hung exit=$rc" >&2; FAIL=1; fi
# vibe/beep の直後に再接続を依頼し（応答待ちは最大5分）、再接続された後:
kill -INT "$DAEMON_PID"
if wait "$DAEMON_PID"; then DAEMON_RC=0; else DAEMON_RC=$?; fi
printf '%s\n' "$DAEMON_RC" > "$LOG/hotplug-daemon.rc"
if [ "$DAEMON_RC" -ne 0 ]; then echo "hotplug daemon cleanup exit=$DAEMON_RC" >&2; FAIL=1; fi
"$DAEMON" --list-json > "$LOG/discovery-reconnected.json" # 新しいportを選ぶ
PORT='1-2.5'
case "$PORT" in ''|*'<'*|*'>'*) echo 'replace PORT with the reconnected port from discovery JSON' >&2; exit 2 ;; esac
python3 - "$LOG/discovery-reconnected.json" "$PORT" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8'))
if not any(d.get('port') == sys.argv[2] and d.get('usb') == '3275:0080' for e in x.get('enclosures',[]) for d in e.get('devices',[])):
 raise SystemExit('reconnected port is not a discovered PX-S1UD')
PY
if [ "$?" -ne 0 ]; then FAIL=1; exit 1; fi
"$DAEMON" --device "usb:$PORT" --instance "$INSTANCE" --firmware "$FW" --runtime-dir "$RT" \
  > "$LOG/reconnected-daemon.out" 2> "$LOG/reconnected-daemon.err" & DAEMON_PID=$!
"$C" --instance "$INSTANCE" --runtime-dir "$RT" status > "$LOG/reconnected-status.out" 2> "$LOG/reconnected-status.err"
if "$B" --instance "$INSTANCE" --runtime-dir "$RT" -c 22 -t 30 -o "$LOG/reconnected.ts" > "$LOG/reconnected.out" 2> "$LOG/reconnected.err"; then RC=0; else RC=$?; fi
printf '%s\n' "$RC" > "$LOG/reconnected.rc"
# 期待: 再列挙ready、30秒受信exit 0、TS §6 pass、終了後process残留なし
kill -INT "$DAEMON_PID"
if wait "$DAEMON_PID"; then DAEMON_RC=0; else DAEMON_RC=$?; fi
printf '%s\n' "$DAEMON_RC" > "$LOG/reconnected-daemon.rc"
if [ "$RC" -ne 0 ] || [ "$DAEMON_RC" -ne 0 ]; then echo 'reconnected receive/daemon cleanup failed' >&2; FAIL=1; fi
test "$FAIL" -eq 0 || { echo 'hotplug expected exit or owned cleanup check failed' >&2; exit 1; }
```

一連の操作に総時間上限はない。各物理操作の依頼から操作完了までの待機だけを最大5分とする。抜去後に有限時間内に終了しない場合はhangとして失敗を記録し、processを止めて復旧する。WindowsとTermuxでは次の環境別起動法を使い、同じ物理順・待機条件を守る。2台を使う追加topology試験はこの1台必須試験の代用ではない。

### 7.2 環境別

- **E01 HAOS x86_64 Debian/glibc SCS**: container root で実行し、そのことを記録する（非root claim に使わない）。`ha core info` で HAOS/kernel を記録。`linux-x86_64` を使い、base は §7.1。既存記録は revision がなく current claim ではない。
- **E02 HAOS x86_64 Alpine/musl add-on**: add-on options を現行値で退避し、ユーザー承認のうえ試験用 options で起動、終了後に復元・停止する。restart loop がないことを確認。base は §7.1。`linux-x86_64` と同じ static binary を使う。
- **E03 AnduinOS x86_64**: `smsusb`/`smsdvb`/`smsmdtv` を blacklist し `lsusb -t` で未bind を確認。udev rule と `video` グループで非root 実行。base は §7.1。C9 は bound 状態で `--detach-kernel-driver` なしの拒否（exit 4）だけを確認し、live unbind はしない。blacklist の変更はユーザー確認のうえ reboot で復元する。`linux-x86_64` archiveのnative確認環境例。
- **E04 macOS arm64**: `otool -L ./siano-ts` が system library のみであること、`sw_vers` を記録。base は §7.1。`darwin-arm64` archiveのnative確認環境例。
- **E05–E07 Android Termux**: 共通のnode照合手順で`TARGET_USB=3275:0080`、`EXPECTED_NODES=1`を指定し、JSON path文字列配列をsysfsへ対応付ける。bootstrap/reconnectごとに必ず再実行する。0件・複数一致・無関係nodeのみ・sysfs未対応ではpermissionを求めず停止する。

```sh
TARGET_USB=3275:0080; EXPECTED_NODES=1
# 上の共通node照合を実行してから、JSONのselected pathを使う。
USB_NODE=$(python3 -c 'import json,sys; x=json.load(open(sys.argv[1]))["selected"]; assert len(x)==1; print(x[0])' "$LOG/termux-usb-map.json")
printf '%s\n' "$USB_NODE" > "$LOG/termux-usb-selected.txt"
termux-usb -r "$USB_NODE"
```

daemonのUSB起動は `termux-usb -r -e "$DAEMON --instance $INSTANCE --runtime-dir $RT --firmware $FW" "$USB_NODE"` とし、`termux-usb -e` が渡すFDをdaemonが所有する。別terminalの`$C`/`$B`には同じinstance/runtimeを指定し、clientへUSB FDやfirmwareを渡さない。受信中にユーザーがUSBを抜く。Termux/Android versionを記録。E06はarmv7a、E07はBliss OS x86_64で、E07は検証中だけ画面常時点灯とTermux前面表示を使い`stay_on_while_plugged_in`を元へ戻す。再接続後は上の列挙・一意選択手順を再実行して新nodeを記録し、daemonを同じinstance/runtimeで起動し直して30秒受信する。凍結したrunはinvalid。`--control`はstdinをファイルから渡す。
- 追加の移植性確認（Fedora SELinux、Gentoo、NixOS、Chimera、Alpine/mdev、OpenWrt、FreeBSD、Debian i386）は[移植性確認手順](platforms/portability-checks.md)を参照する。Stable release gateのmatrix・合否対象には含めない。
- **E15-G Fedora aarch64（L4T）**: `linux-aarch64` の canonical runtime。`uname -r` と `getenforce` を記録し、S-RX・S-DUAL・S-DISC を実施する。claim は特定 kernel に限定する。既存記録 `b2c9466` は履歴。
- **E17 Windows 11 x86_64**: product対象。ZIPを展開し、WinUSB割当後にZIP rootの3 programを使う。OS build、Zadig/WinUSB version、各EXEのcandidate固有SHA（同じZIPのmanifestと照合）を記録する。旧`packaging/windows-baseline.sha256`は単体版の履歴であり、現行candidateとの一致を要求しない。USB IDとselectorを確認して、PowerShellで次の操作を行う。S-CTRLは`--control`付きconsumerのconsole stdinから渡す。[今回の統合3program記録](platforms/validation-results.md#2026-10-21-windows統合3program短時間受信)では1台の30秒受信・TS整合性・lease解放・正常停止を確認した。hotplug・soak・最終RC認定は未実施であり、以下の手順の記載自体を成功記録と扱わない。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$CandidateDir = (Get-Location).Path
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$firmwareSha = (Get-FileHash -LiteralPath $FW -Algorithm SHA256).Hash.ToLowerInvariant()
if ($firmwareSha -ne '054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818') { throw 'candidate firmware hash mismatch' }
$INSTANCE = 's1ud'
$LOG = 'C:\siano-e17-<run-id>-logs'
if (Test-Path $LOG) { throw "refusing to reuse existing log path: $LOG" }
New-Item -ItemType Directory -Path $LOG | Out-Null
$RT = 'C:\siano-e17-<run-id>'
if (Test-Path $RT) { throw "refusing to reuse existing runtime path: $RT" }
New-Item -ItemType Directory -Path $RT | Out-Null
$CandidateDir | Set-Content (Join-Path $LOG 'candidate-dir.txt')
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
$RT = 'C:\siano-e17-<run-id>'
$LOG = 'C:\siano-e17-<run-id>-logs'
$CandidateDir = (Get-Content (Join-Path $LOG 'candidate-dir.txt') -Raw).Trim()
if (-not (Test-Path -LiteralPath (Join-Path $CandidateDir 'siano-ts.exe'))) { throw 'saved candidate root is invalid' }
$INSTANCE = 's1ud'
$C = Join-Path $CandidateDir 'sianoctl.exe'
$B = Join-Path $CandidateDir 'siano-ts.exe'
& $C --instance $INSTANCE --runtime-dir $RT status
$hotplugTs = Join-Path $LOG 'hotplug.ts'; $hotplugErr = Join-Path $LOG 'hotplug.err'
$p = Start-Process -FilePath $B -ArgumentList @('--instance',$INSTANCE,'--runtime-dir',$RT,'-c','22','-o',$hotplugTs) -PassThru -NoNewWindow -WorkingDirectory $CandidateDir -RedirectStandardError $hotplugErr
Start-Sleep -Seconds 20
# ここで利用者がUSBを抜く。2分以内に終了し、ExitCode 7であることを確認する
if ($p.WaitForExit(120000)) { $p.Refresh(); $usbRc = $p.ExitCode }
else { $usbRc = 'timeout'; 'HANG: record PID and timeout; return to the owning console and stop with Ctrl+C; do not force-terminate' }
"$usbRc" | Set-Content (Join-Path $LOG 'hotplug.rc')
# daemon consoleでCtrl+C。利用者がUSBを戻してから同consoleで
# --list-jsonでselectorを確認し、上と同じdaemon起動を行う。
# daemonがreadyになったらconsumer consoleで:
& $C --instance s1ud --runtime-dir $RT status
$reconnectedTs = Join-Path $LOG 'reconnected.ts'; $reconnectedErr = Join-Path $LOG 'reconnected.err'
$reconnected = Start-Process -FilePath $B -ArgumentList "--instance $INSTANCE --runtime-dir `"$RT`" -c 22 -t 30 -o `"$reconnectedTs`"" -PassThru -NoNewWindow -WorkingDirectory $CandidateDir -RedirectStandardOutput (Join-Path $LOG 'reconnected.out') -RedirectStandardError $reconnectedErr
if (-not $reconnected.WaitForExit(90000)) {
  $reconnected.Id | Set-Content (Join-Path $LOG 'reconnected-timeout.txt')
  Stop-Process -Id $reconnected.Id -ErrorAction SilentlyContinue
  if (-not $reconnected.WaitForExit(5000)) { $reconnected.Id | Set-Content (Join-Path $LOG 'reconnected-cleanup-timeout.txt') }
}
$reconnected.Refresh()
if ($reconnected.HasExited) { $reconnected.ExitCode | Set-Content (Join-Path $LOG 'reconnected.rc') } else { 'unavailable: owned client did not exit' | Set-Content (Join-Path $LOG 'reconnected.rc') }
if ($reconnected.HasExited -and $reconnected.ExitCode -eq 0 -and (Test-Path $reconnectedTs) -and -not (Test-Path (Join-Path $LOG 'reconnected-timeout.txt'))) { 'complete: recovery client exited 0' | Set-Content (Join-Path $LOG 'reconnected.state') } else { 'incomplete: recovery client timeout/exit/output failure' | Set-Content (Join-Path $LOG 'reconnected.state') }
if (Test-Path $reconnectedTs) { Get-FileHash -Algorithm SHA256 $reconnectedTs } else { 'reconnected TS unavailable' | Set-Content (Join-Path $LOG 'reconnected-ts-missing.txt') }
if (Test-Path (Join-Path $LOG 'reconnected-timeout.txt')) { throw 'reconnected capture timeout; stop the owned daemon normally, verify cleanup, and record T07 incomplete.' }
if (-not $reconnected.HasExited -or $reconnected.ExitCode -ne 0) { throw 'reconnected capture exit unavailable or nonzero; stop the owned daemon normally and record T07 incomplete.' }
Get-Process siano-ts -ErrorAction SilentlyContinue
# 終了時はdaemon consoleでCtrl+C。
```

#### E17 WindowsのT00–T14操作

下表はWindows PowerShellから各T-IDを実行する対応である。終了値・合否・共通cleanup・記録項目は同じ行のT本文を適用する。daemon consoleとclient consoleは分け、両方へ同じ`$RT`/`$INSTANCE`を設定する。`C:\siano-e17-<run-id>`などのrun固有パスは空白なし・未使用であることを確認する。

| T | Windows実操作 | 記録・cleanup |
|---|---|---|
| T00 | 上のZIP SHAとmanifest内3 executable SHA照合を実行する。 | source/CI run/target/hashを保存し、deviceを開かない。 |
| T01 | `$PSVersionTable.PSVersion`、`[Environment]::OSVersion`、`[Environment]::Is64BitProcess`、`Get-PnpDevice -PresentOnly`を保存する。 | E17/Windows build/architecture、USB ID/WinUSB binding、firmware SHA、channel planを保存する。 |
| T02 | `& $D --list-json`、`& $C --instance s1ud --runtime-dir $RT status`、`list`を実行する。 | `enclosures[].devices[].port`とready/listを保存する。daemonは前景consoleでCtrl+C停止する。 |
| T03 | 下のbounded Windows T03 capture recipeを実行する。 | receiver 0の1 stream、exit 0とTS/counterを保存する。timeout/exit値不明/非zeroは未達。 |
| T04 | 下の`$CHANNEL_PLAN`を現地の全無料ISDB-T channelに設定して10秒ずつ取得し、90秒 bounded waitで終了値を確認する。 | channelごとのexit/TS/counterを保存。timeout/cleanup失敗/exit値不明は未達。初回/major以外は毎releaseの不足波を対象にする。 |
| T05 | 下のPowerShell `.NET ProcessStartInfo` control sequenceを実行し、別consoleから`status`/`list`を反復する。 | tune応答/終了値とcapture期間の重なりを保存し、client/daemonを通常終了する。 |
| T06 | CARD APIがないため理由付きN/A。 | reader/cardを作らず、daemonは通常停止する。 |
| T07 | 下のidle保持runと上の有限受信中のUSB抜去/再接続runを別々に行う。 | client/daemonの実終了値・診断・再列挙/再受信・cleanupを保存する。 |
| T08 | client終了後にdaemon consoleでCtrl+Cを入力して`$LASTEXITCODE`を保存し、`Get-Process sianod,siano-ts -ErrorAction SilentlyContinue`、`Get-ChildItem $RT -Force`、daemon停止後の`$C status`を実行する。 | client/daemon exit、process/runtime/TS cleanupとendpoint不在応答を保存する。 |
| T09 | `tsanalyze "$LOG\t03.ts"`を各TSに実行する。 | parser version、TS SHA、sync/TEI/continuity/drop、client exitを保存する。 |
| T10 | native PC/SC/WinSCard adapterがないため理由付きN/A。 | consumerを起動しない。 |
| T11 | 衛星/LNB機能がないため理由付きN/A。 | meter/給電操作なし。 |
| T12 | `$PORT_A/$PORT_B`を分けた2 daemon/instanceで実行し、下のconsole commandを使う。 | A抜去時A exit 7/B継続 exit 0、再接続後A再取得の各状態を別記録する。1台環境は2台試験の代用にしない。 |
| T13 | `$S`を適用頻度表の時間秒へ設定し、下の同じS-RX commandで連続取得する。 | 開始・定期・終了時のTS/counter/FD/RSS/exitを保存する。全archive native shortは独立run。 |
| T14 | version recordにT-IDごとのstateとevidence pathを記録する。 | failure/unsupported/未実施/環境不能/候補欠落は未達とし、candidate evidenceを保持する。 |

T03 Windowsのcaptureは通常の`-t 30`を維持する。clientは90秒以内に終了する必要があり、timeout時はowned clientを停止して5秒以内の終了を待ち、終了値または取得不能状態を記録してT03を未達にする。

```powershell
$ts = Join-Path $LOG 't03.ts'; $clientErr = Join-Path $LOG 't03.err'
$p = Start-Process -FilePath $B -ArgumentList @('--instance',$INSTANCE,'--runtime-dir',$RT,'-c','22','-t','30','-o',$ts) -PassThru -NoNewWindow -WorkingDirectory $CandidateDir -RedirectStandardOutput (Join-Path $LOG 't03.out') -RedirectStandardError $clientErr
if (-not $p.WaitForExit(90000)) {
  $p.Id | Set-Content (Join-Path $LOG 't03-client-timeout.txt')
  Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
  if (-not $p.WaitForExit(5000)) { $p.Id | Set-Content (Join-Path $LOG 't03-client-cleanup-timeout.txt') }
}
$p.Refresh()
if ($p.HasExited -and $p.ExitCode -eq 0 -and -not (Test-Path (Join-Path $LOG 't03-client-timeout.txt'))) { 'complete: client exited 0' | Set-Content (Join-Path $LOG 't03-state.txt') } else { 'incomplete: client timeout/exit failure' | Set-Content (Join-Path $LOG 't03-state.txt') }
if ($p.HasExited) { $p.ExitCode | Set-Content (Join-Path $LOG 't03-client.rc') } else { 'unavailable: owned client did not exit' | Set-Content (Join-Path $LOG 't03-client.rc') }
if (Test-Path $ts) { Get-FileHash -Algorithm SHA256 $ts } else { 't03.ts unavailable' | Set-Content (Join-Path $LOG 't03-ts-missing.txt') }
if (Test-Path (Join-Path $LOG 't03-client-timeout.txt')) { throw 'T03 client timeout; stop the owned daemon normally, verify cleanup, and record T03 incomplete; do not start T04 captures.' }
```

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$INSTANCE = 's1ud'
$RT = 'C:\siano-e17-<run-id>'
$LOG = 'C:\siano-e17-<run-id>-logs'
$CHANNEL_PLAN = @('<free-channel-1>','<free-channel-2>')
foreach ($channel in $CHANNEL_PLAN) {
    $out = Join-Path $LOG ("t04-$channel.ts")
    $p = Start-Process -FilePath $B -ArgumentList "--instance s1ud --runtime-dir `"$RT`" -c $channel -t 10 -o `"$out`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $LOG "t04-$channel.out") -RedirectStandardError (Join-Path $LOG "t04-$channel.err")
    if (-not $p.WaitForExit(90000)) {
        $p.Id | Set-Content (Join-Path $LOG "t04-$channel-timeout.txt")
        Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
        if (-not $p.WaitForExit(5000)) { $p.Id | Set-Content (Join-Path $LOG "t04-$channel-cleanup-timeout.txt") }
    }
    $p.Refresh()
    if ($p.HasExited) { $p.ExitCode | Set-Content (Join-Path $LOG "t04-$channel.rc") } else { 'unavailable: owned client did not exit' | Set-Content (Join-Path $LOG "t04-$channel.rc") }
    if ($p.HasExited -and $p.ExitCode -eq 0 -and (Test-Path $out) -and -not (Test-Path (Join-Path $LOG "t04-$channel-timeout.txt"))) { 'complete: client exited 0' | Set-Content (Join-Path $LOG "t04-$channel.state") } else { 'incomplete: client timeout/exit/output failure' | Set-Content (Join-Path $LOG "t04-$channel.state") }
    if (Test-Path $out) { Get-FileHash -Algorithm SHA256 $out } else { "$channel output unavailable" | Out-File (Join-Path $LOG 't04-ts-missing.txt') }
    if (Test-Path (Join-Path $LOG "t04-$channel-timeout.txt")) { throw "T04 client timeout for $channel; stop the owned daemon normally, verify cleanup, record T04 incomplete, and do not start the next channel." }
}
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $B
$psi.Arguments = "--instance s1ud --runtime-dir `"$RT`" --control -o `"$LOG\t05.ts`""
$psi.UseShellExecute = $false; $psi.RedirectStandardInput = $true; $psi.RedirectStandardError = $true; $psi.RedirectStandardOutput = $true
$control = [System.Diagnostics.Process]::Start($psi)
$stdout = $control.StandardOutput.ReadToEndAsync(); $stderr = $control.StandardError.ReadToEndAsync()
$C = (Resolve-Path .\sianoctl.exe).Path
1..2 | ForEach-Object {
    Start-Sleep -Seconds 20
    & $C --instance s1ud --runtime-dir $RT status 2>&1 | Out-File -Append "$LOG\t05-status.txt"
    $LASTEXITCODE | Out-File -Append "$LOG\t05-status.rc"
    & $C --instance s1ud --runtime-dir $RT list 2>&1 | Out-File -Append "$LOG\t05-list.txt"
    $LASTEXITCODE | Out-File -Append "$LOG\t05-list.rc"
    $control.StandardInput.WriteLine($(if ($_ -eq 1) { 'channel 21' } else { 'channel 22' }))
}
Start-Sleep -Seconds 20; $control.StandardInput.WriteLine('quit'); $control.StandardInput.Close()
if (-not $control.WaitForExit(90000)) {
    $control.Id | Out-File "$LOG\t05-client-timeout.txt"
    Stop-Process -Id $control.Id -ErrorAction SilentlyContinue
    if (-not $control.WaitForExit(5000)) { $control.Id | Out-File "$LOG\t05-client-cleanup-timeout.txt" }
}
$control.Refresh()
if ($control.HasExited) { $stdout.Result | Out-File "$LOG\t05.out"; $stderr.Result | Out-File "$LOG\t05.err"; "$($control.ExitCode)" | Out-File "$LOG\t05.rc" } else { 'unavailable: owned client did not exit' | Out-File "$LOG\t05.rc" }
if (-not $control.HasExited -or (Test-Path "$LOG\t05-client-timeout.txt")) { 'incomplete: control client timeout/exit failure' | Set-Content "$LOG\t05-state.txt"; throw 'T05 control client timeout; stop the owned daemon normally, verify cleanup, and record T05 incomplete.' } else { 'complete: control client exited' | Set-Content "$LOG\t05-state.txt" }
$S = '<seconds from frequency table>'
$soak = Start-Process -FilePath $B -ArgumentList "--instance s1ud --runtime-dir `"$RT`" -c 22 -t $S -o `"$LOG\t13.ts`"" -PassThru -NoNewWindow
while (-not $soak.HasExited) {
    Get-Process -Id $soak.Id | Select-Object Id,ProcessName,WorkingSet64,HandleCount,Threads | Export-Csv -NoTypeInformation -Append "$LOG\t13-process.csv"
    Get-Process sianod -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,WorkingSet64,HandleCount,Threads | Export-Csv -NoTypeInformation -Append "$LOG\t13-process.csv"
    & $C --instance $INSTANCE --runtime-dir $RT status | Out-File -Append "$LOG\t13-status.txt"
    Start-Sleep -Seconds 5
    $soak.Refresh()
}
$soak.Refresh(); "$($soak.ExitCode)" | Out-File "$LOG\t13.rc"
```

```powershell
# T07 idle run: daemonを維持し、capture clientを起動していない状態を確認する。
& $C --instance $INSTANCE --runtime-dir $RT status | Tee-Object "$LOG\idle-before.txt"
Get-Process sianod -ErrorAction SilentlyContinue | Select-Object Id,ProcessName | Out-File "$LOG\idle-owner.txt"
# 利用者へUSB抜去を依頼し、依頼/応答時刻を記録する。
& $C --instance $INSTANCE --runtime-dir $RT status 2>&1 | Tee-Object "$LOG\idle-after.txt"
Get-Process sianod -ErrorAction SilentlyContinue | Select-Object Id,ProcessName | Out-File "$LOG\idle-owner-after.txt"
# daemon consoleへ戻りCtrl+Cし、直後の`$LASTEXITCODE`とprocess終了を記録してからUSB再接続を依頼する。
& $D --list-json | Out-File "$LOG\discovery-reconnected.json"
$selector = 'usb:<port-from-discovery-reconnected.json>'
$RT = 'C:\siano-e17-<run-id>-reconnected'
if (Test-Path $RT) { throw "refusing to reuse existing runtime path: $RT" }
New-Item -ItemType Directory -Path $RT | Out-Null
# 新しいPowerShell daemon consoleで実行し、ready後にconsumer consoleからstatusと30秒受信を行う。
& $D --device $selector --instance $INSTANCE --firmware $FW --runtime-dir $RT
```

T12ではUSB discoveryから`$PORT_A`/`$PORT_B`を異なるportに置換し、各device用runtimeを作ってdaemon consoleを2つ使う。以下を各daemon consoleで別々に前景実行する。clientは同じinstance/runtimeで別consoleから`$B -c 22 -t $S -o ...`を実行する。Aの抜去時はA exit 7とBが継続してexit 0であることを確認し、各consoleをCtrl+Cで通常停止、実終了値を個別保存する。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$PORT_A = '<port-from-list-json-a>'; $PORT_B = '<port-from-list-json-b>'
$RT_A = 'C:\siano-e17-<run-id>-a'; $RT_B = 'C:\siano-e17-<run-id>-b'
New-Item -ItemType Directory -Path $RT_A, $RT_B | Out-Null
```

各PowerShell consoleは独立しているため、以下のdaemon/clientごとに候補rootから実行ファイルを解決し、該当するinstance/runtime/log値を設定する。runtime directoryは起動前に作成する。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$PORT_A = '<port-from-list-json-a>'
$INSTANCE = 'a'
$RT_A = 'C:\siano-e17-<run-id>-a'
New-Item -ItemType Directory -Path $RT_A | Out-Null
& $D --device "usb:$PORT_A" --instance a --firmware $FW --runtime-dir $RT_A
```

別PowerShell console Bで2つ目のdaemonを前景起動する。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$PORT_B = '<port-from-list-json-b>'
$INSTANCE = 'b'
$RT_B = 'C:\siano-e17-<run-id>-b'
New-Item -ItemType Directory -Path $RT_B | Out-Null
& $D --device "usb:$PORT_B" --instance b --firmware $FW --runtime-dir $RT_B
```

client console AでA側を前景実行し、USB抜去時の終了値を保存する。

```powershell
$B = (Resolve-Path .\siano-ts.exe).Path
$LOG = 'C:\siano-e17-<run-id>-logs'
$RT_A = 'C:\siano-e17-<run-id>-a'
$S = '<finite-duration-seconds>'
$client = Start-Process -FilePath $B -ArgumentList "--instance a --runtime-dir `"$RT_A`" -c 22 -t $S -o `"$LOG\a.ts`"" -PassThru -NoNewWindow -WorkingDirectory $CandidateDir -RedirectStandardOutput (Join-Path $LOG 'a.out') -RedirectStandardError (Join-Path $LOG 'a.err')
if (-not $client.WaitForExit(([int]$S + 90) * 1000)) { $client.Id | Set-Content (Join-Path $LOG 'a-timeout.txt'); Stop-Process -Id $client.Id -ErrorAction SilentlyContinue; if (-not $client.WaitForExit(5000)) { $client.Id | Set-Content (Join-Path $LOG 'a-cleanup-timeout.txt') } }
$client.Refresh(); if ($client.HasExited) { $client.ExitCode | Set-Content (Join-Path $LOG 'a.rc') } else { 'unavailable: owned client did not exit' | Set-Content (Join-Path $LOG 'a.rc') }
if (-not $client.HasExited -or $client.ExitCode -ne 7 -or (Test-Path (Join-Path $LOG 'a-timeout.txt'))) { 'incomplete: A client timeout/exit failure' | Set-Content (Join-Path $LOG 't12-state.txt'); throw 'T12 A client timeout/exit failure; stop both owned daemons normally, verify cleanup, record T12 incomplete, and do not start the B client.' } else { 'A exited with expected USB-cut exit 7' | Set-Content (Join-Path $LOG 'a.state') }
```

client console BでB側を前景実行し、A切断中もBが継続して正常終了することを確認する。

```powershell
$B = (Resolve-Path .\siano-ts.exe).Path
$LOG = 'C:\siano-e17-<run-id>-logs'
$RT_B = 'C:\siano-e17-<run-id>-b'
$S = '<finite-duration-seconds>'
$client = Start-Process -FilePath $B -ArgumentList "--instance b --runtime-dir `"$RT_B`" -c 21 -t $S -o `"$LOG\b.ts`"" -PassThru -NoNewWindow -WorkingDirectory $CandidateDir -RedirectStandardOutput (Join-Path $LOG 'b.out') -RedirectStandardError (Join-Path $LOG 'b.err')
if (-not $client.WaitForExit(([int]$S + 90) * 1000)) { $client.Id | Set-Content (Join-Path $LOG 'b-timeout.txt'); Stop-Process -Id $client.Id -ErrorAction SilentlyContinue; if (-not $client.WaitForExit(5000)) { $client.Id | Set-Content (Join-Path $LOG 'b-cleanup-timeout.txt') } }
$client.Refresh(); if ($client.HasExited) { $client.ExitCode | Set-Content (Join-Path $LOG 'b.rc') } else { 'unavailable: owned client did not exit' | Set-Content (Join-Path $LOG 'b.rc') }
if (-not $client.HasExited -or $client.ExitCode -ne 0 -or (Test-Path (Join-Path $LOG 'b-timeout.txt'))) { 'incomplete: B client timeout/exit failure' | Set-Content (Join-Path $LOG 't12-state.txt'); throw 'T12 B client timeout/exit failure; stop both owned daemons normally and record T12 incomplete.' } else { 'complete: B continued and exited 0' | Set-Content (Join-Path $LOG 't12-state.txt') }
```

USBを物理的に外しても有限終了しない場合はhangとして失敗を記録する。各物理操作は依頼直前に通知し、依頼から最大5分待つ。

## 8. USB切断時の期待値

受信中にUSBが切断されたclientはREADMEのUSB error contractに従ってexit 7を返すことを期待する。対象candidateのOS/ABIごとに実測し、他版の終了値で代用しない。

## 9. 記録

検証結果は各版のrelease recordへ記録する。release recordにはversion、candidate/result/tag commit、workflow run ID/attempt、8 archiveとchecksum/audit、baseline tag、各artifactのbyte-identity判定、hunk-levelの変更影響、claimごとの状態、matrix runの環境ID・host・USB `port=`・archive SHA-256・UTC時刻・コマンド・counter・終了コード・ログ保存先、連続受信の実施時間/OS/結果、FD/RSS系列とdisposition、物理操作を行えなかった場合の理由、検査5の同一run package/manifest/checksum結果、検査6の独立run IDと8 archive/外側`SHA256SUMS`のbyte比較、検査7のtag bundle/member比較・許可path/field差分一覧とtag checksum自己整合、未実装gateと残存blocker・既知の制限を記録する。失敗試行は消さず、原因切り分け後の再試験を別試行として残す。受信設備不備や誤ったコマンド等を特定できても、失敗をpassに書き換えない。

次をすべて満たすまで Stable 公開へ進まない。

- CI・artifact audit・source/license・checksum gate が成功。
- final candidateについて、毎release必須の代表Linux x86_64 2時間soakと全配布archiveの短時間matrixが成功している。初回/majorの追加時間条件は共通頻度表に従い、これらとは別の追加soakはユーザーが指定した場合に完了する。soakのFD数・RSSが最終観測まで安定化しない持続増加を示し、外部要因を特定できない場合は`判定保留`とし、公開しない。
- README の各 claim が実証または適格な baseline 継承に対応し、未試験の組合せを認定表示していない。
- crash、hang、stale state、再接続不能、重大な未解決 issue がない。
- 全配布archiveに適用される短時間matrixに未完了のartifactがある場合はStable gate未完了とし、support表示だけで完了扱いしない。
- §2の手順5の同一run package/manifest/checksum gateが成功している。
- §2の手順6で同一candidate commitの独立2 runが8 archiveと外側`SHA256SUMS`まで完全byte一致している。
- §2の手順7のtag build member比較とtag bundle内checksum照合が成功している。tag build/workflow/member比較または検査CLIが未実装・未確認の間はStable gate未完了とし、candidate対tagの外側`SHA256SUMS`の`cmp`で代用しない。
- README、COPYING、LICENCE.siano、checksum、support表示、release archiveの内容が一致し、公開前レビュー済み。

リリースノートには利用者向け変更、短い検証結果、必要な既知制限だけを書く。試験matrix、内部証拠系譜、詳細log、hash一覧は掲載せず、本記録とREADMEへ分離する。

## 変更記録

- v3 (2026-10-25): 各release全配布archiveのnative回帰を維持し、試験結果commit・製品code不変確認後のrelease工程へ共通手順を整合。
- v2 (2026-10-07): 当時の7 archive短時間確認・candidate bytes公開方針を記録。現行release頻度と最終release工程は上記の共通方針に従う。
