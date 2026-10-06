# AGENTS.md

## Source of truth

- Stable リリース前検証の規範は [`docs/release-validation.md`](docs/release-validation.md)。記載が実装・`README.md` と食い違う場合は、実装とREADMEを確認したうえで先にこの文書を直す。
- 対応機種・USB ID・CLI・終了コードの正本は `README.md` と実装とする。

## Supported scope

- Runtime は Linux（glibc / musl、x86_64 / aarch64）、macOS arm64、Android Termux（aarch64 / armv7a / x86_64）、Windows x64（WinUSB）。
- Android ad-hoc APK の実機検証は dtv-android 所管であり、本リポジトリの release artifact および release gate に含めない。
- FreeBSD は対象外であり、`docs/platforms/validation-results.md` の記録は履歴に限る。

## Implementation rules

- 既存テストを保持する。期待値・fixture・mock・skip を変えて失敗を隠さない。
- カーネルドライバからの live handoff を行わない。既定では bind 済みのデバイスを奪わない。
- 物理USB・アンテナ・カード・電源の操作、および Windows の WinUSB 割当はユーザー確認のうえ実施する。
- production の PX-S1UD、mirakc、EPGStation の経路を承認なしに変更しない。HA Core・アドオン・サービスを自動で再起動しない。
- `git add` / `git commit` / `git push` はユーザーが明示したときだけ行う。

## Stable release validation

- [`docs/release-validation.md`](docs/release-validation.md) の順に実施する。配布する各主要OS/architecture binary artifactについて、final candidateの実機確認を毎回行う。短時間確認の一連の操作に総時間上限を設けない。5分はユーザーの物理操作（USB抜去・再挿入）の応答待ち上限であり、各操作を要求するときはHAOS側でCodexは`beep`、Claude Codeは`vibe`を実行する。5分を超える連続負荷試験はsoakとして分ける。安定性に影響し得る変更のsoak有無・時間（10分/30分/2時間）・対象OSはユーザーが決める。エージェントは選ばず、判断材料を示して決定を待つ。
- 検証状態の語彙は `継承` / `今回再検証` / `未認定` / `対象外`。記録は [`docs/platforms/validation-results.md`](docs/platforms/validation-results.md) へ追記し、新しい records directory や template framework、汎用検証スクリプトを作らない。
- README の claim は記録された証拠を超えない。未実施の項目を実施済みと表現しない。

## Release notes

- GitHub Release のタイトルは `vX.Y.Z` のみとし、先頭に製品名などを付けない。
- 本文の先頭見出しは `siano-ts vX.Y.Z` とし、概要は敬体で簡潔に書く。「主な変更」「検証」「既知の制限」は常体の箇条書きとし、該当項目がない節は省略する。
- 内部監査記録、詳細な試験ログ、ハッシュ一覧、余計な検証 matrix は載せない。環境別の詳細は README 等の正本へリンクする。

## Handoff requirements

- 各 increment の最後に、変更したファイル、実行したコマンド、結果、未確認の範囲を報告する。
