# Siano 初回機種チェック

この初回手順はSianoの対象全機種に適用する。各機種について代表Linux x86_64 glibcで2時間連続受信し、その機種で毎release回帰項目と必要時項目を実施する。機種ごとに結果を分け、他機種の結果を継承しない。通常の無指定時検証機種はPX-S1UDだが、初回対象の限定ではない。以下はPX-S1UDの問題発見段階の実績で、最終candidate archiveの初回認定ではない。

## PX-S1UD 実測結果

| 項目 | 実施内容と結果 | 状態 |
|---|---|---|
| 2時間連続受信 | Linux x86_64 glibc上のlocal build-qaで2時間受信し、client/daemon exit 0。 | 実施済み、packet/error統計なし、final candidateでは未認定 |
| 地上波短時間取得 | 6 channel (T16, T17, T22, T27, T30, T31) を取得。独立解析でTS error 0。 | 実施済み、final candidateでは未認定 |
| USB切断 | 受信中の抜去後52.486秒まで自然終了しなかった。通常SIGINTでclient/daemon exit 0、daemon logに`LIBUSB_ERROR_IO`。 | 有限終了未達 |
| 再接続 | USB再接続後、新daemonで8秒T27受信が完了し、TS error 0。 | 実施済み |
| CARD / LNB / 衛星 | 製品機能外。 | 対象外 |
| OS/ABIのnative回帰 | 最終candidate archiveによる全配布OS/ABI確認なし。 | 未実施 |

今後追加される機種にも同じ共通手順を適用し、独立した実測結果を追記する。機種ごとの全適用項目が完了するまで初回チェックは未認定とする。1.0.0の問題発見結果は[`1.0.0.md`](1.0.0.md)に記録する。
