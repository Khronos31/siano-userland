# Debian 12 i386（32-bit / glibc / EHCI）での検証結果

## 検証環境

- 検証対象: `siano-userland` Stable v0.1.5 source archive
- ネイティブビルドした `siano-ts` バイナリ: ELF 32-bit Intel 80386（SHA-256: `b9bedcf8c44146299dfd10100517eb422360ffc2aad3337c896231275ea404bd`）
- ホストハードウェア: EPSON Endeavor NJ1000
- OS: Debian 12 i386（32-bit / glibc、EHCIコントローラ）
- 使用チューナー: PX-S1UD 2台

## 検証結果

- ソースコードからのネイティブビルドおよび `make test` が成功した。
- PX-S1UD 2台によるT22/T21同時の60秒gate試験が合格した。
- 出力先を `/dev/null`（nonblocking sink）とした30分連続受信（soak試験）が合格した。
- soak試験において両系統ともlock取得、exit code 0、キュー破棄0、プロセス残留なしを確認した。
- 30分間の測定において、RSSは約24 MiB、ファイルディスクリプタ数は8で推移した。
- 試験中のCPUおよび周辺温度は最高53℃であった。
- カーネル側のUSBエラー、OOM、熱異常の記録は0件であった。

## 実ファイル出力における検出事象

- 2 GiB超の実ファイル書き出し試験において、キュー破棄（queue drop）の発生を検出した。
- 当該事象はドライバのUSB受信自体の失敗ではなく、低速なストレージ出力（slow sink）に伴うバッファ飽和に起因する。
- 低速出力時の破棄通知および終了コードの扱いについては、仕様改善課題として [Issue #4](https://github.com/Khronos31/siano-userland/issues/4) で追跡している。

## 適用範囲と境界

- 合格範囲: 32-bit環境でのネイティブビルド、および十分な処理速度を持つ出力先（nonblocking sink）での連続受信。
- 仕様改善課題: 低速シンクに対する破棄検出時の通知および終了ハンドリング。
