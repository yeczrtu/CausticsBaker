#pragma once

#include "CoreMinimal.h"
#include "CausticsBakeTypes.h"
#include "PrimitiveComponentId.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "SceneViewExtension.h"

class FSceneInterface;
struct IPooledRenderTarget;

enum class ECausticsRenderLightType : uint32
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct FCausticsRenderCaster
{
    FPrimitiveComponentId PrimitiveComponentId;
    ECausticsOpticalMode OpticalMode = ECausticsOpticalMode::AutoFromMaterial;
    ECausticsThicknessMode ThicknessMode = ECausticsThicknessMode::Solid;
    bool bAutoTreatAsDielectric = false;
    float IOR = 1.5f;
    float Roughness = 0.02f;
    FVector3f Tint = FVector3f(1.0f);
    FVector3f Absorption = FVector3f::ZeroVector;
    float ThinThicknessCm = 0.5f;
};

struct FCausticsRenderRequest
{
    uint32 CaptureOwnerUniqueId = 0;
    uint32 RegionActorUniqueId = 0;
    FSceneInterface* SceneInterface = nullptr;
    bool bPreview = false;
    bool bUseOIDN = false;
    int32 Resolution = 512;
    int32 BatchCount = 8;
    int32 PhotonsPerBatch = 131072;
    int32 MaxBounces = 6;
    int32 GuideSamples = 1;
    int32 AtrousIterations = 2;
    uint32 RandomSeed = 1337;
    float SPPMAlpha = 0.7f;
    float InitialRadiusTexels = 3.0f;
    float ProjectionTexelWorldSize = 1.0f;
    FVector3f ProjectionDirectionWorld = FVector3f(1.0f, 0.0f, 0.0f);
    float FilterStrength = 1.0f;
    ECausticsDebugDisplay DebugDisplay = ECausticsDebugDisplay::Final;
    ECausticsProjectionMode ProjectionMode = ECausticsProjectionMode::DecalLike;

    FMatrix44f RegionToWorld = FMatrix44f::Identity;
    FMatrix44f WorldToRegion = FMatrix44f::Identity;
    FQuat4f WorldToRegionRotation = FQuat4f::Identity;
    FVector3f RegionSize = FVector3f(500.0f);

    ECausticsRenderLightType LightType = ECausticsRenderLightType::Directional;
    FVector3f LightPosition = FVector3f::ZeroVector;
    FVector3f LightDirection = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f LightColor = FVector3f(1.0f);
    float LightIntensity = 1.0f;
    float LightSourceRadius = 0.0f;
    float LightSourceAngleRadians = 0.0f;
    float LightAttenuationRadius = 1000.0f;
    float LightFalloffExponent = 8.0f;
    uint32 bInverseSquaredFalloff = 1u;
    float SpotInnerCos = 1.0f;
    float SpotOuterCos = 0.0f;
    FVector3f EmissionCenter = FVector3f::ZeroVector;
    FVector3f EmissionTangent = FVector3f(0.0f, 1.0f, 0.0f);
    FVector3f EmissionBitangent = FVector3f(0.0f, 0.0f, 1.0f);
    FVector2f EmissionHalfExtent = FVector2f(100.0f);
    FVector3f CasterBoundsCenter = FVector3f::ZeroVector;
    float CasterBoundsRadius = 100.0f;

    TArray<FCausticsRenderCaster> Casters;
    TArray<FPrimitiveComponentId> Receivers;
};

struct FCausticsGpuCaster
{
    uint32 PrimitiveId = MAX_uint32;
    uint32 OpticalMode = 0;
    uint32 ThicknessMode = 0;
    float IOR = 1.5f;
    FVector4f TintRoughness = FVector4f(1.0f, 1.0f, 1.0f, 0.02f);
    FVector4f AbsorptionThickness = FVector4f(0.0f, 0.0f, 0.0f, 0.5f);
};

class FCausticsRenderJob final : public TSharedFromThis<FCausticsRenderJob, ESPMode::ThreadSafe>
{
public:
    explicit FCausticsRenderJob(FCausticsRenderRequest&& InRequest);
    ~FCausticsRenderJob();

    FCausticsRenderRequest Request;
    TAtomic<ECausticsBakeJobState> Stage { ECausticsBakeJobState::BuildingGuide };
    TAtomic<int32> CompletedBatches { 0 };
    TAtomic<bool> bCancelRequested { false };
    TAtomic<bool> bReadbackPollQueued { false };
    TAtomic<bool> bCpuPixelsReady { false };
    TAtomic<bool> bResourcesReleased { false };

    TArray<FCausticsGpuCaster> GpuCasters;
    TArray<uint32> ReceiverPrimitiveIds;

    TRefCountPtr<IPooledRenderTarget> Guide;
    TRefCountPtr<IPooledRenderTarget> GuideId;
    TRefCountPtr<IPooledRenderTarget> GuideCoverage;
    TRefCountPtr<IPooledRenderTarget> PhotonRaw;
    TRefCountPtr<IPooledRenderTarget> SPPMTau;
    TRefCountPtr<IPooledRenderTarget> SPPMStats;
    TRefCountPtr<IPooledRenderTarget> DensityFiltered;
    TRefCountPtr<IPooledRenderTarget> FinalOutput;
    TUniquePtr<FRHIGPUTextureReadback> TextureReadback;
    TUniquePtr<FRHIGPUTextureReadback> GuideTextureReadback;

    void SetError(FString InError);
    FString GetError() const;
    void SetPixels(TArray<FFloat16Color>&& InPixels, TArray<FVector3f>&& InNormals);
    bool ConsumePixels(TArray<FFloat16Color>& OutPixels, TArray<FVector3f>& OutNormals);

private:
    mutable FCriticalSection DataGuard;
    FString Error;
    TArray<FFloat16Color> CpuPixels;
    TArray<FVector3f> CpuNormals;
};

class FCausticsBakerViewExtension final : public FSceneViewExtensionBase
{
public:
    FCausticsBakerViewExtension(const FAutoRegister& AutoRegister);
    virtual ESceneViewExtensionFlags GetFlags() const override { return ESceneViewExtensionFlags::SubscribesToPostTLASBuild; }
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
    virtual void PostTLASBuild_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
        const FPostProcessingInputs& Inputs) override;

    void SetRenderJob_RenderThread(TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> InJob);
    bool PromoteValidatedPreview_RenderThread(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job);
    void FinishRenderJob_RenderThread(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job);
    void CancelRenderJob_RenderThread(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job);
    void ClearPreview_RenderThread(uint32 RegionActorUniqueId);
    void Shutdown_RenderThread();
    bool HasRenderJob_RenderThread() const { return RenderJob.IsValid(); }

private:
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> RenderJob;
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> PreviewJob;
};

class FCausticsBakerRenderManager
{
public:
    void Initialize(TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> InViewExtension);
    void Shutdown();
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> Start(FCausticsRenderRequest&& Request);
    void Cancel();
    void ClearPreview(uint32 RegionActorUniqueId = 0);
    void PollReadback(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job);
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> GetActiveJob() const;
    void ForgetCompletedJob(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job);

private:
    mutable FCriticalSection Guard;
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> ActiveJob;
    TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> ViewExtension;
};

CAUSTICSBAKER_API FCausticsBakerRenderManager& GetCausticsBakerRenderManager();
void AddCausticsRayGenerationShaders(const class FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
