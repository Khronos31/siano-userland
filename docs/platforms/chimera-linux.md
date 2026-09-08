# Chimera Linux での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-09
- `siano-userland`: commit `898fa71e0438e36b4299d4f836e67ad5321ec8ae`, version `0.1.4`
- Chimera Linux, rootfs snapshot 20251220, x86_64, musl 1.2.5_git20240705, kernel 6.18.48-0-generic
- Clang/LLVM/libc++ 22.1.8, libusb 1.0.30
- PID 1 は dinit、USB device manager は udev

## USB とカーネルモジュールの設定

対応する Siano RIO 系デバイスのみパーミッションを `root:video 0660` に設定する udev ルールは次のとおりです。

```udev
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3275", ATTR{idProduct}=="0080", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0600", MODE="0660", GROUP="video"
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0302", MODE="0660", GROUP="video"
```

実行ユーザーを `video` グループに追加します。`siano-ts` は実行時にカーネルドライバーの自動デタッチを行いますが、今回の実機検証では競合要因を排除するため、起動時のブラックリスト設定を採用しました。

```text
blacklist smsusb
blacklist smsdvb
blacklist smsmdtv
```

## 実機で確認したこと

Clang/musl 環境でのネイティブビルドおよびテストの通過を確認しました。ビルドした動的リンクバイナリと配布物の静的リンクバイナリの双方について、PX-S1UD 2 台を用いて動作確認を行い、両チューナーがシグナルロックを取得して MPEG-TS 受信を完了できること（終了コード 0、キュー破棄数 0、188 バイトアライメント維持）を確認済みです。なお、一般ユーザー実行時に `mlockall` や SCHED_FIFO に関する警告が出力される場合がありますが、今回の検証では非致命であり、受信処理は正常に完了しました。
