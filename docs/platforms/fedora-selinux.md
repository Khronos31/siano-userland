# Fedora 44 / SELinux Enforcing

> [!WARNING]
> チューナー接続前にカーネルモジュール（`smsusb`、`smsdvb`、`smsmdtv`）をblacklistへ登録する。稼働中のチューナーをカーネルドライバから `siano-ts` へ切り替える運用（live handoff）は安全と判定していない。すでにbindしている場合は、設定反映後にOSを再起動する。

## 検証結果の要約

2026-09-09に記録された実機検証の要約は以下のとおりである。

- 対象ソフトウェア: `siano-userland` commit `0315bd5609422748293bdde63e760c9e02a9cef8`（version `0.1.4`）
- OS環境: Fedora 44（x86_64 / glibc、SELinux Enforcing）
- 使用チューナー: PX-S1UD 1台（USB ID `3275:0080`）
- 実行権限: `video` グループ所属の一般ユーザー（udevでデバイスノードを `root:video 0660` に設定）
- 受信実績: 地上波27ch、30秒間受信（64,972,800バイト / 345,600パケット）
- 検査結果: 188バイト余剰 0、syncエラー 0、要約記録上のAVC拒否 0件

## 適用範囲と境界

- 実機結果の対象: version `0.1.4`（上記commit）
- 現行Stable v0.1.5: Fedora再検証対象
- Siano専用SELinux policy: 導入実績なし（一般ユーザーの標準コンテキストで実行）
- 実行形態: 端末からの一般ユーザー直接実行（systemdサービス化や専用confined domainは再検証対象）
- 「AVC拒否0件」の範囲は保存された要約データに基づく。

## 保存済み証跡と次回採取する証跡

### 保存済み証跡

- 受信パケット数（345,600パケット）およびバイト数（64,972,800バイト）
- TS整合性検査結果（余剰0、syncエラー0）
- AVC拒否0件の記録要約

### 次回採取する証跡

- 実行プロセスのSELinuxコンテキスト
- USBデバイスノードのセキュリティラベル
- raw AVC監査ログ（`ausearch -m AVC` の完全ログ）
- 実行時の標準出力および標準エラー出力の完全ログ
- 使用したバイナリおよびファームウェアのSHA-256ハッシュ
- コマンド終了コード

## 設定例

### udevルール

対象機器のUSBデバイスノードに対するアクセス権を設定する。他の対応IDを含むルール例はトップレベルREADMEを参照する。

```udev
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3275", ATTR{idProduct}=="0080", MODE="0660", GROUP="video"
```

実行ユーザーを `video` グループへ追加し、ルールを再読み込みする。

```sh
sudo usermod -aG video "$USER"
sudo udevadm control --reload-rules
```

### カーネルモジュールの除外

標準ドライバの自動バインドを停止するため、blacklist設定を作成する。

```text
blacklist smsusb
blacklist smsdvb
blacklist smsmdtv
```

OSの設定に合わせてinitramfsを更新したうえで再起動し、`lsusb -t` などで `smsusb` が割り当てられていない状態を確認する。

## 再検証の手順例

現行バージョンや独自ポリシー下で動作検証を行う際は、以下の手順例を参考に証跡を取得する。

### 実行前の状態記録

```sh
getenforce
id
id -Z
ls -lZ /dev/bus/usb/<bus>/<device>
sha256sum ./siano-ts ./firmware/isdbt_rio.inp
```

### 受信実行とAVC監査ログの取得

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
