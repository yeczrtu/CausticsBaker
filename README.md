# CausticsBaker (Unreal Engine 5.8)

UE 5.8 Editor 上で、明示した Caster からフォトンを追跡し、投影ボックス内で自動検出した Receiver へ線形 HDR コースティクスをベイクする Win64 専用プラグインです。エンジン改造や Custom Primitive Data は使用しません。

## 必須環境

- Unreal Engine **5.8**（Renderer Private API を使用するため、他バージョンはコンパイル時に停止します）
- Windows / D3D12 / Shader Model 6
- `Support Hardware Ray Tracing` と full ray-tracing shader pipeline
- Caster と投影先メッシュの `Visible in Ray Tracing`

## 導入と使用

1. このディレクトリをプロジェクトの `Plugins/CausticsBaker` に配置し、Editor を再起動します。
2. Place Actors から `Caustics Bake Region` をレベルへ配置します。
3. Actor のローカル `+X` が投影方向、`Y` がテクスチャ U、`-Z` が V です。`Depth / Width / Height` で領域を設定します。
4. `Light Actor` で Directional / Point / Spot Light を1つ選び、Caster の StaticMesh / ISM / HISM component を指定します。
5. `Preview` でビューポート加算表示を確認し、`Bake` で `/Game/Caustics/T_Caustics_<ActorName>`（既定）へ RGBA16F Texture2D を作成します。

`Receiver Filter (Optional)` は空のままで構いません。空の場合は投影ボックスと交差する ray-tracing-visible な StaticMesh / ISM / HISM が自動的に Receiver になり、投影方向の最前面へデカールのように投影されます。特定メッシュだけへ限定したい場合に限り、Receiver Filterへコンポーネントを追加します。Casterは自動Receiverから除外されます。

明示した Receiver が現在の `Depth` より先にあっても、`Auto Fit Depth To Receiver Filter`（既定で有効）が `+X` 方向かつ `Width / Height` 内の Receiver まで Depth を自動拡張します。Preview は GPU readback で Receiver coverage を検証してから永続表示へ切り替わり、次の Preview、`Clear Preview`、またはレベル切替まで保持されます。

生成パッケージは Dirty になりますが、自動保存されません。RGB は Receiver の色を含まない線形 HDR 照度、Alpha は Receiver coverage です。

## 光学設定

- `Auto from Material (Top Surface)`: 非 Substrate は BaseColor / Metallic / Roughness / IOR / Normal を取得します。Substrate は RT 用に完全簡略化された最上位 Surface BSDF の Normal / Roughness / F0 を使用し、下層は無視します。F0 から金属または誘電体を近似判定します。
- `Dielectric Override`: IOR、roughness、tint、Beer–Lambert absorption、Solid / Thin を明示します。
- `Conductor Override`: roughness と反射 F0 色を明示します。
- 複雑なSubstrate積層をそのまま追跡したい場合や自動判定が合わない場合だけ、DielectricまたはConductor OverrideでIOR／反射色を明示します。Hair、Eye、ToonなどSlab／Single Layer Water以外の最上位BSDFはAuto対象外です。

## パイプライン

1. 64×64 の一時 SceneCapture（同一 Editor World）だけ GI=`Plugin`、ray-traced translucency にします。
2. material RT pipeline で投影 Guide（depth / shading normal / Persistent Primitive ID / coverage）を作成します。
3. 1 capture = 1 photon batch として GGX / Fresnel / Snell / TIR / absorption / medium stack / Russian roulette を処理します。
4. 固定長 photon records を integer atomic count、prefix scan、8×8 tile bin、scatter で整理します。
5. Receiver ID、world distance、normal を条件に cone kernel SPPM を更新します。
6. variance / depth / normal / Receiver ID / coverage を使う GPU à-trous を適用します。
7. Bake のみ、任意で UE 同梱 Intel OIDN（白 Albedo、投影 Guide Normal、Alpha 除外）を追加できます。
8. `FRHIGPUTextureReadback` 完了後に Texture asset を Game Thread で更新します。

Preview は Scene Depth から world position を再構築し、Guide depth と一致する面だけへ Pre-PostProcess で加算します。Decal Actor や runtime material asset は生成しません。

## ビルド確認

```powershell
& 'D:\Unreal\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' BuildPlugin `
  -Plugin='<absolute-path>\CausticsBaker.uplugin' `
  -Package='<output-directory>' `
  -TargetPlatforms=Win64 -StrictIncludes
```

Automation tests は `CausticsBaker.*` に登録されます。

## v1 制限

Skeletal Mesh、Landscape、Geometry Collection、volumetric caustics、色分散、複数ライト、Receiver UV bake、投影方向に重なる複数受光層、runtime decal は対象外です。Nanite は UE が TLAS に提供する native / fallback geometry に従います。
