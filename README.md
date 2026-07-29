# CausticsBaker for Unreal Engine 5.8

![Unreal Engine 5.8](https://img.shields.io/badge/Unreal%20Engine-5.8-0E1128?logo=unrealengine&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Win64-0078D6?logo=windows&logoColor=white)
![Rendering](https://img.shields.io/badge/Rendering-DX12%20%2F%20SM6-blue)
![Version](https://img.shields.io/badge/version-1.2.4-orange)
![Status](https://img.shields.io/badge/status-Beta-yellow)

UE 5.8 Editorのビューポート上で領域を指定し、Hardware Ray Tracingによるフォトンマッピングでコースティクスを計算・テクスチャへベイクするEditor専用プラグインです。

投影先はボックス内のメッシュから自動検出されるため、Receiverを1つずつ設定しなくても、デカールに近い感覚でコースティクスを配置できます。ガラスなどの誘電体と、金属などの導体に対応しています。

> [!IMPORTANT]
> Renderer Private APIを使用するため、対応バージョンは**Unreal Engine 5.8のみ**です。UE 5.7以前および5.9以降は、意図しない動作を避けるためコンパイル時に停止します。

## デモ

<!--
GitHubのREADME編集画面へ動画をドラッグ＆ドロップし、生成された
https://github.com/user-attachments/assets/...
のURLで次の「操作デモ動画を準備中です。」という行を置き換えてください。
URLを単独の行に置くと、GitHub上で動画プレイヤーとして表示されます。
-->

> 操作デモ動画を準備中です。

## 主な機能

- Hardware Ray Tracingのmaterial pipelineを利用したフォトンマッピング
- `Caustics Bake Region`の投影ボックスによるデカールライクな領域指定
- 投影ボックス内のReceiver自動検出と、任意のReceiver Filter
- Directional／Point／Spot Lightからのフォトン放出
- Static Mesh／Instanced Static Mesh／Hierarchical Instanced Static MeshのCasterとReceiver
- GGX、Fresnel、Snell屈折、全反射、Beer–Lambert吸収、Russian Roulette
- 最大4段の媒質スタックによる、複数の閉じた誘電体の追跡
- SPPM密度推定と、Guideを利用するGPU à-trousデノイザ
- Bake時に任意で追加できるIntel Open Image Denoise
- ビューポートへのHDR Previewと、Raw／Guideなどのデバッグ表示
- 線形HDR `RGBA16F`と通常のsRGB `BGRA8`出力
- バッチ間Cancel、非同期GPU Readback、Bake結果の更新検出
- エンジン改造およびCustom Primitive Dataの占有なし

## 動作環境

| 項目 | 必要条件 |
| --- | --- |
| Unreal Engine | **5.8** |
| OS | Windows 64-bit |
| RHI | DirectX 12 |
| Shader Model | SM6 |
| Ray Tracing | Hardware Ray Tracing、full ray-tracing shader pipeline |
| GPU | DirectX Raytracing対応GPU |
| 実行環境 | Editorのみ |

プロジェクトの`Project Settings > Engine > Rendering`で、少なくとも次を有効にしてEditorを再起動してください。

- Default RHI: DirectX 12
- D3D12 Targeted Shader Formats: Shader Model 6
- Support Hardware Ray Tracing
- UEから要求された場合はSupport Compute Skin Cache

CasterとReceiverに使用するコンポーネントでは、`Visible in Ray Tracing`も有効にします。

## インストール

### ビルド済みパッケージ

1. Unreal Editorを終了します。
2. 配布パッケージを`<Project>/Plugins/CausticsBaker`へ展開します。
3. `CausticsBaker.uplugin`が`<Project>/Plugins/CausticsBaker/CausticsBaker.uplugin`にあることを確認します。
4. プロジェクトを開き、必要ならPlugins画面で`Caustics Baker`を有効にして再起動します。
5. 初回起動時のGlobal Shaderコンパイルが完了するまで待ちます。

> [!WARNING]
> 同じプロジェクトへ複数バージョンのCausticsBakerを同居させないでください。更新時はEditorを終了してから旧フォルダを置き換えます。

### ソースから使用する場合

このリポジトリを`<Project>/Plugins/CausticsBaker`へ配置し、UE 5.8と互換性のあるVisual Studio 2022 C++ツールチェーンでEditorターゲットをビルドしてください。

## クイックスタート

1. Place Actorsから`Caustics Bake Region`をレベルへ配置します。
2. Regionの`Depth / Width / Height`で投影ボックスをCasterと投影先へ重ねます。
3. `Light Actor`でDirectional、Point、またはSpot Lightを1つ選択します。
4. `Casters`へ要素を追加し、コースティクスを発生させるStatic Mesh／ISM／HISMコンポーネントを選択します。
5. 必要に応じてCasterの`Optical Mode`と光学値を設定します。
6. `Receiver Filter (Optional)`は、通常は空のままにします。
7. `Preview`で結果を確認します。
8. 品質と出力形式を選び、`Bake`を実行します。
9. 生成されたTextureアセットをContent Browserから保存します。

Regionのローカル軸は次のように使用されます。

| ローカル軸 | 意味 |
| --- | --- |
| `+X` | 投影方向 |
| `Y` | 出力テクスチャのU |
| `-Z` | 出力テクスチャのV |

投影ボックスはRegion原点から`+X`方向へ伸びます。Guideはこの方向へレイを飛ばし、同じ投影UVで最初に見つかったReceiver 1層を採用します。

## Receiverの自動検出

`Receiver Filter (Optional)`が空の場合、投影ボックスと交差するray-tracing-visibleなStatic Mesh／ISM／HISMコンポーネントが自動的にReceiverになります。Casterへ登録したコンポーネントは自動Receiverから除外されます。

Receiver Filterは、投影先を特定のメッシュだけへ制限したい場合に使用します。フィルタを1つでも追加すると自動検出ではなく明示フィルタモードになります。

明示Receiverが現在のDepthより先にあっても、`Auto Fit Depth To Receiver Filter`が有効で、Width／Height内かつ`+X`方向にある場合はDepthを自動拡張します。

## Casterの光学設定

| Optical Mode | 用途 |
| --- | --- |
| `Auto from Material (Top Surface)` | Ray Tracing payloadの簡略化された最上位マテリアルから、光学タイプ、IOR／F0、roughness、tint、normalを取得します。 |
| `Dielectric Override (Glass)` | IOR、roughness、透過tint、吸収、Solid／Thinを明示するガラス向け設定です。 |
| `Conductor Override (Metal)` | roughnessと反射F0色を明示する金属向け設定です。 |

SubstrateのAutoモードは、Ray Tracing用に簡略化された最上位のSlabまたはSingle Layer Waterを使用します。任意のSubstrate積層をそのまま再現するものではありません。自動判定が意図と異なる場合は、GlassまたはMetalのOverrideを使用してください。

### シャープなガラスコースティクスの開始値

閉じたガラス球などでは、まず次の設定から調整することを推奨します。

| 項目 | 開始値 |
| --- | --- |
| Optical Mode | `Dielectric Override (Glass)` |
| Thickness Mode | `Solid (Closed Mesh)` |
| Index of Refraction | `1.5` |
| Roughness | `0.001` |
| Optical Tint | White |
| Absorption | `0` |
| Denoiser | `None`で形状確認後、必要に応じて有効化 |
| Initial Radius | `1.0 texel`から調整 |

Directional Lightの`Source Angle`、Point／Spot Lightの`Source Radius`、Casterのroughnessが大きいほどコースティクスは広がります。鋭い模様を確認するときは、最初に光源サイズを0へ近づけてください。

Receiverが光学的な焦点位置から外れている場合、フォトン数を増やしても模様は鋭くなりません。球体などではReceiverの距離も前後へ動かして確認します。

## 品質設定

| 実行 | 解像度 | バッチ数 | Photons / Batch | 総フォトン数 | Max Bounces | à-trous |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 標準Preview | 512² | 8 | 131,072 | 1,048,576 | 6 | 2 |
| Bakeプリセット | 2048² | 32 | 524,288 | 16,777,216 | 8 | 4 |

`Preset`を`Custom (Preview and Bake)`にすると、PreviewとBakeの両方で次を変更できます。

- Resolution: 256～4096の2の累乗へ丸められます
- Photon Batches
- Photons Per Batch
- Max Bounces
- Atrous Iterations
- Random Seed
- SPPM Convergence
- Initial Radius
- Filter Strength

総フォトン数は`Photon Batches × Photons Per Batch`です。値を増やすとノイズは減りますが、処理時間とGPU負荷も増えます。Initial Radiusを小さくすると細部を残しやすくなりますが、フォトン密度が不足するとノイズが増えます。

`Effective Preview / Effective Bake`には、現在の設定から実際に使用される解像度、総フォトン数、バウンス数、フィルタ反復数が表示されます。

## デノイズとデバッグ表示

- `GPU a-trous`: variance、normal、receiver depth、coverageを利用するedge-awareフィルタです。
- `GPU a-trous + Intel OIDN`: Bake時のみ、a-trous後にUE同梱Intel OIDNを追加します。
- `None`: 密度推定結果をそのまま確認します。細い集光線の評価に適しています。

`Debug Display`では`Final`、`Raw`、`Density Filtered`、`OIDN Result`、`Guide Depth`、`Guide Normal`、`Guide Coverage`、`Guide Receiver ID`を確認できます。

## 出力テクスチャ

既定の出力先は`/Game/Caustics/T_Caustics_<ActorName>`です。既存の同名Texture2Dがある場合は更新されます。

| Output Format | Source | Compression | sRGB | 用途 |
| --- | --- | --- | --- | --- |
| `16-bit Float HDR (RGBA16F)` | `TSF_RGBA16F` | `TC_HDR` | Off | 線形HDR照度を保持する既定形式 |
| `8-bit LDR (BGRA8)` | `TSF_BGRA8` | `TC_Default` | On | 通常のカラーTextureとして扱うLDR形式 |

8bit出力では、RGBを`8-bit White Level`で割って0～1へクランプした後、sRGBへ符号化します。たとえばWhite Levelが2なら、線形HDR値2が8bitの255になります。白飛びする場合はWhite Levelを大きくしてください。

どちらの形式も次の仕様です。

- RGB: Receiverのマテリアル色を含まない、ライトとCaster由来のコースティクス照度
- Alpha: Receiver coverage
- Address X／Y: Clamp
- Texture Group: Effects
- Mip: 通常の自動生成

生成パッケージはDirtyになりますが、自動保存されません。プラグインはruntime用Decal ActorやMaterialを生成しませんが、ベイクされたTexture2Dは通常のアセットとして別途マテリアルなどから利用できます。

## 処理パイプライン

<details>
<summary>実装の概要を表示</summary>

1. 同じEditor Worldを参照する64×64の一時SceneCaptureを作成します。
2. Capture専用ViewだけGIをPlugin、TranslucencyをRay Tracedへ設定します。
3. 投影レイからReceiver Guideを作成し、depth、shading normal、Persistent Primitive ID、coverageを保存します。
4. 1回のCaptureで1 photon batchを処理します。
5. Directional LightはCaster境界のライト空間矩形、Point／Spot LightはCaster群を囲む立体角からサンプリングします。
6. Photon RayGenでGGX、Fresnel、Snell屈折、全反射、吸収、媒質スタックを評価します。
7. photon recordsをinteger atomic count、prefix scan、8×8 tile bin、scatterで整理します。
8. World距離、normal、receiver IDを使うcone kernelでSPPM密度推定を更新します。
9. 必要に応じてGPU à-trousとIntel OIDNを適用します。
10. `FRHIGPUTextureReadback`完了後、Game ThreadでTexture2Dを更新します。

PreviewはScene DepthからWorld Positionを再構築し、Region内かつGuide depthと一致する面へPre-PostProcessでHDR結果を加算します。メインビューポートのGI設定は変更しません。

</details>

## トラブルシューティング

| 症状／メッセージ | 確認する項目 |
| --- | --- |
| Light Actorを選べない | コンポーネントではなく、Outliner上のDirectional／Point／Spot Light Actorを選びます。Detailsのeyedropperも使用できます。 |
| `Receiver guide rays did not intersect...` | 投影ボックスがReceiverと交差しているか、Regionのローカル`+X`が投影先を向いているか確認します。 |
| Receiver IDが一致しない | Receiver Filter、`Visible in Ray Tracing`、コンポーネントの登録状態を確認します。不要ならReceiver Filterを空に戻します。 |
| 半透明Casterが拒否される | `r.RayTracing.ExcludeTranslucent 0`を設定し、Casterの`Visible in Ray Tracing`を確認します。 |
| ガラスの模様がぼやける | Glass Override、Solid、roughness、光源サイズ、Initial Radius、Receiverの焦点距離を確認します。最初はDenoiserをNoneにします。 |
| 8bit出力が白飛びする | `8-bit White Level`を大きくします。HDR値がWhite Level以上のRGBは255へクランプされます。 |
| Preview／OutputがOut of Dateになる | Region、ライト、Caster／Receiver、マテリアル、Bake設定の変更後はPreviewまたはBakeを再実行します。 |
| Bake後もContent Browserへ保存されない | アセットは作成・更新されますが自動保存されません。Content BrowserまたはSave Allで保存します。 |
| Pluginの重複エラーが出る | プロジェクトのPlugins以下にCausticsBakerが1つだけ存在する状態にします。 |

## v1の制限

- UE 5.8、Win64、Editor専用
- 1 RegionにつきDirectional／Point／Spot Lightを1灯
- Static Mesh、ISM、HISMのみ
- Skeletal Mesh、Landscape、Geometry Collectionは対象外
- Volumetric caustics、色分散は対象外
- 投影方向に重なる複数Receiver層は、最前面1層のみ
- Receiver UVへの直接ベイクは対象外
- Runtime Decal／Materialの自動生成は対象外
- SubstrateはRay Tracing用に簡略化された最上位Surfaceのみ
- NaniteはUEがTLASへ提供するnativeまたはfallback geometryを使用するため、ラスタライズ形状との完全一致は保証しません

## ビルドとテスト

PluginパッケージはUE 5.8のAutomationToolで作成できます。

```powershell
& 'D:\Unreal\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' BuildPlugin `
  -Plugin='<absolute-path>\CausticsBaker.uplugin' `
  -Package='<output-directory>' `
  -TargetPlatforms=Win64 `
  -StrictIncludes
```

Automation Testsは`CausticsBaker.*`へ登録されています。GPUテストにはDX12／SM6／Hardware Ray Tracingが有効なEditorプロジェクトが必要です。

```powershell
& '<UE_5.8>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' '<Project>.uproject' `
  -unattended -nop4 -nosplash -NoSound -RenderOffscreen `
  '-ExecCmds=Automation RunTests CausticsBaker' `
  '-TestExit=Automation Test Queue Empty'
```

v1.2.4では、Win64 BuildPlugin、PCD3D_SM6 Global Shader Compile、HDR／8bit出力を含む8個の自動テストを確認しています。

## Issueを報告する場合

再現性のある調査のため、可能であれば次を添付してください。

- Unreal Engineの正確なバージョン
- GPUとドライバーバージョン
- Region、Light、Caster設定が分かるスクリーンショット
- Output Logのエラー全文
- 最小限の再現手順
