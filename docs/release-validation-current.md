# 現行 Stable 候補のリリース前回帰手順

現行候補をarchive単位で再現確認する手順です。旧候補の実測・soak条件は現在のpassではありません。CLI/終了コードは [`README.md`](../README.md)、配布gate・結果記録の既存手順は [`release-validation.md`](release-validation.md) を参照します。

## 1. 候補・device-free gate

候補commitと `release.yml` runを固定し、7 binary archive（Linux x86_64/aarch64、macOS arm64、Android aarch64/armv7a/x86_64、Windows x64）とsource archive、`SHA256SUMS` を取得する。run/ref/SHA、artifact ID、各hash、source manifest/license/provenanceを保存する。現行workflowは `workflow_dispatch` のみでmain refを要求し、release-candidate artifactを保存する。今回このworkflow自体を実行しない。現workflowにはtag pushによる自動build/release publish triggerがない。候補検証手順として実態を明示し、最終公開動線の決定が必要な点をIssueへ返す。

```sh
mkdir candidate && cd candidate
tar -xf <candidate-artifact.tar.gz>
sha256sum -c SHA256SUMS
```

Windows ZIPはPowerShellの `Expand-Archive` と `Get-FileHash -Algorithm SHA256` で照合する。全CI build/test/package/binary audit/reproducibility jobの成功、source archiveから追加取得なしのbuild、配布内容とlicenseの整合を確認する。候補に欠落/hash mismatch/不一致があれば止める。

各archiveで `sianod --version`、`sianod --list-json`、`sianoctl --instance rc status` を確認する。USBなし `--list-json` はscan結果を報告しUSBを所有しない。実際の機器があるnative hostでready inventoryを照合する。

## 2. 各archiveの受信回帰

soak時間はLinux x86_64代表機2時間、macOS arm64 30分、Windows x86_64 30分、他の全配布binary各10分。2hは各対応機種が一度以上および各releaseの代表Linux x86_64機で実施する。releaseごと全配布archiveはnative OS/ABIで短時間回帰する。major versionでは全OS/archで短時間TSも行う。7 binaryを他archiveの結果で代替しない。

候補archiveをnative hostへ展開する。USB位置とfirmwareを現地の実値へ置き換え、daemonを起動する。

```sh
RT=$(mktemp -d "${TMPDIR:-/tmp}/siano-release.XXXXXX")
sianod --device usb:<BUS-PORT> --instance rc --firmware <FIRMWARE> --runtime-dir "$RT" & P=$!
sianoctl --instance rc --runtime-dir "$RT" status
sianoctl --instance rc --runtime-dir "$RT" list
siano-ts --instance rc -c T27 -t 10 -o t27.ts
```

現行parserが受け付ける receiverは0のみ。statusがreadyで、T27 capture終了値0、TSが188-byte packet単位で同期し、packet/sync/TEI/continuity/drop/USB統計が期待範囲であることを確認する。各実地域の全地上波channelと無料衛星全対象channelを、それぞれ10秒ずつ取得する。channel label (`T27`、BS/CS label)、TSID/slot、RF値は現地計画・対応機種で確認した値を試験記録に固定し、コマンドへ実値を指定する。無料CS raw TSを省略しない。Sianoの公開機能はISDB-Tの受信機0であり、衛星/CARD/LNB機能はないため、該当しない項目は対象外と明記する。

各受信を正常終了後に同じdaemonで再取得して再利用可能であることを確認する。`siano-ts`の `-t` は秒、`-o` は出力先。受信中のUSB抜去はfatal transfer failureの場合exit 7（queue drop優先時はexit 8）を期待し、TS・stderr・counter・終了値を保存する。TS品質はREADMEおよびcandidateで提供されるanalyzer/CI proofに従い、ファイル長だけで合格にしない。

## 3. USB切断・再接続

idle daemonとcapture中それぞれで、ユーザーが指定portのUSBを抜く。status/clientの実際の切断表示と終了値を記録する。旧daemonの自動hotplug復旧を要求しない。clientを停止し、daemonの通常cleanup後、同じportを再列挙して新しい `sianod` を起動し、status/listとT27再取得で確認する。Termuxは `termux-usb -l`、`termux-usb -r <usb-path>` で許可し、抜き差し後にFD/USB許可を再取得する。WindowsはWinUSB driverを前提としてZIP内の `sianod.exe`、`sianoctl.exe`、`siano-ts.exe` とfirmware構成を保持する。USBがBUSYなら強制detachせず理由を記録する。

SianoにはCARD、PC/SC/WinSCardおよびLNB給電APIがありません。これらを試験対象にせず、他製品の手順を持ち込まない。

## 4. soak・cleanup・release判定

archive別のnative環境で各時間を連続受信する。capture開始・定期・終了時のTS統計/counter、process/FD/RSS、OS/arch、機種、channel、終了値を記録する。地域全ch/無料衛星全対象の各10秒試験とは別の工程。時間未達、exit 7/8、TS不整合、切断は成功に書き換えず失敗/未完として残す。

client/daemonを正常停止し、status、process残留、runtime socket cleanupを確認する。candidate ref/SHA、workflow run/attempt、archive hash、host/OS/arch、USB port、firmware、コマンド、人的操作、期待/実測、ログ位置、未確認/対象外をrelease recordへ残す。全gate後に結果文書だけをcommitし、source/build inputが候補と同一であることを確認する。現workflowには最終tag/公開手順がなく、自動tag-build releaseが必要という共通方針との不一致は親の判断事項とする。今回workflow/公開動作は実行しない。
