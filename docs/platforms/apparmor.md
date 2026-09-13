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

- 検証日: 2026-09-14
- ソフトウェア: `siano-userland` Stable v0.1.5 Linux x86_64 配布アーカイブ（SHA-256: `b5528d9f179574b93a2982390d295c595332bf16d1bb60836de775b0c693d931`）
- 実行バイナリ: `siano-ts`（SHA-256: `53df5abd54039c41d3df0f130e59d82d5cff16a85b3b3c55f516213501c94818`）
- ハードウェア: Latitude 5300
- OS: AnduinOS（Linux 7.0.0-31-generic）
- AppArmorバージョン: 5.0.2
- 使用チューナー: PX-S1UD 2台（USB ID `3275:0080`）
- 起動方法: 名前付きprofileをロードし、`aa-exec -p` 経由で `siano-ts` を直接起動

## 検証結果

- USB nodeの許可を省いたdeny gateでは、`siano-ts` 自身によるopenが `LIBUSB_ERROR_ACCESS` で有限時間内に失敗し、拒否ログ（AppArmor denial）が記録された。
- 必要な権限を付与したprofileでは、complain（2台・30秒）、enforce（1台・60秒）、enforce（2台・60秒）、enforce（2台・30分）の全試験を完走した。
- 2台による30分受信（T22: 20,667,240パケット、T21: 20,666,100パケット）において、188バイト余剰、syncエラー、TEI、キュー破棄、libusbエラーはすべて0件だった。
- 30分間の181回測定すべてで、各プロセスのRSSは5,424 KiB、FDは8に固定された。
- 合格試験中のAppArmor拒否およびカーネル異常は0件だった。
- プロセス終了後にprofileおよびプロセスの残留がないことを確認した。
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
- 当該事象はAppArmorによる制限下でのTS受信処理自体とは分離しているが、live handoffを通常手順としない根拠となる。
- ディストリビューション別のblacklist反映例は [NixOSの構成例](nixos.md) を参照する。
