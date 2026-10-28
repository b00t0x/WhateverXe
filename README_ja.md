# WhateverXe

[English](README.md)

WhateverXeは、一部のIntel Xe-LP内蔵GPU向けにAppleのグラフィックスドライバを適応させるLiluプラグインです。WhateverGreenを基に開発しています。

本プロジェクトは実験段階であり、[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)の署名済みグラフィックスpkgが必要です。資材を`/Library`へ配置してAuxiliary Kernel Collection（AuxKC）を更新し、Systemファイルやsnapshotにはパッチを適用しません。WEXとグラフィックスpkgは、それぞれ独立したバージョンでリリースします。

WEXとWhateverXe Graphicsは、どちらも最新版を使用してください。

## 対応範囲

単一のWEXバイナリと1つのグラフィックスpkgで、以下のmacOSに対応します。

| macOS | 必要バージョン |
| --- | --- |
| Big Sur | 11.7.11 |
| Monterey | 12.7.6 |
| Ventura | 13.7.8 |
| Sonoma | 14.8.9 |
| Sequoia | 15.8.x |
| Tahoe | 26.7以上 |

Big Sur〜Sonomaは検証済みカーネルの識別を必要とします。SequoiaとTahoeでは、実行時の命令列・ABI検査を維持した上で、同じOS系列内の更新を受け付けます。将来の更新が検証済みという意味ではありません。

WEXはOSのABIとGPUバックエンドを独立して選択します。OS別のグラフィックス資材と共通のAppleIntelTGLCompatはWhateverXe Graphicsが導入し、GPU世代・コネクタ配線への対応はWEXが担います。

### 実機で動作確認済み

| GPUファミリー | 確認した構成 |
| --- | --- |
| Tiger Lake-U | 48 EU・80 EU。外部画面を使う構成とノートPCの画面構成 |
| Alder Lake-N | 16 EU・24 EU。HDMI 2画面 |
| Alder Lake-H | 96 EU（`46A6`）。固定TC HDMIとDP→HDMI変換器経由のDisplayPort |
| Alder Lake-S | UHD Graphics 770（`4690`）。HDMI、DisplayPort、固定DisplayPortとして配線されたUSB-C端子 |

