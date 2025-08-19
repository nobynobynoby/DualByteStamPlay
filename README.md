# DualByteStamPlay  
unofficial fan-made project inspired by “KANTAN Play”

## 重要事項

本プロジェクトはKANTAN Play/InstaChordにインスパイアされたプロジェクトです。
InstaChord株式会社、およびkantan-music公式プロジェクトではありません


## 概要
M5StampS3を使用したデジタル楽器プロジェクトです。2つのM5Unit-ByteButtonとMIDI音源ボードを組み合わせて、直感的な音楽演奏システムを構築しています。
M5社の各種パーツを使用しており、はんだ付け不要でくみ上げることが可能です。

## 主な機能

### 🎵 KANTAN Music API統合
- コード理論に基づいた音楽生成(KANTAN Music API使用)
- 6つのGM音色（ギター、ピアノ、オルガン、ストリングス等）
- リアルタイムアルペジオ演奏（FreeRTOSタスク使用）
- 自動ボイシング（ギター/スタティック）

### 🎮 デュアルボタンコントロール
**ByteButton1（表面）- コード演奏**
- ボタン7～1: I, II, III, IV, V, VI, VII（ディグリー）
- ボタン0: メジャー/マイナースワップ

**ByteButton2（裏面）- モディファイア**
- ボタン0: フラット
- ボタン1-3: コード修飾（7th, Sus4, Dim）
- ボタン4-5: 音色変更
- ボタン6-7: キー変更

### 🎸 ストロークタイムの変化
- ギターの弦をゆっくりはじくような演奏をアナログスライダーにより実現

## ハードウェア構成

### 構成一覧と役割

| コンポーネント | 役割・使い方 | 接続・備考 | ドキュメント |
|---|---|---|---|
| **M5StampS3** | メインコントローラー。全ての制御・演奏ロジックを実行。 | 各ユニット・LED・MIDI音源と接続 | [公式](https://docs.m5stack.com/en/core/stampS3) |
| **M5Unit-ByteButton ×2** | 8ボタン×2で計16ボタン。コード/モディファイア/音色/キー操作。 | I2C接続（SDA:GPIO13, SCL:GPIO15）<br>表面:0x53, 裏面:0x4F | [公式](https://docs.m5stack.com/en/unit/byte_button) |
| **Unit Synth (SAM2695)** | MIDI音源。MIDI信号を受けて音声出力。 | Grove端子（TX:GPIO2, RX:GPIO1）<br>標準MIDIボーレート:31250bps | [公式](https://docs.m5stack.com/en/unit/Unit-Synth) |
| **Unit Fader** | アナログスライダー。ストロークタイム調整用。SK6812 LED搭載。 | G9:アナログ入力<br>LEDは独立制御 | [公式](https://docs.m5stack.com/en/unit/fader) |
| **StampS3 Grove BreakOut** | Grove端子拡張・電源供給。各ユニットの接続基板。 | StampS3に直結 | [公式](https://docs.m5stack.com/en/accessory/StampS3%20GroveBreakOut) |




#### 各ユニットの使い方
- **ByteButton（表面）**: コード演奏（I, II, III...）やマイナー化（長押し）
- **ByteButton（裏面）**: 音色/キー変更、拡張モディファイア
- **Unit Fader**: ストロークタイム（アルペジオ速度）調整
- **Unit Synth**: MIDI受信で音声出力。スピーカー/ヘッドフォン接続可
- **LED**: 演奏状態や起動時テスト表示

#### 公式ドキュメント
- [M5StampS3](https://docs.m5stack.com/en/core/stampS3)
- [M5Unit-ByteButton](https://docs.m5stack.com/en/unit/byte_button)
- [Unit Synth (SAM2695)](https://docs.m5stack.com/en/unit/Unit-Synth)
- [Unit Fader](https://docs.m5stack.com/en/unit/fader)
- [StampS3 Grove BreakOut](https://docs.m5stack.com/en/accessory/StampS3%20GroveBreakOut)

## 使用ライブラリ

```ini
fastled/FastLED@^3.9.10                           # LED制御
https://github.com/m5stack/M5Unit-ByteButton.git  # ボタン入力
kantan-music                                       # 音楽理論API（ローカル）
```

## セットアップ

### 1. 環境準備
```bash
# PlatformIOプロジェクトとして開く
pio project init --board m5stack-stamps3
```

### 2. ライブラリインストール
```bash
pio lib install fastled/FastLED@^3.9.10
pio lib install https://github.com/m5stack/M5Unit-ByteButton.git
```

### 3. ビルド＆アップロード
```bash
pio run --target upload --environment m5stack-stamps3
```

### 4. 動作確認
```bash
pio device monitor --environment m5stack-stamps3
```

## 使用方法

### 基本操作
1. **キー設定**: ByteButton2のボタン6/7でキー変更（C, Db, D...）
2. **音色選択**: ByteButton2のボタン4/5で音色変更
3. **コード演奏**: ByteButton1のボタン1-7でディグリー演奏
4. **モディファイア**: ByteButton1のボタン0（長押し）でマイナー化
5. **音量調整**: MIDI音源ボード側で調整

### 音色リスト
1. **SteelGtr** - スチールストリングギター（GM#26）
2. **JazzGtr** - ジャズギター（GM#27）
3. **Piano** - アコースティックピアノ（GM#1）
4. **EPiano1** - エレクトリックピアノ（GM#5）
5. **ChurchOrg** - チャーチオルガン（GM#20）- 4音色
6. **Strings** - ストリングスアンサンブル（GM#49）- 4音色

### アルペジオ設定
- **ストローク時間**: G9のアナログ値で5-125ms調整
- **4音色モード**: オルガン・ストリングスでは2,3音をスキップ

## 技術仕様

### FreeRTOS活用
- **メインタスク**: ボタン処理、MIDI制御
- **アルペジオタスク**: 非同期音符送出（優先度1）
- **タイマー**: Note Off自動送信（6本の独立タイマー）

### デバウンス詳細
- **検出間隔**: 20ms
- **状態管理**: 生状態→安定状態→イベント生成
- **メモリ効率**: ビット演算による8ボタン×2デバイス管理

### MIDI最適化
- **レイテンシ削減**: Serial1.flush()による即座送信
- **重複防止**: 世代ID管理によるキャンセル機能
- **音量制御**: 起動時マスターボリューム・チャンネルボリューム設定

## 開発環境

- **Platform**: ESP32 (espressif32)
- **Framework**: Arduino
- **Build System**: PlatformIO


## ライセンス

このプロジェクトにはKANTAN Music APIが含まれています。詳細は`kantan-music/LICENSE_KANTAN_MUSIC.md`を参照してください。

KANTAN Music APIの元プロジェクト: [InstaChord/KANTAN_Play_core](https://github.com/InstaChord/KANTAN_Play_core)

上記以外のライセンスはMITライセンス準拠です。

[MIT License (LICENSEファイル)](./LICENSE)


## 関連製品ドキュメント

- [M5StampS3 ドキュメント](https://docs.m5stack.com/en/core/stampS3)
- [M5Unit-ByteButton ドキュメント](https://docs.m5stack.com/en/unit/byte_button)
- [Unit Synth (SAM2695 MIDI音源) ドキュメント](https://docs.m5stack.com/en/unit/Unit-Synth)
- [Unit Fader (アナログスライダー) ドキュメント](https://docs.m5stack.com/en/unit/fader)
- [StampS3 Grove BreakOut ドキュメント](https://docs.m5stack.com/en/accessory/StampS3%20GroveBreakOut)
