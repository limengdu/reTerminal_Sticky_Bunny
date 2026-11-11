# reTerminal Sticky Bunny

<p align="center">
  <strong>reTerminal Sticky のための、育つバーチャルペットとインタラクティブな ePaper アプリ集。</strong>
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README_CN.md">简体中文</a> ·
  <a href="https://www.seeedstudio.com/sticky/">Sticky 公式サイト</a> ·
  <a href="https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html">製品ページ</a> ·
  <a href="#クイックスタート">クイックスタート</a> ·
  <a href="#完全ビジュアルツアー">ビジュアルツアー</a> ·
  <a href="CHANGELOG.md">変更履歴</a>
</p>

<p align="center">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/MCU-ESP32--S3-000000?style=flat-square">
  <img alt="ESP-IDF 5.4.1" src="https://img.shields.io/badge/ESP--IDF-5.4.1-000000?style=flat-square">
  <img alt="PlatformIO" src="https://img.shields.io/badge/build-PlatformIO-000000?style=flat-square">
  <img alt="Firmware 0.1.3" src="https://img.shields.io/badge/firmware-0.1.3-000000?style=flat-square">
  <img alt="MIT License" src="https://img.shields.io/badge/license-MIT-000000?style=flat-square">
  <a href="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml"><img alt="Build and test" src="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml/badge.svg"></a>
</p>

<p align="center">
  <img src="docs/images/desktop-pet/home.png" alt="Sticky Bunny バーチャルペットのホーム画面" width="245">
  &nbsp;&nbsp;
  <img src="docs/images/launcher/launcher-portrait.png" alt="Sticky Bunny 縦向きアプリランチャー" width="245">
</p>

reTerminal Sticky Bunny は、Seeed Studio の **reTerminal Sticky** を小さく永続的な世界に変えます。世話を覚えているウサギを育て、ポモドーロセッションを開始し、いまの状況を周囲に示し、答えの書に問いかけましょう。タッチ、ボタン、スワイプジェスチャー、デバイスの回転、連続した振り、RTC によるスケジューリング、低消費電力な ePaper の挙動が、ひとつの一貫したファームウェア体験として設計されています。