各ファミリーの一部構成で確認した結果であり、全モデル・全端子の動作保証ではありません。
OS別の検証範囲と残る問題は[動作確認済みの機能と既知の問題](#動作確認済みの機能と既知の問題)を参照してください。

### 動作を期待できるが実機未検証

- 96 EUを含む、対応IDの他のTiger Lake-U/H35構成。
- Tiger Lake-H45（GT1）：`9A60`、`9A68`、`9A70`。
- 対応IDの他のAlder Lake-N構成とTwin Lake。
- 対応する固定HDMIまたは固定TC DisplayPort配線の、他のAlder Lake-H/P/U・Raptor Lake-H/P/U構成。
- 対応IDの他のAlder Lake-S構成、Alder Lake-HX、Raptor Lake-S、Raptor Lake-HX。

確認済みのファミリーと処理を共有する実装があり、IDは自動で受け付けます。
CPUモデル名や機種固有の設定は不要ですが、実機での動作確認は残っています。
受付IDの一覧と対応する資源構成は[デバイス選択](docs/device-selection.md)を参照してください。

実験的に、Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uの内蔵eDPを実装しています。対象はfirmwareで一意に記述されたCombo A/Bの単一パネルとPPS0・PCH PWMによる輝度制御です。CPU試験とdonor再生試験のみで、実機未検証です。対応するWhateverXe Graphicsリリースと組み合わせて使用してください。条件は[表示の制限](docs/device-selection.md#display-limits)を参照してください。

### 対応に追加改修が必要

- Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uの
  Combo PHY経由の外部DisplayPort・Type-C管理下の出力には追加改修が必要です。
  その他の電源・輝度制御方式を使うeDPパネルも追加対応が必要です。
  GPUのIDを受け付けるだけで、これらの出力が有効になるわけではありません。
- Rocket Lake-S：Tiger Lakeとは表示の電源・クロック制御が異なります。
  このリリースでは未対応で、実機検証の予定もありません。

### 対応予定なし

- Meteor Lakeの内蔵GPUなど、Xe-LPG。
- Xe2以降のグラフィックス世代。

## 必須構成

[Releases](https://github.com/b00t0x/WhateverXe/releases)から **RELEASE** ZIPをダウンロードしてください。DEBUG ZIPは問題調査用です。各ZIPには`WhateverXe.kext`と対応するdSYMのみを収録し、両ZIPのチェックサムは`SHA256SUMS`で配布します。各版の変更点は[CHANGELOG](CHANGELOG.md)を参照してください。

| コンポーネント | 役割 |
| --- | --- |
| Lilu | WhateverXeの動作に必要なランタイム |
| WhateverXe.kext | OpenCoreでLiluより後に読み込む |
| [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases) | 署名済み資材を`/Library`へ導入しAuxKCを更新する |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/extras/AMFIPass-v1.4.1-Mod-RELEASE.zip) | 改変されたグラフィックス構成に必要。OpenCoreで導入する |

SIPは`csr-active-config=03080000`（値`0x803`）に設定してください。SIPの全面有効化には対応していません。署名済みグラフィックスpkgとリンク先のMod AMFIPassを使用する場合、`amfi=0x80`と`ipc_control_port_options=0`は不要です。

Tahoeでは`CoreMedia / AllowOnePassMetalScaling`の無効化が必要です。WhateverXe Graphicsのインストーラーが設定します。

最新版のWEXを読み込んだ対象OSから、最新版のグラフィックスpkgを実行してください。旧Xe System patchがある場合は、先に元の復元手順で戻してください。pkgはEFI・Systemファイル・snapshot・SIP設定を変更しません。pkgの使用方法と単独のアンインストールスクリプトは[導入と削除](https://github.com/b00t0x/WhateverXe-Graphics/blob/main/README_ja.md#インストール)を参照してください。

リンク先の **Mod版** アーカイブから`AMFIPass.kext`を展開し、`EFI/OC/Kexts`へ配置して、`Kernel > Add`で有効にしてください。既存のAMFIPassがある場合はこのMod版へ置き換え、両方を同時に読み込まないでください。upstreamのAMFIPassはMod版の署名チェーンに対応していません。

他のroot patchにOCLPを併用する場合は、同じMod版の署名チェーンを使用する[独自ビルド版OCLP](https://github.com/b00t0x/OpenCore-Legacy-Patcher)を使用してください。Xeグラフィックスの導入自体はOCLPに依存せず、WhateverXe Graphicsを使用します。

複数GPUの構成を含め、WhateverGreenとWhateverXeは同時に有効にしないでください。

## 設定

GPUはPCI device IDから自動選択します。対応範囲内ではファームウェアのVBTから配線を読み取るため、固定HDMI／固定DisplayPort構成は通常、配線propertyの手動指定が不要です。高度な設定としての`device-id`上書きは[デバイス選択](docs/device-selection.md)で説明しています。

対応GPUでは、WhateverGreenと同様に、検出されたIntel Management Engineのデバイス名も`IMEI`へ統一します。`HECI`や`MEI`から`IMEI`へのACPI renameを別途追加する必要はありません。

Tiger Lakeはnative controllerとAUX配線が一致するVBTの固定DisplayPortをTC1〜TC4で自動選択します。固定DPとDP Alt Modeは検出したFIA構成を使用します。TC3/TC4の固定DPと非modular FIAは実装済み・実機未検証です。TC5/TC6と交差AUX配線には追加のbackend対応が必要です。

Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uでは、Combo A/B・固定TC1〜TC4 HDMI配線をVBTから選択します。試験的な固定TC1〜TC4 SST DisplayPortには、VBTのAUX配線の一致が必要です。Alder Lake-NのHDMI明示propertyはCombo配線にのみ適用します。

ADL-Hでは、固定HDMIとDP→HDMI変換器を使い、3画面接続での起動、ファームウェアによる拡大表示からの起動、DP再接続を確認しています。固定TC DP経路での直接DP接続の表示先と、これらのファミリーの他のGPUは未検証です。試験的なUSB-C DP Alt Modeは、AUXが一致するVBTの外部TC1〜TC4 DP配線に対応します。既存SSTリンク訓練にType-C所有権と検出した物理レーン割当を組み合わせます。MST、Thunderbolt、交差AUX配線、USB-Cを起動用画面とする引継ぎは未対応です。Alder Lake-NではBig SurでHDMI変換器経由のUSB-C 1080p60表示とMetal/OpenGL加速を確認しています。USB-Cの抜き差し・スリープ復帰・他の変換器は未検証です。

ADL-P/RPL-Pは物理IDと検出したGPU資源を自動で使用します。device-ID上書きや機種別AGDP kextは不要です。`-wex-noaccel`でFramebufferのみの動作を維持します。

試験用の `-wex-dsi-hdmi-test` は、限定したDisplay13のDSI-A GOPからCombo-Bへ1080p60で移す開発用経路です。内蔵表示を停止し、policyが別の接続済み出力へ切り替える際は暫定HDMI資源を解放します。既定では無効で、DSI対応や汎用のcold起動を提供するものではありません。

### 内蔵パネルの輝度

内蔵ディスプレイの輝度調整には、OpenCoreへ`SSDT-PNLF`を追加してください。

### DVMT

BIOS/UEFIで変更できる場合は、**DVMT Pre-Allocatedを96MBに設定することを推奨します**。

**60MBから変更できない場合**は、`boot-args`に`-igfxdvmt`を追加し、OpenCoreの`DeviceProperties > Add`にあるiGPUの項目（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）へ以下の2つを設定してください。

| Property | 型 | 値（16進数） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

Data値はリトルエンディアンです。`-igfxdvmt`は容量計算を補正し、DevicePropertiesは表示用メモリ領域を60MB内に収めます。ファームウェアが確保したメモリを増やす設定ではありません。96MBの場合、これらのDVMT用設定は不要です。

### 必須ブート引数

OpenCoreの`boot-args`に以下を追加してください。

```text
-allow3d -disablegfxfirmware
```

| 引数 | 用途 |
| --- | --- |
| `-allow3d` | Appleドライバの3D処理を有効にする。 |
| `-disablegfxfirmware` | 現在のバックエンドで、Appleドライバのグラフィックスファームウェア読み込みを無効にする。 |

`-wex-nopanel`ブート引数は廃止され、不要になりました。内蔵ディスプレイの有無は自動で判定します。

Alder Lake-N / Twin Lakeで世代選択や個別機能の有効化引数を追加する必要はありません。
GPU backendの選択と実機検証範囲は独立しています。未検証のPCI revisionも同じfamily実装を使い、
文書化された差分や実際の不具合で必要になった場合にrevision別の処理を追加します。

Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/UではYタイル出力を自動選択するため、OpenCoreの`AAPL,GfxYTile`設定は不要です。既存の値は保持します。
これらのGPUはXタイル出力に未対応なので、Appleドライバの`-yTileDisable`引数は使用しないでください。

FBの立上げ時は、任意の`-wex-noaccel`引数でAcceleratorの起動を止められます。
Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/UではGPU資源のprobeも省略し、FB独自の起動メモリ・MMIOの確認は維持します。
未対応のDisplayバックエンドを有効にする引数ではありません。

開発時の一時診断用に `-wex-adlp-readout` を指定すると、GPUドライバの受付を閉じたままADL-Pの起動資源を採取します。対応済みハードウェアでは不要です。
`-wex-adlp-load-only` は選択済みADL-P/RPL-Pで、FramebufferとAccelerator両方のnative probe/startを止め、GPUを初期化せずにロード段階を診断します。
`-wex-display13-offline` はAlder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/Uのnative display controller初期化を有効に保ち、選択した出力を未接続として扱います。FB診断でHPD/EDIDとmode設定を切り離すための設定で、表示成立を意味しません。

## 動作確認済みの機能と既知の問題

| ハードウェア | 確認済みの機能 |
| --- | --- |
| Tiger Lake / Alder Lake-N | Big Sur〜Tahoe：外部2画面の1920×1080・60Hz出力、Metal/MPS、OpenGL、OpenCL、ハードウェアH.264エンコード／デコード、HEVCデコード、画面の消灯／復帰、Safariの動画デコード、Widevine再生 |
| Tiger Lakeラップトップ | Big Sur〜Tahoe：内蔵1366×768 eDP、輝度調整、HDMI、USB-C DisplayPort Alt Mode、最大3画面同時出力。USB-Cの再接続には未解決の問題あり |
| Alder Lake-N | Big Sur/Tahoe：EFIの`AAPL,GfxYTile`設定なしでのYタイル自動選択 |
| Alder Lake-H | Big Sur〜Tahoe：Metal、OpenGL、ハードウェアH.264エンコード／デコード、HEVCデコード、固定HDMIとDP→HDMI変換器を使った3画面 |
| Alder Lake-S | Big Sur〜Tahoe：Metal、OpenGL、OpenCL、ハードウェアH.264エンコード／デコード、HEVCデコード、3画面（1080p60×2＋720p60×1）、Safari動画再生。WidevineはBig SurとTahoeで確認 |
| Tiger Lake / Alder Lake-S | Tahoe：ChromeのCanvas、WebGL、WebGPUによるハードウェア描画 |

検証した機器での結果であり、すべてのGPU・ディスプレイ・アダプター・ドックでの動作を保証するものではありません。
記載した解像度は検証範囲であり、WEXが設けた固定の解像度制限ではありません。

### 既知の問題と制限

- 一部の画像変換処理でアプリがクラッシュすることがあります。
- SequoiaではApp Storeの動画再生でGPUが停止することがあります。
- 専用DisplayPort端子の抜き差しで黒画面やシステム停止が起きることがあります。
  USB-C DisplayPort Alt Modeでも再接続時のシステム停止がBig SurとTahoeの両方で再現しています。
- Safariの動画再生で時折コマ落ちすることがあります。
- 同時表示は最大3画面です。4画面目には映像が出ません。
- Alder Lake-SではDisplayPortの表示開始や再接続後の復帰に時間がかかる場合があります。
- 一部の端子配線、Thunderbolt管理の出力、内蔵画面の輝度制御方式は未対応です。
  詳細は[表示の制限](docs/device-selection.md#display-limits)を参照してください。
- FairPlayで保護されたコンテンツの再生には未対応です。
- システム全体のスリープ／復帰と、上記を超える解像度は未検証です。
  USB-C映像出力中は表示ハードウェアの一部が低消費電力状態へ移行しません。

## ロードマップ

- Alder Lake-N / Twin LakeとAlder Lake-H/P/U / Raptor Lake-H/P/UでのDisplayPort・USB-C・内蔵画面対応の拡充。
- 画面再接続の安定性改善とシステム全体のスリープ／復帰の調査。

## ビルドとテスト

macOS、Xcode command-line tools、Python 3.9以降、MacKernelSDK、Liluの開発用ヘッダ／ライブラリを使用します。CIではTahoeのkernel定義を含むLiluの[1.7.2リリースタグ](https://github.com/acidanthera/Lilu/tree/1.7.2)を使用します。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に配置するか、`--sdk`と`--lilu`でパスを指定してください。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

WhateverXeとAppleIntelTGLCompatのkext、およびソースのmanifestは`build/`に出力されます。TGLCompatのソースと共有ヘッダーはこのリポジトリで管理し、署名済みバイナリはWhateverXe Graphicsで配布します。WEXのRelease ZIPに含まれるkextはOpenCore用プラグインのみで、グラフィックスpkgは含みません。任意のdonor再生試験には`--donor /path/to/AppleIntelTGLGraphicsFramebuffer`と`--accelerator /path/to/AppleIntelTGLGraphics`を指定します。各試験が受け付ける正確なバイナリが必要です。CPUテストだけで実GPUの互換性が確認できるわけではありません。ビルドにWhateverGreenのソースは不要です。

## ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。LiluとMacKernelSDKは別の依存コンポーネントです。

Xeグラフィックスの取り組みを公開している[JulioNectar/XPS13Plus](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe)に感謝します。同リポジトリのcompiler adapterとLLVM bridgeを、WhateverXe GraphicsのOS標準AIR変換の参考にしました。

内蔵パネルの対応では、[WhateverXe-RaptorLakeP](https://github.com/samranahmad09/WhateverXe-RaptorLakeP)、[X1Carbon-G11](https://github.com/dssoftx/Hackintosh-X1Carbon-G11-IrisXe-Tahoe)、XPS13Plus、[Linux i915の表示ソース](https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/i915/display)も、ハードウェアの挙動と実装の比較に参照しています。
