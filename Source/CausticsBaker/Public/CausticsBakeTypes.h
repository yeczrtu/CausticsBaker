#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "CausticsBakeTypes.generated.h"

class UPrimitiveComponent;

UENUM(BlueprintType)
enum class ECausticsOpticalMode : uint8
{
    AutoFromMaterial UMETA(DisplayName = "Auto from Material (Top Surface)", ToolTip = "Reads optical type, IOR/F0, roughness, tint, and normal from the ray-tracing payload's simplified top surface. Solid/Thin remains the explicit caster setting."),
    DielectricOverride UMETA(DisplayName = "Dielectric Override (Glass)", ToolTip = "Uses the explicit IOR, roughness, tint, absorption, and Solid/Thin settings below. Only the shading normal comes from the material."),
    ConductorOverride UMETA(DisplayName = "Conductor Override (Metal)", ToolTip = "Uses the explicit roughness and F0 tint below. Only the shading normal comes from the material.")
};

UENUM(BlueprintType)
enum class ECausticsThicknessMode : uint8
{
    Solid UMETA(DisplayName = "Solid (Closed Mesh)", ToolTip = "Trace both entry and exit interfaces and maintain a dielectric medium. Use for glass spheres and other watertight volumes."),
    Thin UMETA(DisplayName = "Thin (Single Surface)", ToolTip = "Treat the caster as a single optical sheet with the specified effective thickness. Do not use for a closed glass sphere.")
};

UENUM(BlueprintType)
enum class ECausticsQualityPreset : uint8
{
    Preview UMETA(DisplayName = "Preview (Fast)"),
    Bake UMETA(DisplayName = "Bake (High Quality)"),
    Custom UMETA(DisplayName = "Custom (Preview and Bake)")
};

UENUM(BlueprintType)
enum class ECausticsDenoiser : uint8
{
    Atrous UMETA(DisplayName = "GPU a-trous"),
    AtrousThenOIDN UMETA(DisplayName = "GPU a-trous + Intel OIDN (Bake only)"),
    None
};

UENUM(BlueprintType)
enum class ECausticsDebugDisplay : uint8
{
    Final,
    Raw,
    DensityFiltered,
    OIDNResult,
    GuideDepth,
    GuideNormal,
    GuideCoverage,
    GuideReceiverId
};

UENUM(BlueprintType)
enum class ECausticsProjectionMode : uint8
{
    DecalLike UMETA(DisplayName = "Decal-like (All Surfaces in Box)", ToolTip = "Projects the baked 2D caustics map onto every visible surface inside the box. Overlapping surfaces share the same projected texel, like an Unreal decal."),
    GuideMatched UMETA(DisplayName = "Guide-matched (Front Surface Only)", ToolTip = "Shows the result only on the receiver surface selected by the projection guide. Use this to inspect the physically evaluated front layer without decal-style reprojection.")
};

UENUM(BlueprintType)
enum class ECausticsOutputFormat : uint8
{
    HDR16F UMETA(DisplayName = "16-bit Float HDR (RGBA16F)", ToolTip = "Preserves the full linear HDR caustics irradiance. Uses RGBA16F, HDR compression, and linear sampling."),
    LDR8 UMETA(DisplayName = "8-bit LDR (BGRA8)", ToolTip = "Scales and clamps irradiance to 0-1, stores RGB as a normal sRGB BGRA8 texture, and preserves receiver coverage in alpha.")
};

UENUM(BlueprintType)
enum class ECausticsBakeJobState : uint8
{
    Idle,
    Validating,
    BuildingGuide,
    TracingPhotons,
    Filtering,
    Readback,
    Saving,
    Complete,
    Failed,
    Cancelled
};

USTRUCT(BlueprintType)
struct CAUSTICSBAKER_API FCausticsCasterEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Caster")
    FComponentReference Component;

    UPROPERTY(EditAnywhere, Category = "Optics")
    ECausticsOpticalMode OpticalMode = ECausticsOpticalMode::AutoFromMaterial;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ToolTip = "Solid traces a closed volume through entry and exit surfaces. Thin is for an open or single-surface sheet."))
    ECausticsThicknessMode ThicknessMode = ECausticsThicknessMode::Solid;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "1.0", ClampMax = "4.0", UIMin = "1.0", UIMax = "2.5", EditCondition = "OpticalMode == ECausticsOpticalMode::DielectricOverride", EditConditionHides, ToolTip = "Index of refraction used by Dielectric Override. Typical glass is approximately 1.5."))
    float IndexOfRefraction = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "0.001", ClampMax = "1.0", EditCondition = "OpticalMode != ECausticsOpticalMode::AutoFromMaterial", EditConditionHides, ToolTip = "Microfacet roughness used by explicit Glass/Metal modes. Use 0.001 for a sharp polished caustic."))
    float Roughness = 0.02f;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (DisplayName = "Optical Tint / F0", ToolTip = "Transmission tint for Glass or specular F0 color for Metal. In Auto mode this multiplies the material-derived tint and provides a reliable fallback when UE's simplified Substrate ray-tracing payload does not export transmission color."))
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "0.0", DisplayName = "Absorption (1/cm)"))
    FLinearColor Absorption = FLinearColor::Transparent;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "0.0", EditCondition = "ThicknessMode == ECausticsThicknessMode::Thin", EditConditionHides))
    float ThinThicknessCm = 0.5f;
};

