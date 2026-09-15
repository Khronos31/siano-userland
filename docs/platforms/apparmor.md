# AppArmorで実行する際の注意

> [!NOTE]
> 本リポジトリにAppArmor profileは含まれない。本文書は一時的な自作profileによる実機検証の記録と運用上の注意である。

## 実行前の要点

- profileは利用者側で用意する。
- profile割り当て前の実行状態は `unconfined` となる。
- 起動後に `/proc/<pid>/attr/current` で対象profileの適用状態を確かめる。
- チューナー接続前にカーネルモジュール（`smsusb`、`smsdvb`、`smsmdtv`）をblacklistへ登録する。
- すでにモジュールがbindしている場合は、blacklistを反映したうえでOSを再起動する。

## 検証環境

- 検証日: 2026-09-14（初回検証）、2026-09-15（完全再検証）
- ソフトウェア: `siano-userland` Stable v0.1.5 Linux x86_64 配布アーカイブ（SHA-256: `b5528d9f179574b93a2982390d295c595332bf16d1bb60836de775b0c693d931`）
- 実行バイナリ: `siano-ts`（SHA-256: `53df5abd54039c41d3df0f130e59d82d5cff16a85b3b3c55f516213501c94818`）
- ファームウェア: Sianoファームウェア（SHA-256: `054520642d5d09cb7ab7d08dbd6fd9ba9365de56adf2e7d7d06927f9845ff818`）
- ハードウェア: Latitude 5300
- OS: AnduinOS（Linux 7.0.0-31-generic）
- AppArmorバージョン: 5.0.2
- 使用チューナー: PX-S1UD 2台（USB ID `3275:0080`）
- 起動方法: 名前付きprofileをロードし、`aa-exec -p` 経由で `siano-ts` を直接起動

## 検証結果

### 2026-09-15 完全再検証結果

- **総合判定**: SianoのAppArmor userland機能はpass、`smsusb` live handoffは独立したfailとして保持する。
- **Deny gate**:
  - USB node未許可のprofileで `siano-ts` 自身のopenが拒否され、有限時間内に終了してAppArmor拒否ログが記録された。
- **通常suite（complain / enforce）**:
  - 反復handoffを避けるため、`smsusb` を一度アンロードして module absent を維持した状態で試験を実施した。
  - complain（2台・30秒）、enforce（1台・60秒）、enforce（2台・60秒）、enforce（2台・30分）の全試験を完走した。
  - 2台による30分受信実測値:
    - device 0（T22）: 3,885,181,680 bytes / 20,665,860 packets
    - device 1（T21）: 3,885,396,000 bytes / 20,667,000 packets
    - 両系統とも188バイト余剰、syncエラー、TEI、キュー破棄、libusbエラー、AppArmor拒否は0件であった。
- **受信中物理切断**:
  - 初回試行（無効試行）: USB物理パスとdevice indexの仮定が逆で判定対象を取り違えたため、無効試行（invalid）として保持した。
  - 再試行（pass）: プロセスのopen FDより device 0→node 20、device 1→node 17 を実測同定し、node 20のみを物理切断した。
    - 切断側（device 0）: 1秒以内に `LIBUSB_ERROR_IO` / exit 1 で有限終了。切断まで 140,518,720 bytes / 747,440 packets、余剰・sync・TEIは0件であった。
    - 残存側（device 1）: 切断後30秒間受信を継続し、明示停止によりexit 0。209,003,360 bytes / 1,111,720 packets、余剰・sync・TEIは0件であった。
    - 切断再試行中のAppArmor拒否、page dump、プロセス/profile残留は0件、カーネルtaintは12800を維持した。
- **再接続復帰**:
  - OS再起動なしで再接続ノード21を同定し、profileを更新・再ロードした。
  - 2台によるenforce 60秒同時受信がpassした（各 129,727,520 bytes / 690,040 packets、余剰・sync・TEI・キュー破棄・libusbエラー・AppArmor拒否 0件）。
- **クリーンアップ**:
  - 最終cleanupで `smsusb` を再ロードして両interfaceへbindし、page dump 0件で開始状態へ復元した。試験profile、プロセス、エンドポイントの残留は0件であった。

### 2026-09-14 初回検証結果（履歴）

- 範囲: 2台同時受信までの基本機能（物理切断および再接続復帰は未実施）。
- 30分受信時の測定値:
  - 2台受信（T22: 20,667,240パケット、T21: 20,666,100パケット）において全エラー0件。
  - 181回測定すべてで各プロセスのRSSは5,424 KiB、ファイルディスクリプタ数は8固定。
- 詳細な測定値は [OS・環境別の検証結果](validation-results.md) を参照する。

## profileに必要だった権限

- 実行ファイル本体の実行権限。
- ファームウェアの読み取り権限。
- 対象USBデバイスノードの読み書き権限。
- USB sysfsおよびudevデータベースの読み取り権限。
- netlink通信権限。
- プロセス間シグナル送信権限。
- `ipc_lock`（任意の `mlockall` 呼び出しに対応）。
- `sys_nice`（任意の `SCHED_FIFO` 呼び出しに対応）。
- ファームウェアは実行ユーザーが通常権限で読めるディレクトリに配置する（所有権の不一致を `dac_override` や `dac_read_search` で回避する構成は避ける）。
- `ipc_lock` と `sys_nice` の付与を省いた場合でも警告表示後に受信は継続するが、AppArmor拒否ログを0件にする場合は明示的な許可を設ける。

## USBデバイスノードの扱い

- `/dev/bus/usb/BBB/DDD` のバス番号およびデバイス番号は動的に割り当てられるため、固定パスでの記述を避ける。
- profileの生成および再読み込みの直前に、sysfsの `busnum` および `devnum` からノード番号を取得する。
- 取得時はVID:PIDに加え、sysfs上の物理USBパスを照合する（シリアル番号を持たないPX-S1UD個体が存在するため）。
- 照合に一致しない場合はprofileのロードを中断する（fail-closed構成）。
- チューナーを再接続した際は、パスの再照合とprofileの再生成・再読み込みを実施する。

## smsusbとの競合

> [!WARNING]
> 稼働中のチューナーをカーネルドライバから切り替えて `siano-ts` へ渡す運用（live handoff）は安全と判定していない。

- `smsusb` のunbind、自動reprobe、binding復元の境界で以下のカーネル異常を観測した。
  - `smsusb_onresponse` での `-ESHUTDOWN`
  - `page dumped because: Not a kmalloc allocation`
  - 1回のページ割り当て失敗（page allocation failure）
- 2026-09-15完全再検証時、deny gate時の `smsusb` unbindで `page dumped because: Not a kmalloc allocation` を20件観測した。通常suite前の別の制御unbindでも20件再現し、計40件となった（kernel taintは12800のまま増加なし）。
- 反復handoffを避けるため、AppArmor userland試験中は `smsusb` を一度アンロードして module absent を維持した。
- 最終cleanupでの `smsusb` 再ロードおよび両interfaceへのbindでは、page dump 0件で開始状態へ復帰した。
- 当該事象はAppArmorによる制限下でのTS受信処理自体とは分離しているが、`smsusb` live handoffは独立したfailとして保持し、通常手順としない根拠とする。
- ディストリビューション別のblacklist反映例は [NixOSの構成例](nixos.md) を参照する。
