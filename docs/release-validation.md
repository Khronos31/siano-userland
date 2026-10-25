# 安定版リリース前検証手順

この文書はSiano Stable releaseで再利用する準備・操作・判定・cleanupの正本です。版ごとの適用matrixと検証記録は [`release-validation/1.0.0.md`](release-validation/1.0.0.md) のように版別文書へ記録します。個別候補の実測値・結果は今回候補の合格を意味しません。

Status: v3 (2026-10-25)

この文書は `siano-userland` の Stable リリースごとの実行手順である。`README.md` の CLI・終了コードと実装が事実の正本であり、本書の記載と食い違う場合は両者を確認して先に本書を直す。Stable releaseは次の順で進める。

1. 候補source commitを固定してRC candidateをbuildする。
2. candidateで回帰・soakを完了する。毎releaseの回帰項目と適用頻度は「共通の検証項目と頻度」に従う。
3. 試験結果をversion別文書へ記録してcommitする。
4. 候補sourceから製品codeの変更がない文書only commitであることを確認し、そのcommitへのrelease tagから自動build/releaseする。

candidateのsource、package、build inputを回帰・soak完了前に変更した場合は新candidateを作成し、影響する試験を行う。結果記録commit後はcandidateと製品codeが同一であることを確認し、共通release方針のtag/build工程へ進む。ソークは一定時間以上の連続したTS取得を指し、具体的な時間は適用頻度と版別matrixに従う。物理抜差しをユーザーに依頼する場合、各操作の直前に通知し、依頼後最大5分待つ。検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外` に統一する。

## 共通の検証項目と頻度

用語は4製品で共通とする。代表機はLinux x86_64 glibc、機種はチューナー、製品はドライバ、ソークは一定時間以上の連続したTS取得を指す。時間条件は下の適用頻度と版別matrixに従う。製品・機種ごとの能力、対応機能、実コマンドは続く製品固有節と手順節に記し、版別記録にはcandidateごとの実施結果を記す。

| 区分 | 適用頻度 | 共通の確認 |
|---|---|---|
| 機種初回 | 各製品の全対象機種で、機種ごとに代表機の2時間連続受信と毎release項目を実施する。必要時項目も通し、結果は機種別に初回記録へ残す。 | 当該機種の公開能力・対応機能に沿って実行し、別機種の結果で代替しない。 |
| 回帰（毎release） | 代表機で2時間連続受信する。全配布archiveをそれぞれ対応するnative OS/ABIで短時間実機回帰し、`--list-json`、機種の公開同時受信能力、TS/counter/終了値を確認する。最大同時受信で地上波・衛星の対象3波を確認できない場合は不足波を短時間取得する。受信中の状態と反復制御、正常停止、process/FD/IPC残留なし、対応機能のCARD挿抜、USB抜去/再挿入を確認する。native PC/SC/WinSCard経路がある製品・OSでは製品CLI経路と分けて確認する。 | 各archive/OS/ABIのrunと各機種の結果を分ける。未対応機能は理由付きでN/Aとし、未実施や失敗を合格に読み替えない。 |
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

## 共通試験シナリオ T00–T14

各IDを同じ順序・同じ6項目で実施する。$RT/$LOG/$BUS-PORT/$CHANNEL_PLANはrunごとの実値に置換し、空のcandidate専用runtime/logを使う。物理操作を依頼する各回は既定通知を行い、最大5分で応答がなければ未完了として停止する。OS/API固有の長い操作は同じT-IDから後続recipeを参照する。

### T00 候補/配布物固定

- **適用対象/頻度:** 毎releaseのcandidate作成時。
- **準備:** source SHA、CI run/attempt、archive targetとhashを固定。POSIX git rev-parse HEAD/sha256sum、Windows Get-FileHashを保存。
- **実操作/利用者への依頼:** 固定したsource commitとCI runから候補archiveを取得し、POSIXではsha256sum -c SHA256SUMS、WindowsではGet-FileHashでarchive hashを照合する。archive manifest/source refがcandidate commit/runと一致することを確認する。
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

### T08 通常停止・cleanup

- **適用対象/頻度:** 全test終了時。
- **準備:** 各owned client/daemon PIDとruntimeを記録してから起動する。
- **実操作/利用者への依頼:** client signal/EOF後wait、daemon SIGINT/console stop後wait。client/daemon終了値は別々に記録。
- **期待値/終了値:** 全owned process終了、TS close、FD/IPC/socket/runtime残留なし。timeoutはfail。
- **停止/cleanup:** 無関係processへsignalしない。test専用runtimeだけ片付け失敗log保持。
- **記録:** PID/role/exit、process/FD/IPC/socket/runtime、復旧状態。

### T09 TS・counter・終了値判定

- **適用対象/頻度:** 全capture/soak。
- **準備:** TS parser/SPEC条件とdaemon counterを確認できるようにする。
- **実操作/利用者への依頼:** TSDuck version記録、188-byte remainderとtsanalyze --normalized <file>、sync/TEI/continuity/drop確認。
- **期待値/終了値:** 全TSが製品SPEC基準を満たし通常取得exit 0。非空file/counter 0だけでpassにしない。
- **停止/cleanup:** 元TS/hash/logを残してT08 cleanup。
- **記録:** parser/version、TS hash、sync/TEI/continuity/drop/counter、exit。

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
- **実操作/利用者への依頼:** 毎release代表Linux x86_64 glibc 2h/全archive native short; 初回全modelの地上channel 10s、macOS/Windows 30m、他ABI 10m。
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

N/Aは製品機能上該当しない項目だけに使う。OS/ABI候補のunsupported、candidate欠落、未実施、失敗、環境不能は版別結果で未達とし、passに読み替えない。手順に記した期待値は実測結果ではない。

## 対象アーカイブ・環境から全試験への対応

選択したarchive/E-IDごとに、下表のT00–T14を同じrun記録へ実施する。各T見出しは準備・実操作・期待値/終了値・停止/cleanup・記録を含む手順本体であり、表は対象環境からその全項目へ直接辿る索引である。環境表に載る別OS/ABIの結果や過去runで代用しない。

| archive / E-ID / 実行環境 | T00–T14の手順リンク |
|---|---|
| `linux-x86_64` / [E01](#72-環境別) / Linux x86_64/glibc container profile | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-x86_64` / [E02](#72-環境別) / Linux x86_64/musl add-on profile | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-x86_64` / [E03](#72-環境別) / Linux x86_64/glibc native profile | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `linux-aarch64` / [E15-G](#72-環境別) / Linux aarch64/glibc | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `darwin-arm64` / [E04](#72-環境別) / macOS arm64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-aarch64` / [E05](#72-環境別) / Android Termux/aarch64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-armv7a` / [E06](#72-環境別) / Android Termux/armv7a | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `android-x86_64` / [E07](#72-環境別) / Android Termux/x86_64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |
| `windows-x64` / [E17](#72-環境別) / Windows 11 x86_64 | [T00](#t00-候補配布物固定) · [T01](#t01-実行環境アクセス権入力) · [T02](#t02-列挙selectorready確認) · [T03](#t03-最大同時受信) · [T04](#t04-不足波全無料channel) · [T05](#t05-受信中の状態確認制御) · [T06](#t06-card物理挿抜復帰) · [T07](#t07-usb待機中受信中の切断と再取得) · [T08](#t08-通常停止cleanup) · [T09](#t09-tscounter終了値判定) · [T10](#t10-native-pcscwinscard) · [T11](#t11-lnb非対応要求の拒否) · [T12](#t12-複数devicedaemonaccess) · [T13](#t13-連続受信頻度) · [T14](#t14-失敗リリース判定) |

OS/ABI固有の起動方法は以下の環境recipeと§3のarchive手順を使う。試験の期待・cleanup・記録項目はリンク先T本文に従う。機能対象外だけを理由付きN/Aとし、候補未入手・unsupported・未実施・環境不足は未達で記録する。

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
2. baseline は完全な記録を持つ直近 Stable とする。現在は v0.1.5（その後 v0.1.6–v0.1.9 の tag はあるが実機記録が v0.1.5 で止まっている）。baseline 以後の累積差分を `git diff --stat <baseline>..<candidate>` で把握する。
3. 各 archive の member（`siano-ts`、`libusb-1.0.dll`、`firmware/isdbt_rio.inp`、mdev 等）を baseline の実機検証 artifact と比較する。**byte-identical な archive はその実機証拠を直接継承する**。byte が異なる場合は §4 の class で hunk 単位に分類する。
4. 対象 claim ごとに `継承` / `今回再検証` / `未認定` / `対象外` を根拠付きで記録する。時間経過・release 回数だけを理由にした再検証はしない。
5. 前回の長時間試験の日付やrelease数は事実情報として記録できるが、エージェントがsoak有無・時間・OSを決める規則として使わない。

影響分類が複数にまたがる、依存範囲が不明、または非影響を証明できない場合は `unknown/ambiguous` として、影響し得るpath集合をユーザーへの説明に含める。選択する短時間実機確認のarchiveは影響判定に関係なくcandidateそのものを使い、適用するarchive/OS頻度は該当版の `docs/release-validation/<version>.md` に従う。

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

## 3. 配布アーカイブ別手順

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

短時間回帰とは分けて連続受信を行う。毎releaseの代表Linux x86_64 2時間と初回/majorのmacOS/Windows 30分、残るOS/ABI 10分は「共通の検証項目と頻度」で定める。ユーザーから別途soakが指示された場合は、追加分の実施有無・時間・OSをその指示に従う。

各連続受信では実行archive、OS/ABI、モデル、channel、実行時間、client/daemon終了値、TS品質、process/FD/RSSの観測値、cleanupを記録する。追加試験の選択や、頻度表の対象外へ追加する条件はユーザーの指示に従う。

### 連続受信の内容

- 共通の頻度表またはユーザーの追加指示で指定された時間、OS、topologyを用いる。
- 10秒以下の間隔で RSS と FD 数を記録し、傾向を示す。**数値の合否基準は設けない**。記録系列が最終観測まで安定化しない持続的な増加を示し、外部要因も特定できない場合、その soak は `判定保留` として pass にせず、release前に原因を調査する。途中で頭打ちになる増加は自動的な失敗ではなく、証拠と理由を記録する。この disposition は soak の受入規則であり、新しい soak trigger でも、trigger のない追加 run を要求するものでもない。
- 各 cycle の出力を §6 で検査する。終了後にprocess残存と`sianoctl status/list`を確認する。
- S1UD は `port=` で個体を選ぶ。ユーザー指定OSで必要な台数/topologyを用意できない場合は、制約を報告してユーザーに次の判断を求める。他OSへの振替やsoak中止をagentが決めない。

## 6. TS 検査

`siano-ts` は in-band の TS 整合性 counter を持たない。出力ファイルを解析 host へコピーし、両 host で SHA-256 が一致することを確認してから検査する。TS 検査に使う TSDuck の version を固定して記録し、`tsanalyze` の field 名を採用時に1回確認する。

1. `stat`（Linux）/ `stat -f`（macOS/BSD）で `size % 188 == 0`（188-byte remainder なし）。
2. `tsanalyze --normalized <file>` で invalid sync 0、TEI 0、PID ごとの continuity discontinuity 0。各 PID の先頭 packet、および `--control` の retune 境界2点は除外する。
3. `TS queue dropped` が stderr になく、exit code が 8 でないこと。正常は exit 0。

## 7. 正式アーカイブの環境別手順

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
- 追加の移植性確認（Fedora SELinux、Gentoo、NixOS、Chimera、Alpine/mdev、OpenWrt、FreeBSD、Debian i386）は[移植性確認手順](platforms/portability-checks.md)を参照する。Stable release gateのmatrix・合否対象には含めない。
- **E15-G Fedora aarch64（L4T）**: `linux-aarch64` の canonical runtime。`uname -r` と `getenforce` を記録し、S-RX・S-DUAL・S-DISC を実施する。claim は特定 kernel に限定する。既存記録 `b2c9466` は履歴。
- **E17 Windows 11 x86_64**: product対象。ZIPを展開し、WinUSB割当後にZIP rootの3 programを使う。OS build、Zadig/WinUSB version、各EXEのcandidate固有SHA（同じZIPのmanifestと照合）を記録する。旧`packaging/windows-baseline.sha256`は単体版の履歴であり、現行candidateとの一致を要求しない。USB IDとselectorを確認して、PowerShellで次の操作を行う。S-CTRLは`--control`付きconsumerのconsole stdinから渡す。[今回の統合3program記録](platforms/validation-results.md#2026-10-21-windows統合3program短時間受信)では1台の30秒受信・TS整合性・lease解放・正常停止を確認した。hotplug・soak・最終RC認定は未実施であり、以下の手順の記載自体を成功記録と扱わない。

```powershell
$D = (Resolve-Path .\sianod.exe).Path
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
$FW = (Resolve-Path .\firmware\isdbt_rio.inp).Path
$INSTANCE = 's1ud'
$LOG = 'C:\siano-e17-<run-id>-logs'
if (Test-Path $LOG) { throw "refusing to reuse existing log path: $LOG" }
New-Item -ItemType Directory -Path $LOG | Out-Null
$RT = 'C:\siano-e17-<run-id>'
if (Test-Path $RT) { throw "refusing to reuse existing runtime path: $RT" }
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
$RT = 'C:\siano-e17-<run-id>'
$LOG = 'C:\siano-e17-<run-id>-logs'
$INSTANCE = 's1ud'
$C = (Resolve-Path .\sianoctl.exe).Path
$B = (Resolve-Path .\siano-ts.exe).Path
& $C --instance $INSTANCE --runtime-dir $RT status
$p = Start-Process -FilePath $B -ArgumentList "--instance s1ud --runtime-dir `"$RT`" -c 22 -o hotplug.ts" -PassThru -NoNewWindow -RedirectStandardError hotplug.err
Start-Sleep -Seconds 20
# ここで利用者がUSBを抜く。2分以内に終了し、ExitCode 7であることを確認する
if ($p.WaitForExit(120000)) { $p.Refresh(); $p.ExitCode }
else { 'HANG: record PID and timeout; return to the owning console and stop with Ctrl+C; do not force-terminate' }
# daemon consoleでCtrl+C。利用者がUSBを戻してから同consoleで
# --list-jsonでselectorを確認し、上と同じdaemon起動を行う。
# daemonがreadyになったらconsumer consoleで:
& $C --instance s1ud --runtime-dir $RT status
& $B --instance s1ud --runtime-dir $RT -c 22 -t 30 -o reconnected.ts 2> reconnected.err
$LASTEXITCODE                 # 0を期待
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
| T03 | `& $B --instance s1ud --runtime-dir $RT -c 22 -t 30 -o "$LOG\t03.ts"`。 | receiver 0の1 stream、exit 0とTS/counterを保存する。 |
| T04 | 下の`$CHANNEL_PLAN`を現地の全無料ISDB-T channelに設定して10秒ずつ取得する。 | channelごとのexit/TS/counterを保存。初回/major以外は毎releaseの不足波を対象にする。 |
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
    $p = Start-Process -FilePath $B -ArgumentList "--instance s1ud --runtime-dir `"$RT`" -c $channel -t 10 -o `"$out`"" -PassThru -NoNewWindow -Wait
    [pscustomobject]@{ Channel=$channel; ExitCode=$p.ExitCode; File=$out }
}
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $B
$psi.Arguments = "--instance s1ud --runtime-dir `"$RT`" --control -o `"$LOG\t05.ts`""
$psi.UseShellExecute = $false; $psi.RedirectStandardInput = $true; $psi.RedirectStandardError = $true; $psi.RedirectStandardOutput = $true
$control = [System.Diagnostics.Process]::Start($psi)
$stdout = $control.StandardOutput.ReadToEndAsync(); $stderr = $control.StandardError.ReadToEndAsync()
Start-Sleep -Seconds 20; $control.StandardInput.WriteLine('channel 21')
Start-Sleep -Seconds 20; $control.StandardInput.WriteLine('channel 22')
Start-Sleep -Seconds 20; $control.StandardInput.WriteLine('quit'); $control.StandardInput.Close()
$control.WaitForExit()
$stdout.Result | Out-File "$LOG\t05.out"; $stderr.Result | Out-File "$LOG\t05.err"; "$($control.ExitCode)" | Out-File "$LOG\t05.rc"
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
& $B --instance a --runtime-dir $RT_A -c 22 -t $S -o "$LOG\a.ts"; $LASTEXITCODE
```

client console BでB側を前景実行し、A切断中もBが継続して正常終了することを確認する。

```powershell
$B = (Resolve-Path .\siano-ts.exe).Path
$LOG = 'C:\siano-e17-<run-id>-logs'
$RT_B = 'C:\siano-e17-<run-id>-b'
$S = '<finite-duration-seconds>'
& $B --instance b --runtime-dir $RT_B -c 21 -t $S -o "$LOG\b.ts"; $LASTEXITCODE
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

## 変更記録

- v3 (2026-10-25): 各release全配布archiveのnative回帰を維持し、試験結果commit・製品code不変確認後のrelease工程へ共通手順を整合。
- v2 (2026-10-07): 当時の7 archive短時間確認・candidate bytes公開方針を記録。現行release頻度と最終release工程は上記の共通方針に従う。
