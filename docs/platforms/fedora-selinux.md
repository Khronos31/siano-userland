# Fedora 44 / SELinux Enforcing

> [!IMPORTANT]
> 本文書は、保存されている2026-09-09の検証要約と、現在のLinux共通設定を区別して記載します。
> 検証ではSiano専用SELinux policy moduleを導入しておらず、専用domainによる拘束や
> systemd serviceとしての実行を確認したものではありません。

> [!WARNING]
> Linux標準の`smsusb`へ一度bindされたチューナーを、稼働中に`siano-ts`へ切り替える運用は
> 安全と判定していません。チューナーを接続する前またはboot時から、`smsusb`、`smsdvb`、
> `smsmdtv`が対象機器を所有しない構成にしてください。すでにbind済みの場合は、live handoffに
> 頼らずblacklistを反映して再起動してください。この注意はSELinuxとは別のUSB device所有権の問題です。

## 保存されている検証要約

- 検証記録日: 2026-09-09
- `siano-userland`: commit `0315bd5609422748293bdde63e760c9e02a9cef8`, version `0.1.4`
- OS: Fedora 44, x86_64 / glibc, SELinux Enforcing
- チューナー: PX-S1UD（USB ID `3275:0080`）
- 権限: udevでUSB device nodeを`root:video 0660`とし、`video` groupの一般ユーザーで実行
- 受信: 地上波27ch、30秒、64,972,800 bytes / 345,600 packets
- 検査結果: 188-byte remainder 0、sync error 0、検証要約上のAVC拒否0件

この結果はversion 0.1.4の上記commitへ固定されます。現在のStable v0.1.5配布アーカイブを
Fedoraで再試験した記録ではありません。

## この記録から証明できないこと

当時のSiano試験について、次の生証跡は保存されていません。

- 実行したSSH commandとstdout / stderr
- 実行中processのSELinux context
- USB device nodeのSELinux label
- `ausearch`のSiano専用raw log

したがって、「AVC拒否0件」は保存された検証要約の範囲に限定されます。専用domainでの拘束、
任意のSELinux user domain、systemd service、将来のFedora policyで追加allow ruleが不要であることを
示す結果ではありません。

## USB権限とkernel module

保存された検証要約にはSiano専用SELinux allow ruleの導入記録はなく、記録に残る権限設定は
通常のUSB device node用udev ruleです。検証対象だったPX-S1UDのIDだけを示します。
他の対応IDを含む共通例はトップレベルREADMEを参照してください。

```udev
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3275", ATTR{idProduct}=="0080", MODE="0660", GROUP="video"
```

実行ユーザーを`video` groupへ追加し、udev ruleを再読込します。

```sh
sudo usermod -aG video "$USER"
sudo udevadm control --reload-rules
```

stock driverを除外する設定例は次のとおりです。

```text
blacklist smsusb
blacklist smsdvb
blacklist smsmdtv
```

blacklistの配置場所とinitramfsへの反映方法はFedoraの構成に合わせてください。group変更とblacklistを
反映した再起動後、`lsusb -t`などで対象interfaceが`smsusb`へbindされていないことを確認してから
`siano-ts`を実行します。

## 再検証する場合の確認例

以下は現在のCLIに基づく再検証例であり、2026-09-09に保存された元のcommandの再掲ではありません。

実行前にSELinuxと実行ユーザー、対象USB nodeの状態を記録します。

```sh
getenforce
id
id -Z
ls -lZ /dev/bus/usb/<bus>/<device>
sha256sum ./siano-ts ./firmware/isdbt_rio.inp
```

監査開始時刻を記録してから一般ユーザーで受信し、実行中processのcontextも保存します。

```sh
audit_start=$(date '+%H:%M:%S')
./siano-ts --firmware ./firmware/isdbt_rio.inp -c 27 -t 30 -o /tmp/output.ts &
pid=$!
ps -o pid=,label=,comm= -p "$pid"
wait "$pid"
status=$?
printf 'exit=%s\n' "$status"
sudo ausearch -m AVC -ts "$audit_start"
```

再検証の証跡には、少なくともprocess context、USB node label、使用したbinaryとfirmwareのSHA-256、
exit code、出力byte数、188-byte alignment / sync検査、試験時間帯のAVC記録を残してください。

自作のconfined domainやsystemd serviceで実行する場合は、この検証要約を流用せず、そのdomainから
firmware、USB node、必要なsysfsとnetlinkへのアクセスを個別に設計・検証してください。