デバイスの詳細は [Sticky 公式サイト](https://www.seeedstudio.com/sticky/) と [reTerminal Sticky 製品ページ](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html) をご覧ください。

このリポジトリには、PlatformIO/ESP-IDF ファームウェア一式、オリジナルのモノクロアートワーク、ホスト側の挙動・描画テスト、リリースパッケージング、開発者向けドキュメントが含まれています。

## 新旧ディスプレイの互換性

ファームウェア **0.1.3** は、Sticky の **SSD1677 と SSD2677** の両方を単一イメージでサポートします。起動時にレジスター `0x70` を読み取り、ID が `0x07` なら SSD2677、それ以外の正常に読み取れた値なら従来の SSD1677 を選択します。SPI 読み取りエラー時は初期化を中止します。起動ログで ID と選択されたコントローラーを確認できます。

モノクロ全画面更新、部分更新、4 階調表示に対応し、BUSY 極性、波形、前後フレームのパッキング、スリープ処理を自動で切り替えます。更新後はスタンバイに入り、待機中の高電圧電源を停止します。モデルの手動選択や別々のファームウェアは不要です。

実機での確認手順と検証範囲は[互換性テスト計画](docs/screen_compatibility.md)を参照してください。


## このプロジェクトが特別な理由

- **静的なマスコットではなく、連続性を持つペット。** 卵から孵り、5 つのライフステージを経て成長し、3 つの性格のいずれかを獲得し、世話を覚え、眠り、話し、遊び、ときには外出します。
- **磨き上げられた 4 つのオンデバイス体験。** デスクトップペット、ポモドーロタイマー、ステータスボード、答えの書が、ひとつのランチャーとひとつのビジュアル言語を共有します。
- **デバイスそのものがコントローラー。** ボタンまたはスワイプでランチャーを開き、タッチで選択し、縦向き・横向きのアプリへ回転させ、振って答えの書に入ります。
- **ePaper のための設計。** 静的な画面は全体更新または高品質更新を使い、時間に敏感なビューは範囲を限定した部分更新を使います。更新中も入力は応答し続けます。
- **実機の挙動に基づく実装。** ファームウェアは、ディスプレイと SD で共有する SPI バス、検証済みの Sticky `480 x 800` センサーマッピングとリファレンスのリセット復帰処理を備えた GT911 タッチコントローラー、LSM6DS3TR-C IMU、PCF8563 RTC、BQ27220 フューエルゲージ、ブザー、サイドボタン、バッテリー動作、ディープスリープを管理します。

## アプリケーションギャラリー

| バーチャルペット | ポモドーロタイマー |
| --- | --- |
| <img src="docs/images/desktop-pet/home.png" alt="成長・愛情・満腹度・元気を表示するウサギのホーム画面" width="330"> | <img src="docs/images/pomodoro/setup.png" alt="Sticky ポモドーロタイマーの設定画面" width="330"> |
| ウサギを孵し、名前を付け、餌をやり、なでて、話しかけ、一緒に遊びます。ステージ・性格・セリフは時間とともに変化します。 | 15 分・25 分・60 分から選ぶか、任意の時間を入力し、一時停止や終了ができます。時間になると穏やかなアラームが繰り返し鳴ります。 |

| ステータスボード | 答えの書 |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="Sticky ステータスボードのメニュー" width="390"> | <img src="docs/images/book-of-answers/home.png" alt="答えの書のホーム画面" width="245"> |
| `BUSY`、`MEETING`、`ON CALL`、`OPEN TO TALK`、`REST`、またはデバイス上で入力したカスタムメッセージを横向きで表示します。 | 質問を思い浮かべたまま 3 秒間振ると、メッセージ、`YES`、`NO`、`UNCLEAR` のいずれかが示されます。 |

## バーチャルペット

ペットはホーム画面であり、このファームウェアの感情的な中心です。その状態は実際の RTC 時刻によって進み、チェックサム付きの 2 つの NVS スロットに保存されるため、セーブが壊れても直前の有効なレコードにフォールバックできます。

<p align="center">
  <img src="docs/images/desktop-pet/growth-lineage.png" alt="卵から大人の各性格に至るウサギの成長系統" width="640">
</p>

### 生涯と性格

1. **卵** — 意図的に 3 回タップすると孵化します。
2. **ハッチリング** — 世話によって最初の絆が生まれます。
3. **子ども** — 食事・愛情・遊びが性格の根拠を形づくり始めます。
4. **青年** — ウサギは `FOODIE`、`AFFECTIONATE`、`ACTIVE` のいずれかになります。
5. **大人** — 各分岐が固有のプロポーション、行動、セリフ、記念品を得ます。

成長、愛情、満腹度、元気、日々の世話の連続記録、機嫌、直近の行動、直近のセリフ、性格の根拠、予定された外出は、再起動をまたいで保持されます。バランス調整のモデルと、監査済みの上流の着想元は [ペット成長デザイン](docs/desktop_pet_growth_system.md) に記載しています。

| 値 | 意味 |
| --- | --- |
| `GROWTH` | 有効な日々の世話によって得られる長期的な進行度 |
| `LOVE` | セリフや反応を変化させる絆のレベル |
| `FULLNESS` | 餌やりで回復する空腹の状態 |
| `ENERGY` | 睡眠 1 分あたり 6% 回復する活動量 |

世話のペースは意図的に抑えられています。1 日に得られるのは成長が最大 10、愛情が最大 8 までで、連続世話日数が 3・7・30・100 日に達したときにそれぞれ一度だけ記念のお祝いが発生します。ウサギは日によって家を離れることがあり、予定された 1〜7 時間のあいだ外出し、外出ページから呼び戻すこともできます。

## ランチャーと物理的な操作

<p align="center">
  <img src="docs/images/launcher/launcher-landscape.png" alt="横向きランチャー" width="700">
</p>

- **AI キー** をタップするか、画面下部から上にスワイプするとランチャーが開きます。
- アプリカードをタップすると起動します。
- 画面上半分から下にスワイプするとランチャーを閉じます。
- どのページからでも AI キーをダブルタップするとペットに戻ります。
- ランチャーを開いた状態で横向きから縦向きに回転させるとポモドーロに入ります。
- ランチャーを開いた状態で縦向きから横向きに回転させるとステータスボードに入ります。
- ランチャーを開いた状態で振ると答えの書に入ります。
- AI キー以外のサイドキー 2 つを長押しするとディープスリープに入ります。

向きの変更は、動きが収まり最終的な姿勢が安定してからはじめて受け付けられます。安定したサンプル 5 回で回転が確定し、ランチャー上で 800 ms 連続して振ると答えの書が選択されます。答えの書自体の質問フローには、3 秒間の有効な振りが必要です。

## ePaper・電力・時刻

ディスプレイは電源がなくても最後の画像を保持するため、ファームウェアはあらゆる更新を限られたリソースとして扱います。静的なページにはきれいなベースラインを与え、カウントダウンの数字やアニメーション領域にはより小さな更新ポリシーを使い、定期的な全体更新でコントラストを回復させます。

ペットはスリープ前に次の意味のあるイベントをスケジュールします。PCF8563 RTC は、一定間隔で起こすのではなく、外出などの自律的なイベントの直前に ESP32-S3 を起こすことができます。バッテリー残量は BQ27220 フューエルゲージから取得し、UI は充電中・スリープのインジケーターをアプリの内容に重ならない形で表示し続けます。

ディープスリープは、AI キー以外のサイドキー 2 つを長押しして要求することもできます。スリープ前には、実行中のアプリが安定したページを保存し、入力周辺機器が安全に停止し、必要に応じてディスプレイがクリーンアップされ、RTC に次の意味のある起床時刻が設定されます。

## 完全ビジュアルツアー

以下の画面はすべて、ファームウェア実物の `Canvas` と 1 ビットアセットからレンダリングされたものか、デザイン履歴として明示的にラベル付けされたものです。これらはデバイスのビルドが使うレイアウトとアセットそのものです。

### デスクトップペット: 卵から個性ある相棒へ

| 卵とホーム | 子どもと青年 |
| --- | --- |
| <img src="docs/images/desktop-pet/egg.png" alt="ペットの卵のページ" width="235"> <img src="docs/images/desktop-pet/home-firmware-render.png" alt="ファームウェアが描画したペットのホーム" width="235"> | <img src="docs/images/desktop-pet/child.png" alt="子どものウサギ" width="235"> <img src="docs/images/desktop-pet/youth.png" alt="青年のウサギ" width="235"> |

| 性格と成人 | 睡眠と外出 |
| --- | --- |
| <img src="docs/images/desktop-pet/personality-choice.png" alt="性格選択のページ" width="235"> <img src="docs/images/desktop-pet/adult.png" alt="大人のウサギ" width="235"> | <img src="docs/images/desktop-pet/sleep.png" alt="ペットの睡眠ページ" width="235"> <img src="docs/images/desktop-pet/outing.png" alt="ペットの外出ページ" width="235"> |

ペットのモデルは表示コードから独立しています。RTC 時刻が欲求と年齢を進め、インタラクションがバージョン付きの状態オブジェクトを更新し、チェックサム付きの 2 つの NVS スロットがセーブを保護し、UI はステージと性格に応じたポーズを選びます。セリフの選択はステージ・絆・活動・直近の履歴でフィルタリングされ、すぐに同じ文が繰り返されるよりも別の有効な文が優先されます。

### ポモドーロ: 設定、集中、そして終了

<p align="center">
  <img src="docs/images/pomodoro/setup.png" alt="ポモドーロの設定" width="145">
  <img src="docs/images/pomodoro/custom-time.png" alt="カスタム時間のキーパッド" width="145">
  <img src="docs/images/pomodoro/running.png" alt="カウントダウン中" width="145">
  <img src="docs/images/pomodoro/end-dialog.png" alt="終了確認" width="145">
  <img src="docs/images/pomodoro/alarm.png" alt="時間切れアラーム" width="145">
</p>

ホームページには実用的な 3 つのプリセット、15 分・25 分・60 分だけを残しています。カスタム入力では時・分・秒を個別に指定でき、`CLEAR` は編集中のフィールドをリセットし、`DELETE` はバックスペースとして動作します。カウントダウンは実際の経過時間を使い、更新中もタッチの応答性を保ち、一時停止とその場での終了確認に対応し、`END` がタップされるまで穏やかなアラームを繰り返します。

### ステータスボード: ひと目で伝わるひとつの状態

| メニュー | 全画面ステータス |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="ステータスボードのメニュー" width="380"> | <img src="docs/images/status-board/status.png" alt="全画面のステータス" width="380"> |

| Open to talk | デバイス上のカスタムテキスト |
| --- | --- |
| <img src="docs/images/status-board/open-to-talk.png" alt="Open to talk のステータス" width="380"> | <img src="docs/images/status-board/custom.png" alt="カスタムステータスのキーボード" width="380"> |

`BUSY`、`MEETING`、`ON CALL`、`OPEN TO TALK`、`REST`、`CUSTOM` はそれぞれ横向きの第 2 階層ページを全画面で開きます。プリセットのページには対応するウサギのシーンが表示され、カスタムページでは英字・数字のレスポンシブなキーボードが使え、選択したテキストは現在のセッションのあいだ保持されます。

### 答えの書: 問い、振り、考え、明かす

<p align="center">
  <img src="docs/images/book-of-answers/home.png" alt="答えの書のホーム" width="145">
  <img src="docs/images/book-of-answers/thinking.png" alt="思考中のアニメーション" width="145">
  <img src="docs/images/book-of-answers/shake-longer.png" alt="もっと振るよう促す表示" width="145">
  <img src="docs/images/book-of-answers/message-result.png" alt="メッセージの回答" width="145">
  <img src="docs/images/book-of-answers/crystal-result.png" alt="水晶玉の回答" width="145">
</p>

`MESSAGE` が既定で選択され、組み込みの 350 種類の回答から引き出します。`YES OR NO` は水晶玉を使い、`YES`、`NO`、`UNCLEAR` のいずれかを返します。ホームページは 3 秒間の案内が読みやすいままになるよう、視覚的に静かに保たれています。短い振りでは再挑戦を促すメッセージが表示され、条件を満たした振りは思考と開示のアニメーションを経て結果を表示します。

### 2 つの物理的な向きに対応したランチャー

| 縦向きレイアウト | 横向きレイアウト |
| --- | --- |
| <img src="docs/images/launcher/launcher-portrait.png" alt="縦向きのアプリランチャー" width="245"> | <img src="docs/images/launcher/launcher-landscape.png" alt="横向きのアプリランチャー" width="500"> |

ランチャーは、ePaper の更新が始まる前、AI キーが物理的に押された時点で IMU の監視を開始します。タッチによる選択とモーションによる選択は同時に利用できます。選択されたアプリはデバイスの最終的な姿勢を受け取るため、描画とタッチ座標が同じ見た目の向きを使います。

### 6 ページの初回起動ガイド

ガイドは NVS の状態が新しい場合に一度だけ表示され、ペットのホーム画面にあるイラストの本からいつでも開き直せます。

| ようこそ | 世話の指標 | 成長の結果 |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-1.png" alt="チュートリアルのようこそページ" width="220"> | <img src="docs/images/onboarding/tutorial-page-2.png" alt="ペットの指標のチュートリアル" width="220"> | <img src="docs/images/onboarding/tutorial-page-3.png" alt="ペットの成長結果のチュートリアル" width="220"> |

| アプリケーション | ランチャー操作 | 回転と振り |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-4.png" alt="アプリケーションのチュートリアル" width="220"> | <img src="docs/images/onboarding/tutorial-page-5.png" alt="ランチャー操作のチュートリアル" width="220"> | <img src="docs/images/onboarding/tutorial-page-6.png" alt="回転と振りのチュートリアル" width="220"> |

## 対象ハードウェア

| コンポーネント | ファームウェアでの用途 |
| --- | --- |
| ESP32-S3R8 | アプリケーション、グラフィックス、入力のルーティング、低消費電力制御 |
| 800 × 480 モノクロ ePaper | 縦向き・横向きのアプリケーション UI |
| GT911 | 静電容量式のタッチとスワイプ入力 |
| LSM6DS3TR-C | 安定した向きの判定、モーションセッション、連続した振りの検出 |
| PCF8563 | カレンダー時刻とスケジュールされた起床 |
| BQ27220 | バッテリー残量 |
| ブザー | 状況に応じたペットの音とポモドーロのアラーム |
| microSD スロット | 起動時に安全に準備される共有 SPI のハードウェア経路 |

実装は、Seeed Studio の Sticky ハードウェアサンプルが示すボード初期化とドライバーの挙動に従っています。

## クイックスタート

### 必要なもの

- reTerminal Sticky
- USB-C データケーブル
- PlatformIO Core 6.1 または PlatformIO IDE
- Python 3
- macOS、Linux、または Windows

このプロジェクトは `espressif32@6.11.0` に固定され、ESP-IDF 5.4.1 でビルドされます。

### クローンとビルド

```bash
git clone https://github.com/limengdu/reTerminal_Sticky_Bunny.git
cd reTerminal_Sticky_Bunny
pio run
```

既定の環境は `sticky-release` です。ビルドに成功すると次のファイルが生成されます:

```text
.pio/build/sticky-release/firmware.bin
```

### リリースファームウェアの書き込み

```bash
pio run -e sticky-release -t upload
```

開発用ログを取得する場合:

```bash
pio run -e sticky-debug -t upload
pio device monitor -e sticky-debug
```

モニターのボーレートは `115200` です。起動に成功すると `sticky_boot: phase=ready result=ok` に到達し、続いてアクティブなアプリが報告されます。まっさらなペットから始めてオンボーディングをもう一度見るには、フラッシュを消去してから書き込み直してください:

```bash
pio run -e sticky-release -t erase
pio run -e sticky-release -t upload
```

## ビルドプロファイル

| 環境 | 目的 | 実行時のルール | ログ |
| --- | --- | --- | --- |
| `sticky-release` | 日常利用とリリース | 本番の時間・上限・外出スケジュール | 重要な警告とライフサイクルイベント |
| `sticky-debug` | ハードウェアと操作の診断 | 本番のゲームプレイルール | アプリ・入力・ストレージの詳細ログ |
| `sticky-power-test` | スリープ/ウェイクの高速な検証 | 電力まわりのタイミングのみ短縮 | 電力に焦点を当てた診断 |

リリース前にはすべてのプロファイルをビルドしてください:

```bash
pio run -e sticky-release -e sticky-debug -e sticky-power-test
```

## リポジトリ構成

```text
src/
├── app/                 # アプリマネージャー、ランチャー、ルーティング、ライフサイクル
├── apps/                # ペット、ポモドーロ、ステータスボード、答えの書、オンボーディング
├── board/               # 電源、充電、共有バス、ピン設定
├── devices/             # バッテリー、RTC、ブザーのドライバー
├── display/             # ePaper の所有権と更新処理
├── input/               # ボタンと GT911 タッチキュー
├── sensors/             # IMU の向きと振りのセッション
└── ui/                  # Canvas、フォント、オーバーレイ、生成されたピクセルアセット

assets/                  # オリジナルアート、ファームウェア用画像、QA レンダリング
docs/images/             # 厳選した README のスクリーンショットとデザイン履歴
docs/desktop_pet_growth_system.md
                         # ペットの詳細なルールとソース監査
test/                    # ネイティブの状態・ポリシー・描画テスト
tools/                   # 決定論的なアセット・データベース生成ツール
third_party/             # 取り入れたオープンソースのアイデアに関するライセンス告知
```

### ファームウェアの実行フロー

1. `app_main()` がバッテリー電源のラッチを保持し、ボードの電源経路を初期化します。
2. 共有 SPI と I²C のオーナーが、ディスプレイ・タッチ・RTC・バッテリー・ブザー・ボタン・IMU の各クライアントより先に起動します。
3. 新品のデバイスは 6 ページのチュートリアルに入り、設定済みのデバイスはペットのセーブを復元して適切なルートページを開きます。
4. アプリコーディネーターは、常にひとつのアプリだけに入力とディスプレイの所有権を与えます。
5. ランチャーはアクティブなアプリを一時停止し、タッチと IMU による選択を受け取ってから、再開または所有権の切り替えを行います。
6. ディープスリープの前に、安定した状態が保存され、周辺機器が停止し、ディスプレイが準備され、RTC のアラームがプログラムされます。

公開されるハードウェアとアプリのインターフェースには、簡潔な二言語のコメントが付いています。純粋な C++ の状態・ポリシー・ルーティング・保存レコード・描画の各モジュールは、開発用コンピューター上で挙動を検証できるよう、可能な限り ESP-IDF から独立させています。

## テストとビジュアル QA

ネイティブテストは、デバイスなしで状態機械を動かし、ページを PPM ファイルへレンダリングします。カバー範囲は次のとおりです:

- ペットの進行、オフライン時間、セリフ、アニメーションのスケジューリング、デュアルスロット保存
- ポモドーロの入力、カウントダウンのポリシー、ページレイアウト、アラームの状態
- ステータスの選択、カスタムテキスト、ステータス固有のアニメーション
- 回答の選択、3 秒間の振りの成立判定、結果のレイアウト
- ランチャーのタッチ領域、スワイプジェスチャー、向きによるルーティング、電力ポリシー
- オンボーディングのナビゲーションと最終的な 6 ページすべて

```bash
./tools/run_host_tests.sh
python3 tools/check_markdown_links.py
```

描画テストは、ペット、ポモドーロ、ステータスボード、答えの書、ランチャー、すべてのオンボーディングページを含む PPM プレビューを `/tmp` に書き出します。これらのプレビューは、ファームウェア実物の Canvas、フォント、タッチマップ、生成されたピクセルアセットを使用します。

### アセットの再生成

```bash
python3 -m pip install -r requirements-dev.txt
python3 tools/generate_desktop_pet_assets.py
python3 tools/generate_pomodoro_assets.py
python3 tools/generate_app_launcher_assets.py
python3 tools/generate_book_of_answers_assets.py
python3 tools/generate_onboarding_assets.py
python3 tools/generate_pixel_bunnies.py
```

生成された C++ アセットは `src/ui/assets/` 以下に書き出されます。入力が変わらない状態でジェネレーターを再実行しても、ソースの差分は発生しません。

## ハードウェアとドライバーの対応

<p align="center">
  <img src="docs/images/hardware/sticky-button-layout.png" alt="reTerminal Sticky のボタンと SD カードの配置" width="620">
</p>

| ハードウェア経路 | ファームウェア側の担当 |
| --- | --- |
| ePaper と microSD の共有 SPI | `src/board/board_shared_spi.*` がディスプレイと SD の所有権を直列化します |
| `0x14` の GT911 | `src/input/sticky_touch.*` が離されたタップと完全なスワイプ経路を記録します |
| `0x6A` の LSM6DS3TR-C | `src/sensors/sticky_imu.*` が観測された動き、安定した姿勢、振りのセッションを報告します |
| `0x51` の PCF8563 | `src/devices/sticky_rtc.*` が日付、経過時間、アラームによる起床を提供します |
| `0x55` の BQ27220 | `src/devices/sticky_battery.*` が充電残量を提供します |
| GPIO 48 のブザー | `src/devices/sticky_buzzer.*` がノンブロッキングでアプリとペットのパターンを鳴らします |

## トラブルシューティング

| 症状 | 確認すること |
| --- | --- |
| 書き込みで接続できない | データ通信対応のケーブルを使い、シリアルモニターを閉じ、現在の `/dev/cu.*` または COM ポートを選択してから、もう一度書き込んでください。 |
| 以前のペットの値が残る | 上記の消去と書き込みのコマンドを実行してください。書き込みだけでは設計上 NVS が保持されます。 |
| 表示はできるがタッチが効かない | `sticky-debug` を使い、`GT911` が ID `911`、アドレス `0x14`、センサー `480x800`、`touch=polling_ready` を報告することを確認してください。 |
| 画面に古い残像が残る | 起動時に白の全体クリアが実行され、定期的なクリーンアップ更新が行われていることを確認してください。 |
| 回転すると誤ったページが選ばれる | 確定した `from` と `to` の向きのログを、デバイスの実際の最終姿勢と比較してください。 |
| 短く振っただけで答えが表示される | 新しい静止ゲートが有効になっており、有効なピークが 3 秒の質問ウィンドウ全体にわたっているか確認してください。 |
| USB を外すとバッテリー動作が止まる | ディスプレイの初期化より前に、起動ログが電源ラッチと充電経路を報告しているか確認してください。 |

## デザイン履歴

このプロジェクトは、実機でのテストと ePaper UI の反復的な検討を通じて育ってきました。上に示したコードで描画された画面が出荷時の正となるもので、以下に選んだコンセプトは、ウサギ・ランチャー・チュートリアル・集中まわりの表現がどのように発展したかを示しています。

<details>
<summary><strong>デザインとアセットのギャラリーを開く</strong></summary>

### デスクトップペットのホームの方向性

<p align="center">
  <img src="docs/images/design-history/desktop-pet-home-concept.png" alt="デスクトップペットのホームのコンセプト" width="420">
  <img src="docs/images/design-history/pet-home-reference-comparison.png" alt="ペットホームの参考案とファームウェアの比較" width="420">
</p>

### アプリランチャーの方向性

<p align="center">
  <img src="docs/images/design-history/app-launcher-concept.png" alt="アプリランチャーのビジュアルコンセプト" width="650">
</p>

### 初回起動チュートリアルの方向性

<p align="center">
  <img src="docs/images/design-history/onboarding-concept.png" alt="初回起動チュートリアルのコンセプト" width="760">
</p>

### ポモドーロの案内の方向性

<p align="center">
  <img src="docs/images/design-history/pomodoro-guide-concept.png" alt="ポモドーロガイドのコンセプト" width="520">
</p>

</details>

## コントリビューション

Issue と Pull Request を歓迎します。まずは [CONTRIBUTING.md](CONTRIBUTING.md) を読み、ハードウェアの挙動が Sticky のリファレンス実装まで辿れる状態を保ち、挙動を変更する場合はネイティブの回帰テストを追加し、ハードウェアが必要な場合は実機での検証計画を含めてください。

## クレジットとライセンス

Sticky Bunny は [MIT ライセンス](LICENSE) で公開されています。

ペットのアーキテクチャは、TamaPoke、esp32-artoria-tamagotchi、openclaw-tamagotchi、ESP32-TamaPetchi の MIT ライセンスの小さなアイデアを取り入れています。元の告知と監査対象の正確なコミットは [third_party/virtual_pet/NOTICE.md](third_party/virtual_pet/NOTICE.md) に記録されています。このリポジトリのウサギのアートワークは、本プロジェクトのオリジナルです。

答えの書のメッセージデータベースは、Apache License 2.0 の `DBinK/The-Book-of-Answers-Interpreter` から派生したものです。その完全な告知はソースのデータベースとともに保存されています。

reTerminal Sticky は Seeed Studio の製品です。本プロジェクトはコミュニティによるファームウェアであり、デバイスの工場出荷時ファームウェアではありません。