USTRUCT(BlueprintType)
struct CAUSTICSBAKER_API FCausticsBakeSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ToolTip = "Preview uses the fast preset and Bake uses the selected preset. Select Custom to use the editable values for both Preview and Bake."))
    ECausticsQualityPreset Preset = ECausticsQualityPreset::Bake;

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ClampMin = "256", ClampMax = "4096", EditCondition = "Preset == ECausticsQualityPreset::Custom", EditConditionHides, ToolTip = "Power-of-two output resolution used by both Preview and Bake in Custom mode."))
    int32 Resolution = 2048;

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ClampMin = "1", ClampMax = "256", EditCondition = "Preset == ECausticsQualityPreset::Custom", EditConditionHides, ToolTip = "Number of independently traced photon batches. Total photons = Photon Batches x Photons Per Batch."))
    int32 PhotonBatches = 32;

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ClampMin = "1024", ClampMax = "4194304", EditCondition = "Preset == ECausticsQualityPreset::Custom", EditConditionHides, ToolTip = "Photons traced by each batch. Total photons = Photon Batches x Photons Per Batch."))
    int32 PhotonsPerBatch = 524288;

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "Preset == ECausticsQualityPreset::Custom", EditConditionHides))
    int32 MaxBounces = 8;

    UPROPERTY(EditAnywhere, Category = "Quality", meta = (ClampMin = "0", ClampMax = "8", EditCondition = "Preset == ECausticsQualityPreset::Custom", EditConditionHides))
    int32 AtrousIterations = 4;

    UPROPERTY(EditAnywhere, Category = "Quality")
    int32 RandomSeed = 1337;

    UPROPERTY(EditAnywhere, Category = "Density", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float SPPMConvergence = 0.7f;

    UPROPERTY(EditAnywhere, Category = "Density", meta = (ClampMin = "0.5", ClampMax = "32.0", DisplayName = "Initial Radius (texels)"))
    float InitialRadiusTexels = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Filtering", meta = (ClampMin = "0.0", ClampMax = "8.0"))
    float FilterStrength = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Filtering")
    ECausticsDenoiser Denoiser = ECausticsDenoiser::Atrous;

    UPROPERTY(EditAnywhere, Category = "Projection", meta = (DisplayName = "Viewport Projection", ToolTip = "Decal-like projects the baked map onto all visible surfaces in the box. Guide-matched restricts the viewport result to the evaluated front receiver layer."))
    ECausticsProjectionMode ProjectionMode = ECausticsProjectionMode::DecalLike;

    UPROPERTY(EditAnywhere, Category = "Debug")
    ECausticsDebugDisplay DebugDisplay = ECausticsDebugDisplay::Final;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (DisplayName = "Output Format", ToolTip = "Choose the source and compression format of the baked Texture2D. Preview always remains HDR."))
    ECausticsOutputFormat OutputFormat = ECausticsOutputFormat::HDR16F;

    UPROPERTY(EditAnywhere, Category = "Output", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0", EditCondition = "OutputFormat == ECausticsOutputFormat::LDR8", EditConditionHides, DisplayName = "8-bit White Level", ToolTip = "Linear HDR irradiance encoded as 255 in the 8-bit RGB output. RGB is divided by this value and clamped; alpha coverage is not rescaled."))
    float LDRWhiteLevel = 1.0f;

    void Resolve(bool bPreview, int32& OutResolution, int32& OutBatches, int32& OutPhotonsPerBatch,
        int32& OutMaxBounces, int32& OutAtrousIterations) const
    {
        if (Preset == ECausticsQualityPreset::Custom)
        {
            OutResolution = FMath::Clamp(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(Resolution)), 256u, 4096u);
            OutBatches = FMath::Clamp(PhotonBatches, 1, 256);
            OutPhotonsPerBatch = FMath::Clamp(PhotonsPerBatch, 1024, 4194304);
            OutMaxBounces = FMath::Clamp(MaxBounces, 1, 16);
            OutAtrousIterations = FMath::Clamp(AtrousIterations, 0, 8);
        }
        else if (bPreview || Preset == ECausticsQualityPreset::Preview)
        {
            OutResolution = 512;
            OutBatches = 8;
            OutPhotonsPerBatch = 131072;
            OutMaxBounces = 6;
            OutAtrousIterations = 2;
        }
        else if (Preset == ECausticsQualityPreset::Bake)
        {
            OutResolution = 2048;
            OutBatches = 32;
            OutPhotonsPerBatch = 524288;
            OutMaxBounces = 8;
            OutAtrousIterations = 4;
        }
    }
};

USTRUCT(BlueprintType)
struct CAUSTICSBAKER_API FCausticsJobStatus
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Caustics")
    ECausticsBakeJobState State = ECausticsBakeJobState::Idle;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Caustics")
    float Progress = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Caustics")
    FText Message;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Caustics")
    bool bCancelPending = false;
};

namespace CausticsBaker
{
    CAUSTICSBAKER_API bool IsSupportedPrimitiveComponent(const UPrimitiveComponent* Component);
    CAUSTICSBAKER_API FText JobStateToText(ECausticsBakeJobState State);
}
