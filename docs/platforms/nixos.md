# NixOS での構成例

> **検証済み構成について:** 本文書は以下の環境で動作確認した構成例です。将来のOS更新への追従や継続的な保守は保証しません。

## 検証対象

- 検証日: 2026-09-08
- `siano-userland`: commit `898fa71e0438e36b4299d4f836e67ad5321ec8ae`, version `0.1.4`
- NixOS 26.05.9227.c25784012c99, x86_64, glibc 2.42, kernel 6.18.49
- systemd 260, udev

## 最小構成例

USB のアクセス権限、一般ユーザーでの実行、および起動時ブラックリストに関する最小限の設定例です。`siano-ts` はカーネルドライバーの自動デタッチを行いますが、今回の実機検証では競合要因を排除するため、起動時ブラックリスト（`boot.blacklistedKernelModules`）を採用しています。

```nix
{
  boot.blacklistedKernelModules = [ "smsusb" "smsdvb" "smsmdtv" ];

  users.users."<user>".extraGroups = [ "video" ];

  services.udev.extraRules = ''
    SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3275", ATTR{idProduct}=="0080", MODE="0660", GROUP="video"
    SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0600", MODE="0660", GROUP="video"
    SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="187f", ATTR{idProduct}=="0302", MODE="0660", GROUP="video"
  '';
}
```

## 実機で確認したこと

NixOS 環境にて、Linux 向け配布アーカイブを一般ユーザー権限で実行し、PX-S1UD 2 台の USB ノードが `root:video 0660` となること、両チューナーで MPEG-TS 受信が正常に完了すること、およびカーネル標準の Siano モジュールがロードされないことを確認済みです。
