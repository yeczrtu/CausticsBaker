#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "CausticsBakeTypes.generated.h"

class UPrimitiveComponent;

UENUM(BlueprintType)
enum class ECausticsOpticalMode : uint8
{
    AutoFromMaterial UMETA(DisplayName = "Auto from Material (Top Surface)", ToolTip = "Uses the material hit payload. With Substrate, only the ray-tracing payload's simplified top surface is used; lower layers are ignored."),
    DielectricOverride UMETA(DisplayName = "Dielectric Override"),
    ConductorOverride UMETA(DisplayName = "Conductor Override")
};

UENUM(BlueprintType)
enum class ECausticsThicknessMode : uint8
{
    Solid,
    Thin
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

    UPROPERTY(EditAnywhere, Category = "Optics")
    ECausticsThicknessMode ThicknessMode = ECausticsThicknessMode::Solid;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "1.0", ClampMax = "4.0", UIMin = "1.0", UIMax = "2.5"))
    float IndexOfRefraction = 1.5f;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (ClampMin = "0.001", ClampMax = "1.0"))
    float Roughness = 0.02f;

    UPROPERTY(EditAnywhere, Category = "Optics", meta = (DisplayName = "Optical Tint / F0"))
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

    UPROPERTY(EditAnywhere, Category = "Debug")
    ECausticsDebugDisplay DebugDisplay = ECausticsDebugDisplay::Final;

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
