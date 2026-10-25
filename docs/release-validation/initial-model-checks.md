# Siano 初回機種チェック

標準機種はPX-S1UD。条件は代表Linux x86_64 glibcで2時間連続受信し、同機種で毎release回帰項目と必要時項目を実施すること。以下は問題発見段階の実績で、最終candidate archiveの初回認定ではない。

| 項目 | 実施内容と結果 | 状態 |
|---|---|---|
| 2時間連続受信 | Linux x86_64 glibc上のlocal build-qaで2時間受信し、client/daemon exit 0。 | 実施済み、packet/error統計なし、final candidateでは未認定 |
| 地上波短時間取得 | 6 channel (T16, T17, T22, T27, T30, T31) を取得。独立解析でTS error 0。 | 実施済み、final candidateでは未認定 |
| USB切断 | 受信中の抜去後52.486秒まで自然終了しなかった。通常SIGINTでclient/daemon exit 0、daemon logに`LIBUSB_ERROR_IO`。 | 有限終了未達 |
| 再接続 | USB再接続後、新daemonで8秒T27受信が完了し、TS error 0。 | 実施済み |
| CARD / LNB / 衛星 | 製品機能外。 | 対象外 |
| OS/ABIのnative回帰 | 最終candidate archiveによる全配布OS/ABI確認なし。 | 未実施 |

初回チェックは全適用項目が完了するまで未認定とする。1.0.0の問題発見結果は[`1.0.0.md`](1.0.0.md)に記録する。
