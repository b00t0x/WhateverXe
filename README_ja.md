# WhateverXe

[English](README.md)

WhateverXeは、WhateverGreenを基に開発した、一部のIntel Xe-LP内蔵GPU向けにAppleのグラフィックスドライバーを適応させるLiluプラグインです。
[WhateverXe](https://github.com/b00t0x/WhateverXe/releases)と[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases)は両方とも最新版を使用してください。変更点は[CHANGELOG](CHANGELOG.md)を参照してください。

## 対応範囲

| macOS | 必要バージョン |
| --- | --- |
| Big Sur | 11.7.11 |
| Monterey | 12.7.6 |
| Ventura | 13.7.8 |
| Sonoma | 14.8.9 |
| Sequoia | 15.8.x |
| Tahoe | 26.7以上 |

SequoiaとTahoeは同じOS系列内の互換性のある更新に対応します。将来のApple側の変更によりWEXやGraphicsの更新が必要になる場合があります。

### 実機で動作確認済み

- Tiger Lake-U：48 EU・80 EU構成。
- Alder Lake-N：16 EU・24 EU構成。
- Twin Lake：32 EU構成。
- Alder Lake-H：シングルチャネルを含む96 EU構成。
- Alder Lake-S：UHD Graphics 770。
- Raptor Lake-U：48 EU構成。

OS別の範囲と表示構成は[動作確認済みの機能と既知の問題](#動作確認済みの機能と既知の問題)、PCI IDは[デバイス選択](docs/device-selection.md)を参照してください。

### 動作を期待できるが実機未検証

- 96 EUを含む対応IDの他のTiger Lake-U/H35構成と、Tiger Lake-H45。
- 対応IDの他のAlder Lake-N / Twin Lake構成。
- 対応IDの他のAlder Lake-H/P/U・Raptor Lake-H/P/U構成。
- 対応IDの他のAlder Lake-S/HX・Raptor Lake-S/HX構成。
- Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uの内蔵eDPは実装済み・実機未検証です。

### 対応に追加改修が必要

- Rocket Lake-S：表示制御に追加対応が必要です。

### 対応予定なし

- Meteor Lakeの内蔵GPUなど、Xe-LPG。
- Xe2以降のグラフィックス世代。

## インストール

1. Lilu・`WhateverXe.kext`・[AMFIPass 1.4.1 Mod](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/extras/AMFIPass-v1.4.1-Mod-RELEASE.zip)をOpenCoreに追加し、WEXはLiluの後に設定します。WEXは**RELEASE** ZIPを使用してください。DEBUGは障害調査用です。
2. `csr-active-config=03080000`（`0x803`）と、[設定](#設定)を適用します。
3. 対象macOSを起動して最新版の[WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases)を導入し、再起動します。[pkgの導入・削除手順](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/README_ja.md#インストール)。

AMFIPassは本家版に代えてリンク先の**Mod版**を使用し、両方を同時に読み込まないでください。WhateverGreenとWhateverXeも併用できません。`amfi=0x80`と`ipc_control_port_options=0`は不要です。

Graphicsは`/Library`への配置とAuxKC生成を行い、Systemファイルやsnapshotにはパッチしません。旧Xe System patchは先に復元してください。他のroot patchのためにOCLPを使う場合は、同じMod署名に対応する[独自ビルド版OCLP](https://github.com/b00t0x/OpenCore-Legacy-Patcher)を使用します。

## 設定

### 必須ブート引数

OpenCoreの`boot-args`に追加します。

```text
-allow3d -disablegfxfirmware
```

### DVMT

BIOS/UEFIで設定できる場合は、**DVMT Pre-Allocatedを96MB**にします。
**60MB固定**の場合は、`-igfxdvmt`を追加し、`DeviceProperties > Add`のiGPU（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）に以下を設定します。

| Property | 型 | 値（hex） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

96MBの場合はこれらの設定は不要です。firmwareが確保するメモリを増やす設定ではありません。

### その他の設定

- 内蔵画面の輝度調整には`SSDT-PNLF`を追加します。
- GPUは自動選択するため、通常は`device-id`上書きは不要です。
- Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uではタイル形式を自動選択します。`-yTileDisable`は使わないでください。`AAPL,GfxYTile`は不要です。

## 動作確認済みの機能と既知の問題

共通の回帰項目はMetal・OpenGL・OpenCL・H.264ハードウェアエンコード／デコード・HEVCデコードです。以下にOS別の範囲と、各構成に固有の表示機能をまとめます。

| 構成 | 確認した範囲 |
| --- | --- |
| Tiger LakeミニPC | Big Sur〜Tahoe：80 EU、1080p60の2画面、消灯復帰、Safari動画・Widevine |
| Tiger Lakeラップトップ | Big Sur〜Tahoe：48 EU、内蔵eDPと輝度調整、HDMI/USB-C、最大3画面・消灯復帰 |
| Alder Lake-NミニPC | Big Sur〜Tahoe：16/24 EU、1080p60のHDMI 2画面・消灯復帰 |
| Alder Lake-N / Twin Lakeラップトップ | Big Sur・Tahoe：MicroPC 2・MiniBook X N100のMIPI DSI・輝度・回転・消灯復帰・HDMI/USB-C共存。MicroPC 2はHiDPI・最大3画面、MiniBook X N100は任意設定の60 Hzも確認 |
| Raptor Lake-Uラップトップ | Big Sur・Tahoe：MiniBook X U300の内蔵MIPI DSI・輝度・回転・HiDPI・USB-C外部2画面。内蔵は50 Hz、Big Surでは任意設定の60 Hzも確認 |
| Alder Lake-H | Big Sur〜Tahoe：96 EU、HDMIとDP→HDMI変換器による3画面。シングルチャネル構成の`4626`でもBig Surの加速を確認 |
| Alder Lake-S | Big Sur〜Tahoe：UHD 770、HDMI/DP/USB-C、最大3画面、Safari動画。WidevineはBig Sur/Tahoeで確認 |

MIPI DSI対応は**試験的**で、検証したGPD MicroPC 2・CHUWI MiniBook X N100/U300の内蔵パネルに限ります。他の機器での動作を期待しないでください。

システム全体のスリープ／復帰対応は**試験的**です。実機確認済みの構成は、機種別ACPI設定を使用したMiniBook X N100/U300（MIPI DSI）のBig Sur・Tahoeで、内蔵単独と外部2画面までです。他機種・他OSと長時間のDarkWake反復は未検証です。

### 既知の問題と制限

- DisplayPortの再接続で黒画面・停止が起きる場合があります。Tiger LakeのUSB-C再接続にも未解決の問題があり、Alder Lake-SのDP表示開始・再接続は時間がかかる場合があります。
- 一部の画像変換でアプリがクラッシュすることがあります。
- Safari動画で時折コマ落ちします。FairPlay保護コンテンツは未対応です。
- 同時表示は最大3画面です。検証構成を超える解像度は未検証です。
- 一部の端子配線・輝度制御方式、MST、Thunderbolt管理の出力は未対応です。

### ロードマップ

- Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/UでのDisplayPort・USB-C・内蔵画面対応の拡充。
- 画面再接続の安定性改善とシステム全体のスリープ／復帰の対応範囲拡充。

## ビルドとテスト

macOS、Xcode Command Line Tools、Python 3.9以上、MacKernelSDK、Lilu 1.7.2の開発用ヘッダー／ライブラリが必要です。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に置くか、`--sdk`・`--lilu`で指定します。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

出力先は`build/`です。AppleIntelTGLCompatもここでビルドし、Graphicsで配布します。任意の実ドライバー再生試験には`--donor`・`--accelerator`で対応する原本を指定します。

## ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。LiluとMacKernelSDKは別の依存コンポーネントです。

Xeグラフィックスの取り組みを公開している[JulioNectar/XPS13Plus](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe)に感謝します。同リポジトリのcompiler adapterとLLVM bridgeを、WhateverXe GraphicsのOS標準AIR変換の参考にしました。

内蔵パネルの対応では、[WhateverXe-RaptorLakeP](https://github.com/samranahmad09/WhateverXe-RaptorLakeP)、[X1Carbon-G11](https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe)、XPS13Plus、[Linux i915の表示ソース](https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/i915/display)も、ハードウェアの挙動と実装の比較に参照しています。

MiniBook Xの任意設定60 Hzのパネルタイミングには、[minibookx-ctlの表示ガイド](https://github.com/ishad0w/minibookx-ctl/blob/main/docs/guide/display.md)も参照しています。
