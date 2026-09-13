# AppArmorで実行する際の注意

> **検証済み構成について:** 本文書は、一時的な自作profileを用いた実機検証の記録と、
> その際に判明した運用上の注意です。本リポジトリはAppArmor profileを同梱しておらず、
> 任意のprofileや将来のOS更新における動作を保証するものではありません。

## 検証対象

- 検証日: 2026-09-14
- `siano-userland`: Stable v0.1.5 Linux x86_64配布アーカイブ
- ホスト: Latitude 5300 / AnduinOS / Linux 7.0.0-31-generic / AppArmor 5.0.2
- チューナー: PX-S1UD 2台（USB ID `3275:0080`）
- 起動方法: 名前付きprofileをloadし、`aa-exec -p`から`siano-ts`を直接起動

## 実機で確認したこと

- USB device nodeを許可しないenforce profileでは、profile内の`siano-ts`自身によるnative openが
  `LIBUSB_ERROR_ACCESS`で有限時間内に失敗し、対象nodeへのAppArmor denialが記録された。
- allow profileでは、complain 2台30秒、enforce 1台60秒、enforce 2台60秒、
  enforce 2台30分の全caseが完走した。
- 2台30分ではT22が20,667,240 packets、T21が20,666,100 packetsで、両方とも
  remainder / sync / TEI / queue drop / libusb errorは0だった。
- 30分間の181測定で、各processのRSSは5,424 KiB、FDは8で固定だった。
- 合格case内のAppArmor denialとkernel異常は0で、終了後にprofileとprocessの残留がないことを確認した。

詳細な結果は[OS・環境別の検証結果](validation-results.md)を参照してください。

## Profile設計の要点

- AppArmorが有効なLinuxでも、profileへ入っていないprocessは`unconfined`のままです。
  `aa-exec -p`またはservice manager側の設定で、実際の`siano-ts`が意図したprofileへ入ったことを
  `/proc/<pid>/attr/current`などで確認してください。
- 実測したprofileでは、実行ファイル、firmwareの読み取り、対象USB device nodeの読み書き、
  USB sysfsとudev databaseの読み取り、netlink、signal、`ipc_lock`、`sys_nice`を許可しました。
- `ipc_lock`と`sys_nice`は、それぞれ任意の`mlockall`と`SCHED_FIFO`試行に対応します。
  許可がなくても`siano-ts`は警告を出して通常動作を継続しますが、拒否ログを0件にするprofileでは
  許可するか、この非致命な拒否を運用上明示して扱う必要があります。
- firmwareは専用service accountから通常のファイル権限で読める場所へ置いてください。
  所有権の不一致を`dac_override`や`dac_read_search`で迂回する構成は、この検証結果からは推奨しません。

## USB device nodeは固定パスではない

`/dev/bus/usb/BBB/DDD`のbus番号とdevice番号は、抜き差しや再列挙で変わります。
検証時に使った番号を恒久profileへコピーしないでください。

USB nodeを個別に許可する場合は、profileの生成・reload直前にsysfsの`busnum`と`devnum`からnodeを求め、
少なくともVID:PIDと物理USB pathを照合してください。PX-S1UDにはserialを持たない個体があるため、
複数台を区別する場合は物理USB pathも必要です。照合できない場合はprofileをloadせず停止する
fail-closedな構成にしてください。再接続後は同じ確認とprofileの再生成・reloadが必要です。

## `smsusb`との競合

> [!WARNING]
> ロード済みのLinux標準`smsusb`へ一度bindされたPX-S1UDを、稼働中にuserlandへ切り替える運用は
> 安全と判定していません。

`siano-ts`はlibusbのkernel driver自動detachを有効にします。一方、今回の検証では`smsusb`の
unbind、自動reprobe、binding復元の境界で、`smsusb_onresponse`の`-ESHUTDOWN`、
`page dumped because: Not a kmalloc allocation`、および1回のpage allocation failureを観測しました。
AppArmor拘束下の受信case自体では発生しておらず、AppArmor denialやTS受信失敗とは分離していますが、
live handoffを通常手順として推奨できる結果ではありません。

継続運用では、PX-S1UDを接続する前またはboot時から`smsusb`がbindしない構成を採用してください。
blacklistを反映する場所やinitramfsの再生成要否はdistributionごとに異なるため、利用中のOSの手順に
従ってください。NixOSで実機確認した例は[NixOSでの構成例](nixos.md)にあります。
