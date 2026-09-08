# Alpine Linux / BusyBox mdev での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-08
- `siano-userland`: commit `898fa71e0438e36b4299d4f836e67ad5321ec8ae`, version `0.1.4`
- Alpine Linux 3.24.1, x86_64, musl 1.2.6, kernel 6.18.49-0-lts
- BusyBox mdev 1.37.0, OpenRC

Alpine ではホットプラグを mdev で処理し、起動時のコールドプラグ（既存デバイス検出）は `mdev -s` の後に sysfs スキャンヘルパーを実行して処理します。`mdev -s` のコマンド一致処理には `DEVTYPE` と `PRODUCT` が渡されないため、起動時のスキャンを省略しないでください。

## USB ノードの権限

配布物に含まれる mdev 設定行を、汎用の USB / `$MODALIAS` ルールより前に `/etc/mdev.conf` へ追加します。

```text
DEVTYPE=usb_device;PRODUCT=3275/80/.*;bus/usb/[0-9]+/[0-9]+ root:video 0660
DEVTYPE=usb_device;PRODUCT=187f/600/.*;bus/usb/[0-9]+/[0-9]+ root:video 0660
DEVTYPE=usb_device;PRODUCT=187f/302/.*;bus/usb/[0-9]+/[0-9]+ root:video 0660
```

ヘルパーと OpenRC の local フックを配布物から配置します。

```sh
addgroup '<user>' video
install -d -m 0755 /usr/local/libexec /usr/local/share /etc/local.d
install -m 0755 mdev/siano-ts-mdev.sh /usr/local/libexec/siano-ts-mdev
install -m 0755 mdev/siano-ts-mdev.start /etc/local.d/siano-ts-mdev.start
install -m 0644 mdev/siano-ts-mdev.conf /usr/local/share/siano-ts-mdev.conf
```

`/etc/mdev.conf` の先頭に上記設定行を追加し、`local` サービスを有効化します。local フックにより、起動時に `mdev -s` の実行後スキャンヘルパーが呼び出されます。接続済みのデバイスへ即座に反映する場合は以下を実行します。

```sh
rc-update add local default
mdev -s
/usr/local/libexec/siano-ts-mdev --scan
```

対象ノードのパーミッションが `root:video` かつ `0660` であることを確認後、`siano-ts` を `<user>`（一般ユーザー）で実行します。デバイス再接続時は mdev のホットプラグ設定によって自動で適用されます。

## カーネルモジュールの除外

`siano-ts` は実行時にカーネルドライバーの自動デタッチを行いますが、今回の実機検証では競合要因を排除するため、起動時のブラックリスト設定を採用しました。最小例は次のとおりです。

```text
blacklist smsusb
blacklist smsdvb
blacklist smsmdtv
```

ブラックリストを initramfs へ反映する方法や適用タイミングは、使用している Alpine の環境構成に依存します。

## 実機で確認したこと

Alpine Linux x86_64 実機環境にて、Siano RIO 系 USB チューナー 2 台を接続し、配布アーカイブの実行ファイルを一般ユーザー権限で実行しました。コールドプラグ用ヘルパーが対象となる 2 つのデバイスノードのみを `root:video 0660` に設定し、両プロセスとも ISDB-T のシグナルロックを取得して 30 秒間の MPEG-TS 受信を完了できることを確認しました（終了コード 0、キュー破棄数 0、188 バイトアライメント維持、プロセス残存なし）。
