#include "CausticsBakerRenderer.h"

#include "CausticsBakerMath.h"

#include "DeferredShadingRenderer.h"
#include "GlobalShader.h"
#include "Math/Float16Color.h"
#include "Nanite/NaniteRayTracing.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "RayTracingShaderBindingLayout.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"

#if RHI_RAYTRACING

class FCausticsGuideRG final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsGuideRG)
    SHADER_USE_ROOT_PARAMETER_STRUCT(FCausticsGuideRG, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Guide)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, GuideId)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, GuideCoverage)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ReceiverIds)
        SHADER_PARAMETER(uint32, ReceiverCount)
        SHADER_PARAMETER(FMatrix44f, RegionToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToRegion)
        SHADER_PARAMETER(FVector3f, RegionSize)
        SHADER_PARAMETER(FVector3f, PreViewTranslation)
        SHADER_PARAMETER(uint32, OutputResolution)
        SHADER_PARAMETER(uint32, GuideSampleCount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRayTracingUniformParameters, NaniteRayTracing)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
    }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("RAY_TRACING_PAYLOAD_TYPE"), 0);
    }
    static ERayTracingPayloadType GetRayTracingPayloadType(int32) { return ERayTracingPayloadType::RayTracingMaterial; }
    static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
    {
        return RayTracing::GetShaderBindingLayout(Parameters.Platform);
    }
};
IMPLEMENT_GLOBAL_SHADER(FCausticsGuideRG, "/Plugin/CausticsBaker/CausticsRayTracing.usf", "CausticsGuideRG", SF_RayGen);

class FCausticsPhotonRG final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsPhotonRG)
    SHADER_USE_ROOT_PARAMETER_STRUCT(FCausticsPhotonRG, FGlobalShader)

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FPhotonRecord>, PhotonRecords)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ReceiverIds)
        SHADER_PARAMETER(uint32, ReceiverCount)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FCausticsGpuCaster>, CasterConfigs)
        SHADER_PARAMETER(uint32, CasterCount)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Guide)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, GuideId)
        SHADER_PARAMETER(FMatrix44f, RegionToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToRegion)
        SHADER_PARAMETER(FVector3f, RegionSize)
        SHADER_PARAMETER(float, ProjectionTexelWorldSize)
        SHADER_PARAMETER(FVector3f, PreViewTranslation)
        SHADER_PARAMETER(uint32, OutputResolution)
        SHADER_PARAMETER(uint32, PhotonCount)
        SHADER_PARAMETER(uint32, PhotonNormalizationCount)
        SHADER_PARAMETER(uint32, bUseDispersion)
        SHADER_PARAMETER(uint32, BatchIndex)
        SHADER_PARAMETER(uint32, RandomSeed)
        SHADER_PARAMETER(uint32, MaxBounces)
        SHADER_PARAMETER(uint32, BinGridWidth)
        SHADER_PARAMETER(uint32, BinGridHeight)
        SHADER_PARAMETER(uint32, LightType)
        SHADER_PARAMETER(FVector3f, LightPosition)
        SHADER_PARAMETER(FVector3f, LightDirection)
        SHADER_PARAMETER(FVector3f, LightColor)
        SHADER_PARAMETER(float, LightIntensity)
        SHADER_PARAMETER(float, LightSourceRadius)
        SHADER_PARAMETER(float, LightSourceAngleRadians)
        SHADER_PARAMETER(float, LightAttenuationRadius)
        SHADER_PARAMETER(float, LightFalloffExponent)
        SHADER_PARAMETER(uint32, bInverseSquaredFalloff)
        SHADER_PARAMETER(float, SpotInnerCos)
        SHADER_PARAMETER(float, SpotOuterCos)
        SHADER_PARAMETER(FVector3f, EmissionCenter)
        SHADER_PARAMETER(FVector3f, EmissionTangent)
        SHADER_PARAMETER(FVector3f, EmissionBitangent)
        SHADER_PARAMETER(FVector2f, EmissionHalfExtent)
        SHADER_PARAMETER(FVector3f, CasterBoundsCenter)
        SHADER_PARAMETER(float, CasterBoundsRadius)
        SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRayTracingUniformParameters, NaniteRayTracing)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
    }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("RAY_TRACING_PAYLOAD_TYPE"), 0);
    }
    static ERayTracingPayloadType GetRayTracingPayloadType(int32) { return ERayTracingPayloadType::RayTracingMaterial; }
    static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
    {
        return RayTracing::GetShaderBindingLayout(Parameters.Platform);
    }
};
IMPLEMENT_GLOBAL_SHADER(FCausticsPhotonRG, "/Plugin/CausticsBaker/CausticsRayTracing.usf", "CausticsPhotonRG", SF_RayGen);

#endif

class FCausticsCountPhotonsCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsCountPhotonsCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsCountPhotonsCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhotonRecord>, PhotonRecords)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWBinCounts)
        SHADER_PARAMETER(uint32, PhotonCount)
        SHADER_PARAMETER(uint32, BinCount)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsCountPhotonsCS, "/Plugin/CausticsBaker/CausticsDensity.usf", "CountPhotonsCS", SF_Compute);

class FCausticsPrefixScanCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsPrefixScanCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsPrefixScanCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BinCounts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWBinOffsets)
        SHADER_PARAMETER(uint32, BinCount)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsPrefixScanCS, "/Plugin/CausticsBaker/CausticsDensity.usf", "PrefixScanCS", SF_Compute);

class FCausticsScatterPhotonsCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsScatterPhotonsCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsScatterPhotonsCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhotonRecord>, PhotonRecords)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BinOffsets)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BinCursors)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWPhotonIndices)
        SHADER_PARAMETER(uint32, PhotonCount)
        SHADER_PARAMETER(uint32, BinCount)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsScatterPhotonsCS, "/Plugin/CausticsBaker/CausticsDensity.usf", "ScatterPhotonsCS", SF_Compute);

class FCausticsDensityEstimateCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsDensityEstimateCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsDensityEstimateCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPhotonRecord>, PhotonRecords)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BinCounts)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BinOffsets)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PhotonIndices)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Guide)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GuideId)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideCoverage)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, PhotonRaw)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SPPMTau)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SPPMStats)
        SHADER_PARAMETER(uint32, BinGridWidth)
        SHADER_PARAMETER(uint32, BinGridHeight)
        SHADER_PARAMETER(uint32, OutputResolution)
        SHADER_PARAMETER(uint32, BatchIndex)
        SHADER_PARAMETER(float, SPPMAlpha)
        SHADER_PARAMETER(float, InitialRadiusWorld)
        SHADER_PARAMETER(float, ProjectionTexelWorldSize)
        SHADER_PARAMETER(FVector3f, ProjectionDirectionWorld)
        SHADER_PARAMETER(FMatrix44f, RegionToWorld)
        SHADER_PARAMETER(FVector3f, RegionSize)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsDensityEstimateCS, "/Plugin/CausticsBaker/CausticsDensity.usf", "DensityEstimateCS", SF_Compute);

class FCausticsFinalizeDensityCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsFinalizeDensityCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsFinalizeDensityCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GuideId)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideCoverage)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SPPMTau)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SPPMStats)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DensityOutput)
        SHADER_PARAMETER(uint32, OutputResolution)
        SHADER_PARAMETER(uint32, BatchCount)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsFinalizeDensityCS, "/Plugin/CausticsBaker/CausticsDensity.usf", "FinalizeDensityCS", SF_Compute);

class FCausticsAtrousCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsAtrousCS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsAtrousCS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Guide)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GuideId)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideCoverage)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SPPMStats)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        SHADER_PARAMETER(uint32, OutputResolution)
        SHADER_PARAMETER(uint32, StepWidth)
        SHADER_PARAMETER(uint32, BatchCount)
        SHADER_PARAMETER(float, FilterStrength)
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsAtrousCS, "/Plugin/CausticsBaker/CausticsDenoise.usf", "AtrousCS", SF_Compute);

class FCausticsPreviewPS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FCausticsPreviewPS)
    SHADER_USE_PARAMETER_STRUCT(FCausticsPreviewPS, FGlobalShader)
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CausticsTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Guide)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, GuideId)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideCoverage)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampSampler)
        SHADER_PARAMETER(FMatrix44f, WorldToRegion)
        SHADER_PARAMETER(FMatrix44f, RegionToWorld)
        SHADER_PARAMETER(FVector3f, RegionSize)
        SHADER_PARAMETER(float, ProjectionTexelWorldSize)
        SHADER_PARAMETER(uint32, BakeResolution)
        SHADER_PARAMETER(uint32, DebugDisplay)
        SHADER_PARAMETER(uint32, ProjectionMode)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FCausticsPreviewPS, "/Plugin/CausticsBaker/CausticsPreview.usf", "MainPS", SF_Pixel);

namespace
{
    struct FPhotonRecordStride
    {
        FVector3f Position;
        uint32 ReceiverId;
        FVector3f Power;
        uint32 BinIndex;
        uint32 PackedNormal;
    };

    static_assert(sizeof(FPhotonRecordStride) == 36, "FPhotonRecord must match the HLSL structured-buffer stride.");
    static_assert(sizeof(FCausticsGpuCaster) == 64, "FCausticsGpuCaster must match the HLSL structured-buffer stride.");

    TAtomic<uint32> GActiveCaptureOwnerUniqueId { 0 };

    void ReleaseResources_RenderThread(FCausticsRenderJob& Job)
    {
        Job.Guide.SafeRelease();
        Job.GuideId.SafeRelease();
        Job.GuideCoverage.SafeRelease();
        Job.PhotonRaw.SafeRelease();
        Job.SPPMTau.SafeRelease();
        Job.SPPMStats.SafeRelease();
        Job.DensityFiltered.SafeRelease();
        Job.FinalOutput.SafeRelease();
        Job.TextureReadback.Reset();
        Job.GuideTextureReadback.Reset();
        Job.bResourcesReleased.Store(true);
    }

    void AllocateTexture(FRDGBuilder& GraphBuilder, TRefCountPtr<IPooledRenderTarget>& Target, EPixelFormat Format,
        FIntPoint Extent, const TCHAR* Name)
    {
        if (Target.IsValid()) return;
        const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
            Extent, Format, FClearValueBinding::Black, TexCreate_None,
            TexCreate_ShaderResource | TexCreate_UAV, false);
        GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, Target, Name);
    }

    bool ResolvePrimitiveIds_RenderThread(FCausticsRenderJob& Job)
    {
        Job.GpuCasters.Reset(Job.Request.Casters.Num());
        Job.ReceiverPrimitiveIds.Reset(Job.Request.Receivers.Num());
        for (const FCausticsRenderCaster& Source : Job.Request.Casters)
        {
            const FPrimitiveSceneInfo* SceneInfo = Job.Request.SceneInterface && Source.PrimitiveComponentId.IsValid()
                ? Job.Request.SceneInterface->GetPrimitiveSceneInfo(Source.PrimitiveComponentId) : nullptr;
            if (!SceneInfo)
            {
                Job.SetError(FString::Printf(TEXT("Caster component ID %u was not present in the render scene while the job was starting."),
                    Source.PrimitiveComponentId.PrimIDValue));
                return false;
            }
            const FPersistentPrimitiveIndex PersistentIndex = SceneInfo->GetPersistentIndex();
            if (!PersistentIndex.IsValid())
            {
                Job.SetError(FString::Printf(TEXT("Caster component ID %u did not have a valid Persistent Primitive ID after TLAS construction."),
                    Source.PrimitiveComponentId.PrimIDValue));
                return false;
            }
            FCausticsGpuCaster Dest;
            Dest.PrimitiveId = PersistentIndex.Index;
            Dest.OpticalMode = static_cast<uint32>(Source.OpticalMode);
            // The high bit is an internal Auto-mode hint; the low bits retain
            // the public ECausticsThicknessMode value without changing the GPU
            // structured-buffer layout.
            Dest.ThicknessMode = static_cast<uint32>(Source.ThicknessMode) |
                (Source.bAutoTreatAsDielectric ? 0x80000000u : 0u);
            Dest.IOR = Source.IOR;
            Dest.TintRoughness = FVector4f(Source.Tint.X, Source.Tint.Y, Source.Tint.Z, Source.Roughness);
            Dest.AbsorptionThickness = FVector4f(Source.Absorption.X, Source.Absorption.Y, Source.Absorption.Z, Source.ThinThicknessCm);
            Dest.Dispersion = FVector4f(Source.bEnableDispersion ? 1.0f / FMath::Clamp(Source.AbbeNumber, 5.0f, 200.0f) : 0.0f,
                0.0f, 0.0f, 0.0f);
            Job.GpuCasters.Add(Dest);
        }
        for (const FPrimitiveComponentId ComponentId : Job.Request.Receivers)
        {
            const FPrimitiveSceneInfo* SceneInfo = Job.Request.SceneInterface && ComponentId.IsValid()
                ? Job.Request.SceneInterface->GetPrimitiveSceneInfo(ComponentId) : nullptr;
            if (!SceneInfo)
            {
                Job.SetError(FString::Printf(TEXT("Receiver component ID %u was not present in the render scene while the job was starting."),
                    ComponentId.PrimIDValue));
                return false;
            }
            const FPersistentPrimitiveIndex PersistentIndex = SceneInfo->GetPersistentIndex();
            if (!PersistentIndex.IsValid())
            {
                Job.SetError(FString::Printf(TEXT("Receiver component ID %u did not have a valid Persistent Primitive ID after TLAS construction."),
                    ComponentId.PrimIDValue));
                return false;
            }
            Job.ReceiverPrimitiveIds.Add(PersistentIndex.Index);
        }
        Job.GpuCasters.Sort([](const FCausticsGpuCaster& A, const FCausticsGpuCaster& B) { return A.PrimitiveId < B.PrimitiveId; });
        Job.ReceiverPrimitiveIds.Sort();
        for (const FCausticsGpuCaster& Caster : Job.GpuCasters)
        {
            if (Job.ReceiverPrimitiveIds.Contains(Caster.PrimitiveId))
            {
                Job.SetError(FString::Printf(TEXT("Persistent Primitive ID %u resolved as both a caster and receiver."),
                    Caster.PrimitiveId));
                return false;
            }
        }
        return true;
    }

#if RHI_RAYTRACING
    template<typename TShaderClass>
    void AddMaterialRayDispatch(FRDGBuilder& GraphBuilder, const FViewInfo& View,
        typename TShaderClass::FParameters* Parameters, FIntPoint DispatchSize, const TCHAR* EventName)
    {
        TShaderMapRef<TShaderClass> RayGenerationShader(View.ShaderMap);
        GraphBuilder.AddPass(
            RDG_EVENT_NAME("%s", EventName), Parameters, ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
            [Parameters, RayGenerationShader, DispatchSize, &View](FRHICommandList& RHICmdList)
            {
                if (!View.MaterialRayTracingData.PipelineState || !View.MaterialRayTracingData.ShaderBindingTable) return;
                FRHIUniformBuffer* SceneUniformBuffer = Parameters->Scene->GetRHI();
                FRHIUniformBuffer* NaniteUniformBuffer = Parameters->NaniteRayTracing->GetRHI();
                TOptional<FScopedUniformBufferStaticBindings> StaticBindings =
                    RayTracing::BindStaticUniformBufferBindings(View, SceneUniformBuffer, NaniteUniformBuffer, RHICmdList);
                FRHIBatchedShaderParameters& GlobalResources = RHICmdList.GetScratchShaderParameters();
                SetShaderParameters(GlobalResources, RayGenerationShader, *Parameters);
                RHICmdList.RayTraceDispatch(View.MaterialRayTracingData.PipelineState,
                    RayGenerationShader.GetRayTracingShader(), View.MaterialRayTracingData.ShaderBindingTable,
                    GlobalResources, DispatchSize.X, DispatchSize.Y);
            });
    }
#endif

    void BuildGuide_RenderThread(FRDGBuilder& GraphBuilder, const FViewInfo& View, FCausticsRenderJob& Job)
    {
#if RHI_RAYTRACING
        if (!ResolvePrimitiveIds_RenderThread(Job))
        {
            Job.Stage.Store(ECausticsBakeJobState::Failed);
            return;
        }
        const FIntPoint Extent(Job.Request.Resolution, Job.Request.Resolution);
        AllocateTexture(GraphBuilder, Job.Guide, PF_A32B32G32R32F, Extent, TEXT("Caustics.Guide"));
        AllocateTexture(GraphBuilder, Job.GuideId, PF_R32_UINT, Extent, TEXT("Caustics.GuideId"));
        AllocateTexture(GraphBuilder, Job.GuideCoverage, PF_R16F, Extent, TEXT("Caustics.GuideCoverage"));
        AllocateTexture(GraphBuilder, Job.PhotonRaw, PF_FloatRGBA, Extent, TEXT("Caustics.Raw"));
        AllocateTexture(GraphBuilder, Job.SPPMTau, PF_FloatRGBA, Extent, TEXT("Caustics.SPPMTau"));
        AllocateTexture(GraphBuilder, Job.SPPMStats, PF_A32B32G32R32F, Extent, TEXT("Caustics.SPPMStats"));
        AllocateTexture(GraphBuilder, Job.DensityFiltered, PF_FloatRGBA, Extent, TEXT("Caustics.DensityFiltered"));
        AllocateTexture(GraphBuilder, Job.FinalOutput, PF_FloatRGBA, Extent, TEXT("Caustics.Final"));

        FRDGTextureRef Guide = GraphBuilder.RegisterExternalTexture(Job.Guide);
        FRDGTextureRef GuideId = GraphBuilder.RegisterExternalTexture(Job.GuideId);
        FRDGTextureRef GuideCoverage = GraphBuilder.RegisterExternalTexture(Job.GuideCoverage);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Guide), FVector4f::Zero());
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GuideId), MAX_uint32);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GuideCoverage), 0.0f);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(Job.PhotonRaw)), FVector4f::Zero());
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(Job.SPPMTau)), FVector4f::Zero());
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalTexture(Job.SPPMStats)), FVector4f::Zero());

        FRDGBufferRef ReceiverBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Caustics.ReceiverIds"), Job.ReceiverPrimitiveIds);
        FCausticsGuideRG::FParameters* Parameters = GraphBuilder.AllocParameters<FCausticsGuideRG::FParameters>();
        Parameters->Guide = GraphBuilder.CreateUAV(Guide);
        Parameters->GuideId = GraphBuilder.CreateUAV(GuideId);
        Parameters->GuideCoverage = GraphBuilder.CreateUAV(GuideCoverage);
        Parameters->ReceiverIds = GraphBuilder.CreateSRV(ReceiverBuffer);
        Parameters->ReceiverCount = Job.ReceiverPrimitiveIds.Num();
        Parameters->RegionToWorld = Job.Request.RegionToWorld;
        Parameters->WorldToRegion = Job.Request.WorldToRegion;
        Parameters->RegionSize = Job.Request.RegionSize;
        Parameters->PreViewTranslation = FVector3f(View.ViewMatrices.GetPreViewTranslation());
        Parameters->OutputResolution = Job.Request.Resolution;
        Parameters->GuideSampleCount = Job.Request.GuideSamples;
        Parameters->TLAS = View.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
        Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
        Parameters->Scene = GetSceneUniformBufferRef(GraphBuilder, View);
        Parameters->NaniteRayTracing = Nanite::GetPublicGlobalRayTracingUniformBuffer();
        AddMaterialRayDispatch<FCausticsGuideRG>(GraphBuilder, View, Parameters, Extent, TEXT("Caustics Guide"));
        Job.Stage.Store(ECausticsBakeJobState::TracingPhotons);
#else
        Job.SetError(TEXT("This engine build has no full-pipeline ray tracing support."));
        Job.Stage.Store(ECausticsBakeJobState::Failed);
#endif
    }

    void TracePhotonBatch_RenderThread(FRDGBuilder& GraphBuilder, const FViewInfo& View, FCausticsRenderJob& Job)
    {
#if RHI_RAYTRACING
        const uint32 BatchIndex = Job.CompletedBatches.Load();
        const uint32 Resolution = Job.Request.Resolution;
        const uint32 PhotonNormalizationCount = CausticsBaker::Math::PhotonNormalizationCountPerBatch(
            Job.Request.PhotonsPerBatch);
        const uint32 PhotonCount = CausticsBaker::Math::PhotonRecordCountPerBatch(
            PhotonNormalizationCount, Job.Request.bUseDispersion);
        const uint32 BinGridWidth = FMath::DivideAndRoundUp(Resolution, 8u);
        const uint32 BinGridHeight = FMath::DivideAndRoundUp(Resolution, 8u);
        const uint32 BinCount = BinGridWidth * BinGridHeight;

        FRDGTextureRef Guide = GraphBuilder.RegisterExternalTexture(Job.Guide);
        FRDGTextureRef GuideId = GraphBuilder.RegisterExternalTexture(Job.GuideId);
        FRDGTextureRef GuideCoverage = GraphBuilder.RegisterExternalTexture(Job.GuideCoverage);
        FRDGTextureRef PhotonRaw = GraphBuilder.RegisterExternalTexture(Job.PhotonRaw);
        FRDGTextureRef Tau = GraphBuilder.RegisterExternalTexture(Job.SPPMTau);
        FRDGTextureRef Stats = GraphBuilder.RegisterExternalTexture(Job.SPPMStats);
        FRDGBufferRef ReceiverBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Caustics.ReceiverIds"), Job.ReceiverPrimitiveIds);
        FRDGBufferRef CasterBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Caustics.CasterConfigs"), Job.GpuCasters);
        FRDGBufferRef PhotonBuffer = GraphBuilder.CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(sizeof(FPhotonRecordStride), PhotonCount), TEXT("Caustics.PhotonRecords"));

        FCausticsPhotonRG::FParameters* RayParameters = GraphBuilder.AllocParameters<FCausticsPhotonRG::FParameters>();
        RayParameters->PhotonRecords = GraphBuilder.CreateUAV(PhotonBuffer);
        RayParameters->ReceiverIds = GraphBuilder.CreateSRV(ReceiverBuffer);
        RayParameters->ReceiverCount = Job.ReceiverPrimitiveIds.Num();
        RayParameters->CasterConfigs = GraphBuilder.CreateSRV(CasterBuffer);
        RayParameters->CasterCount = Job.GpuCasters.Num();
        RayParameters->Guide = GraphBuilder.CreateUAV(Guide);
        RayParameters->GuideId = GraphBuilder.CreateUAV(GuideId);
        RayParameters->RegionToWorld = Job.Request.RegionToWorld;
        RayParameters->WorldToRegion = Job.Request.WorldToRegion;
        RayParameters->RegionSize = Job.Request.RegionSize;
        RayParameters->ProjectionTexelWorldSize = Job.Request.ProjectionTexelWorldSize;
        RayParameters->PreViewTranslation = FVector3f(View.ViewMatrices.GetPreViewTranslation());
        RayParameters->OutputResolution = Resolution;
        RayParameters->PhotonCount = PhotonCount;
        RayParameters->PhotonNormalizationCount = PhotonNormalizationCount;
        RayParameters->bUseDispersion = Job.Request.bUseDispersion ? 1u : 0u;
        RayParameters->BatchIndex = BatchIndex;
        RayParameters->RandomSeed = Job.Request.RandomSeed;
        RayParameters->MaxBounces = Job.Request.MaxBounces;
        RayParameters->BinGridWidth = BinGridWidth;
        RayParameters->BinGridHeight = BinGridHeight;
        RayParameters->LightType = static_cast<uint32>(Job.Request.LightType);
        RayParameters->LightPosition = Job.Request.LightPosition;
        RayParameters->LightDirection = Job.Request.LightDirection;
        RayParameters->LightColor = Job.Request.LightColor;
        RayParameters->LightIntensity = Job.Request.LightIntensity;
        RayParameters->LightSourceRadius = Job.Request.LightSourceRadius;
        RayParameters->LightSourceAngleRadians = Job.Request.LightSourceAngleRadians;
        RayParameters->LightAttenuationRadius = Job.Request.LightAttenuationRadius;
        RayParameters->LightFalloffExponent = Job.Request.LightFalloffExponent;
        RayParameters->bInverseSquaredFalloff = Job.Request.bInverseSquaredFalloff;
        RayParameters->SpotInnerCos = Job.Request.SpotInnerCos;
        RayParameters->SpotOuterCos = Job.Request.SpotOuterCos;
        RayParameters->EmissionCenter = Job.Request.EmissionCenter;
        RayParameters->EmissionTangent = Job.Request.EmissionTangent;
        RayParameters->EmissionBitangent = Job.Request.EmissionBitangent;
        RayParameters->EmissionHalfExtent = Job.Request.EmissionHalfExtent;
        RayParameters->CasterBoundsCenter = Job.Request.CasterBoundsCenter;
        RayParameters->CasterBoundsRadius = Job.Request.CasterBoundsRadius;
        RayParameters->TLAS = View.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
        RayParameters->ViewUniformBuffer = View.ViewUniformBuffer;
        RayParameters->Scene = GetSceneUniformBufferRef(GraphBuilder, View);
        RayParameters->NaniteRayTracing = Nanite::GetPublicGlobalRayTracingUniformBuffer();
        AddMaterialRayDispatch<FCausticsPhotonRG>(GraphBuilder, View, RayParameters,
            FIntPoint(PhotonCount, 1), TEXT("Caustics Photon Trace"));

        FRDGBufferRef BinCounts = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BinCount), TEXT("Caustics.BinCounts"));
        FRDGBufferRef BinOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BinCount), TEXT("Caustics.BinOffsets"));
        FRDGBufferRef BinCursors = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), BinCount), TEXT("Caustics.BinCursors"));
        FRDGBufferRef PhotonIndices = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), PhotonCount), TEXT("Caustics.PhotonIndices"));
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BinCounts), 0u);
        AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BinCursors), 0u);

        TShaderMapRef<FCausticsCountPhotonsCS> CountShader(View.ShaderMap);
        auto* Count = GraphBuilder.AllocParameters<FCausticsCountPhotonsCS::FParameters>();
        Count->PhotonRecords = GraphBuilder.CreateSRV(PhotonBuffer);
        Count->RWBinCounts = GraphBuilder.CreateUAV(BinCounts);
        Count->PhotonCount = PhotonCount;
        Count->BinCount = BinCount;
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics Bin Count"), CountShader, Count,
            FIntVector(FMath::DivideAndRoundUp(PhotonCount, 256u), 1, 1));

        TShaderMapRef<FCausticsPrefixScanCS> ScanShader(View.ShaderMap);
        auto* Scan = GraphBuilder.AllocParameters<FCausticsPrefixScanCS::FParameters>();
        Scan->BinCounts = GraphBuilder.CreateSRV(BinCounts);
        Scan->RWBinOffsets = GraphBuilder.CreateUAV(BinOffsets);
        Scan->BinCount = BinCount;
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics Prefix Scan"), ScanShader, Scan, FIntVector(1, 1, 1));

        TShaderMapRef<FCausticsScatterPhotonsCS> ScatterShader(View.ShaderMap);
        auto* Scatter = GraphBuilder.AllocParameters<FCausticsScatterPhotonsCS::FParameters>();
        Scatter->PhotonRecords = GraphBuilder.CreateSRV(PhotonBuffer);
        Scatter->BinOffsets = GraphBuilder.CreateSRV(BinOffsets);
        Scatter->BinCursors = GraphBuilder.CreateUAV(BinCursors);
        Scatter->RWPhotonIndices = GraphBuilder.CreateUAV(PhotonIndices);
        Scatter->PhotonCount = PhotonCount;
        Scatter->BinCount = BinCount;
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics Photon Scatter"), ScatterShader, Scatter,
            FIntVector(FMath::DivideAndRoundUp(PhotonCount, 256u), 1, 1));

        TShaderMapRef<FCausticsDensityEstimateCS> DensityShader(View.ShaderMap);
        auto* Density = GraphBuilder.AllocParameters<FCausticsDensityEstimateCS::FParameters>();
        Density->PhotonRecords = GraphBuilder.CreateSRV(PhotonBuffer);
        Density->BinCounts = GraphBuilder.CreateSRV(BinCounts);
        Density->BinOffsets = GraphBuilder.CreateSRV(BinOffsets);
        Density->PhotonIndices = GraphBuilder.CreateSRV(PhotonIndices);
        Density->Guide = Guide;
        Density->GuideId = GuideId;
        Density->GuideCoverage = GuideCoverage;
        Density->PhotonRaw = GraphBuilder.CreateUAV(PhotonRaw);
        Density->SPPMTau = GraphBuilder.CreateUAV(Tau);
        Density->SPPMStats = GraphBuilder.CreateUAV(Stats);
        Density->BinGridWidth = BinGridWidth;
        Density->BinGridHeight = BinGridHeight;
        Density->OutputResolution = Resolution;
        Density->BatchIndex = BatchIndex;
        Density->SPPMAlpha = Job.Request.SPPMAlpha;
        Density->InitialRadiusWorld = Job.Request.InitialRadiusTexels * Job.Request.ProjectionTexelWorldSize;
        Density->ProjectionTexelWorldSize = Job.Request.ProjectionTexelWorldSize;
        Density->ProjectionDirectionWorld = Job.Request.ProjectionDirectionWorld;
        Density->RegionToWorld = Job.Request.RegionToWorld;
        Density->RegionSize = Job.Request.RegionSize;
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics SPPM Density"), DensityShader, Density,
            FComputeShaderUtils::GetGroupCount(FIntPoint(Resolution), FIntPoint(8)));

        const int32 NewCompleted = BatchIndex + 1;
        Job.CompletedBatches.Store(NewCompleted);
        if (NewCompleted >= Job.Request.BatchCount)
        {
            Job.Stage.Store(ECausticsBakeJobState::Filtering);
        }
#endif
    }

    void FilterAndReadback_RenderThread(FRDGBuilder& GraphBuilder, const FViewInfo& View, FCausticsRenderJob& Job)
    {
        const int32 Resolution = Job.Request.Resolution;
        const FIntPoint Extent(Resolution);
        FRDGTextureRef Guide = GraphBuilder.RegisterExternalTexture(Job.Guide);
        FRDGTextureRef GuideId = GraphBuilder.RegisterExternalTexture(Job.GuideId);
        FRDGTextureRef GuideCoverage = GraphBuilder.RegisterExternalTexture(Job.GuideCoverage);
        FRDGTextureRef Tau = GraphBuilder.RegisterExternalTexture(Job.SPPMTau);
        FRDGTextureRef Stats = GraphBuilder.RegisterExternalTexture(Job.SPPMStats);
        FRDGTextureRef DensityTexture = GraphBuilder.RegisterExternalTexture(Job.DensityFiltered);
        FRDGTextureRef FinalTexture = GraphBuilder.RegisterExternalTexture(Job.FinalOutput);

        TShaderMapRef<FCausticsFinalizeDensityCS> FinalizeShader(View.ShaderMap);
        auto* Finalize = GraphBuilder.AllocParameters<FCausticsFinalizeDensityCS::FParameters>();
        Finalize->GuideId = GuideId;
        Finalize->GuideCoverage = GuideCoverage;
        Finalize->SPPMTau = GraphBuilder.CreateUAV(Tau);
        Finalize->SPPMStats = GraphBuilder.CreateUAV(Stats);
        Finalize->DensityOutput = GraphBuilder.CreateUAV(DensityTexture);
        Finalize->OutputResolution = Resolution;
        Finalize->BatchCount = Job.Request.BatchCount;
        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics Finalize Density"), FinalizeShader, Finalize,
            FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8)));

        FRDGTextureRef Input = DensityTexture;
        FRDGTextureRef Output = FinalTexture;
        if (Job.Request.AtrousIterations == 0)
        {
            AddCopyTexturePass(GraphBuilder, DensityTexture, FinalTexture);
        }
        else
        {
            for (int32 Iteration = 0; Iteration < Job.Request.AtrousIterations; ++Iteration)
            {
                Output = (Iteration & 1) == 0 ? FinalTexture : DensityTexture;
                TShaderMapRef<FCausticsAtrousCS> AtrousShader(View.ShaderMap);
                auto* Atrous = GraphBuilder.AllocParameters<FCausticsAtrousCS::FParameters>();
                Atrous->InputTexture = Input;
                Atrous->Guide = Guide;
                Atrous->GuideId = GuideId;
                Atrous->GuideCoverage = GuideCoverage;
                Atrous->SPPMStats = Stats;
                Atrous->OutputTexture = GraphBuilder.CreateUAV(Output);
                Atrous->OutputResolution = Resolution;
                Atrous->StepWidth = 1u << Iteration;
                Atrous->BatchCount = Job.Request.BatchCount;
                Atrous->FilterStrength = Job.Request.FilterStrength;
                FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Caustics a-trous %d", Iteration), AtrousShader, Atrous,
                    FComputeShaderUtils::GetGroupCount(Extent, FIntPoint(8)));
                Input = Output;
            }
            if (Input != FinalTexture)
            {
                AddCopyTexturePass(GraphBuilder, Input, FinalTexture);
            }
        }

        // Preview uses the same asynchronous validation readback as Bake. This
        // prevents a zero-coverage guide from flashing once and then being
        // incorrectly reported as a current, persistent preview.
        Job.TextureReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("CausticsBakerReadback"));
        Job.GuideTextureReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("CausticsBakerGuideReadback"));
        AddEnqueueCopyPass(GraphBuilder, Job.TextureReadback.Get(), FinalTexture);
        AddEnqueueCopyPass(GraphBuilder, Job.GuideTextureReadback.Get(), Guide);
        Job.Stage.Store(ECausticsBakeJobState::Readback);
    }
}

FCausticsRenderJob::FCausticsRenderJob(FCausticsRenderRequest&& InRequest)
    : Request(MoveTemp(InRequest))
{
}

FCausticsRenderJob::~FCausticsRenderJob() = default;

void FCausticsRenderJob::SetError(FString InError)
{
    FScopeLock Lock(&DataGuard);
    Error = MoveTemp(InError);
}

FString FCausticsRenderJob::GetError() const
{
    FScopeLock Lock(&DataGuard);
    return Error;
}

void FCausticsRenderJob::SetResultStatistics(const int32 InCoverageTexels, const int32 InLitTexels,
    const float InMaxRadiance, const double InTotalLuminance)
{
    FScopeLock Lock(&DataGuard);
    CoverageTexels = InCoverageTexels;
    LitTexels = InLitTexels;
    MaxRadiance = InMaxRadiance;
    TotalLuminance = InTotalLuminance;
}

FString FCausticsRenderJob::GetResultStatisticsText() const
{
    FScopeLock Lock(&DataGuard);
    return FString::Printf(TEXT("%d lit / %d covered texels, max %.6g, total luminance %.6g"),
        LitTexels, CoverageTexels, MaxRadiance, TotalLuminance);
}

void FCausticsRenderJob::SetPixels(TArray<FFloat16Color>&& InPixels, TArray<FVector3f>&& InNormals)
{
    FScopeLock Lock(&DataGuard);
    CpuPixels = MoveTemp(InPixels);
    CpuNormals = MoveTemp(InNormals);
    bCpuPixelsReady.Store(true);
}

bool FCausticsRenderJob::ConsumePixels(TArray<FFloat16Color>& OutPixels, TArray<FVector3f>& OutNormals)
{
    if (!bCpuPixelsReady.Load()) return false;
    FScopeLock Lock(&DataGuard);
    OutPixels = MoveTemp(CpuPixels);
    OutNormals = MoveTemp(CpuNormals);
    bCpuPixelsReady.Store(false);
    return true;
}

FCausticsBakerViewExtension::FCausticsBakerViewExtension(const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
}

void FCausticsBakerViewExtension::SetupView(FSceneViewFamily&, FSceneView& InView)
{
    const uint32 ActiveCaptureOwnerUniqueId = GActiveCaptureOwnerUniqueId.Load();
    ApplyCaptureOverrides(InView.FinalPostProcessSettings, ActiveCaptureOwnerUniqueId,
        InView.bIsSceneCapture, InView.ViewActor.ActorUniqueId);
}

void FCausticsBakerViewExtension::ApplyCaptureOverrides(FPostProcessSettings& InOutSettings,
    const uint32 ActiveCaptureOwnerUniqueId, const bool bIsSceneCapture, const uint32 ViewOwnerUniqueId)
{
    // Ownerless editor views also report ID 0 while SetupView runs, so the inactive sentinel must never match them.
    if (ActiveCaptureOwnerUniqueId != 0u && bIsSceneCapture && ViewOwnerUniqueId == ActiveCaptureOwnerUniqueId)
    {
        InOutSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Plugin;
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        InOutSettings.TranslucencyType = ETranslucencyType::RayTraced_Deprecated;
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
}

void FCausticsBakerViewExtension::PostTLASBuild_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
    if (!RenderJob.IsValid() || InView.ViewActor.ActorUniqueId != RenderJob->Request.CaptureOwnerUniqueId) return;
    FCausticsRenderJob& Job = *RenderJob;
    if (Job.bCancelRequested.Load())
    {
        Job.Stage.Store(ECausticsBakeJobState::Cancelled);
        ReleaseResources_RenderThread(Job);
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
        return;
    }
    if (!InView.IsRayTracingAllowedForView())
    {
        Job.SetError(TEXT("The dedicated capture view was not allowed to use hardware ray tracing."));
        Job.Stage.Store(ECausticsBakeJobState::Failed);
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
        return;
    }
    FViewInfo& View = static_cast<FViewInfo&>(InView);
    if (!View.MaterialRayTracingData.PipelineState || !View.MaterialRayTracingData.ShaderBindingTable)
    {
        Job.SetError(TEXT("The material ray-tracing pipeline or shader binding table was unavailable for the dedicated capture view."));
        Job.Stage.Store(ECausticsBakeJobState::Failed);
        ReleaseResources_RenderThread(Job);
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
        return;
    }
    switch (Job.Stage.Load())
    {
    case ECausticsBakeJobState::BuildingGuide:
        BuildGuide_RenderThread(GraphBuilder, View, Job);
        break;
    case ECausticsBakeJobState::TracingPhotons:
        TracePhotonBatch_RenderThread(GraphBuilder, View, Job);
        break;
    case ECausticsBakeJobState::Filtering:
        FilterAndReadback_RenderThread(GraphBuilder, View, Job);
        break;
    default:
        break;
    }
    if (Job.Stage.Load() == ECausticsBakeJobState::Failed)
    {
        ReleaseResources_RenderThread(Job);
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
    }
}

void FCausticsBakerViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView,
    const FPostProcessingInputs& Inputs)
{
    if (!PreviewJob.IsValid() || InView.bIsSceneCapture || InView.Family->Scene != PreviewJob->Request.SceneInterface ||
        PreviewJob->Stage.Load() != ECausticsBakeJobState::Complete ||
        InView.ViewActor.ActorUniqueId == PreviewJob->Request.CaptureOwnerUniqueId || !PreviewJob->FinalOutput.IsValid())
    {
        return;
    }
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    if (!Inputs.SceneTextures) return;
    const FSceneTextures& SceneTextures = View.GetSceneTextures();
    FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, View.ViewRect);

    TRefCountPtr<IPooledRenderTarget> Selected = PreviewJob->FinalOutput;
    if (PreviewJob->Request.DebugDisplay == ECausticsDebugDisplay::Raw) Selected = PreviewJob->PhotonRaw;
    else if (PreviewJob->Request.DebugDisplay == ECausticsDebugDisplay::DensityFiltered) Selected = PreviewJob->DensityFiltered;

    auto* Parameters = GraphBuilder.AllocParameters<FCausticsPreviewPS::FParameters>();
    Parameters->CausticsTexture = GraphBuilder.RegisterExternalTexture(Selected);
    Parameters->Guide = GraphBuilder.RegisterExternalTexture(PreviewJob->Guide);
    Parameters->GuideId = GraphBuilder.RegisterExternalTexture(PreviewJob->GuideId);
    Parameters->GuideCoverage = GraphBuilder.RegisterExternalTexture(PreviewJob->GuideCoverage);
    Parameters->SceneDepthTexture = SceneTextures.Depth.Resolve;
    Parameters->BilinearClampSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->WorldToRegion = PreviewJob->Request.WorldToRegion;
    Parameters->RegionToWorld = PreviewJob->Request.RegionToWorld;
    Parameters->RegionSize = PreviewJob->Request.RegionSize;
    Parameters->ProjectionTexelWorldSize = PreviewJob->Request.ProjectionTexelWorldSize;
    Parameters->BakeResolution = PreviewJob->Request.Resolution;
    Parameters->DebugDisplay = static_cast<uint32>(PreviewJob->Request.DebugDisplay);
    Parameters->ProjectionMode = static_cast<uint32>(PreviewJob->Request.ProjectionMode);
    Parameters->View = View.ViewUniformBuffer;
    Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ELoad);
    TShaderMapRef<FCausticsPreviewPS> PixelShader(View.ShaderMap);
    FPixelShaderUtils::AddFullscreenPass<FCausticsPreviewPS>(GraphBuilder, View.ShaderMap,
        RDG_EVENT_NAME("Caustics Preview Composite"), PixelShader, Parameters, View.ViewRect,
        TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI());
}

void FCausticsBakerViewExtension::SetRenderJob_RenderThread(TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> InJob)
{
    check(IsInRenderingThread());
    RenderJob = MoveTemp(InJob);
    GActiveCaptureOwnerUniqueId.Store(RenderJob.IsValid() ? RenderJob->Request.CaptureOwnerUniqueId : 0u);
}

bool FCausticsBakerViewExtension::PromoteValidatedPreview_RenderThread(
    const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job)
{
    check(IsInRenderingThread());
    if (!Job.IsValid() || !Job->Request.bPreview || RenderJob != Job || Job->bResourcesReleased.Load())
    {
        return false;
    }
    if (PreviewJob.IsValid() && PreviewJob != Job && !PreviewJob->bResourcesReleased.Load())
    {
        ReleaseResources_RenderThread(*PreviewJob);
    }
    PreviewJob = Job;
    RenderJob.Reset();
    GActiveCaptureOwnerUniqueId.Store(0);
    return true;
}

void FCausticsBakerViewExtension::FinishRenderJob_RenderThread(
    const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job)
{
    check(IsInRenderingThread());
    if (RenderJob == Job)
    {
        if (!Job->bResourcesReleased.Load())
        {
            ReleaseResources_RenderThread(*Job);
        }
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
    }
}

void FCausticsBakerViewExtension::CancelRenderJob_RenderThread(
    const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job)
{
    check(IsInRenderingThread());
    if (RenderJob == Job)
    {
        Job->bCancelRequested.Store(true);
        Job->Stage.Store(ECausticsBakeJobState::Cancelled);
        if (!Job->bResourcesReleased.Load()) ReleaseResources_RenderThread(*Job);
        RenderJob.Reset();
        GActiveCaptureOwnerUniqueId.Store(0);
    }
}

void FCausticsBakerViewExtension::ClearPreview_RenderThread(const uint32 RegionActorUniqueId)
{
    check(IsInRenderingThread());
    if (PreviewJob.IsValid() && (RegionActorUniqueId == 0 || PreviewJob->Request.RegionActorUniqueId == RegionActorUniqueId))
    {
        ReleaseResources_RenderThread(*PreviewJob);
        PreviewJob.Reset();
    }
}

void FCausticsBakerViewExtension::Shutdown_RenderThread()
{
    check(IsInRenderingThread());
    if (RenderJob.IsValid())
    {
        RenderJob->bCancelRequested.Store(true);
        RenderJob->Stage.Store(ECausticsBakeJobState::Cancelled);
        ReleaseResources_RenderThread(*RenderJob);
        RenderJob.Reset();
    }
    if (PreviewJob.IsValid())
    {
        ReleaseResources_RenderThread(*PreviewJob);
        PreviewJob.Reset();
    }
    GActiveCaptureOwnerUniqueId.Store(0);
}

void FCausticsBakerRenderManager::Initialize(TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> InViewExtension)
{
    FScopeLock Lock(&Guard);
    ViewExtension = MoveTemp(InViewExtension);
}

void FCausticsBakerRenderManager::Shutdown()
{
    TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> LocalExtension;
    {
        FScopeLock Lock(&Guard);
        if (ActiveJob.IsValid()) ActiveJob->bCancelRequested.Store(true);
        ActiveJob.Reset();
        LocalExtension = ViewExtension;
        ViewExtension.Reset();
    }
    if (LocalExtension.IsValid())
    {
        ENQUEUE_RENDER_COMMAND(CausticsBakerShutdown)([LocalExtension](FRHICommandListImmediate&) { LocalExtension->Shutdown_RenderThread(); });
        FlushRenderingCommands();
    }
}

TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> FCausticsBakerRenderManager::Start(FCausticsRenderRequest&& Request)
{
    FScopeLock Lock(&Guard);
    if (ActiveJob.IsValid() || !ViewExtension.IsValid()) return nullptr;
    ActiveJob = MakeShared<FCausticsRenderJob, ESPMode::ThreadSafe>(MoveTemp(Request));
    const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> NewJob = ActiveJob;
    const TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> Extension = ViewExtension;
    ENQUEUE_RENDER_COMMAND(CausticsBakerStart)([Extension, NewJob](FRHICommandListImmediate&) { Extension->SetRenderJob_RenderThread(NewJob); });
    return ActiveJob;
}

void FCausticsBakerRenderManager::Cancel()
{
    FScopeLock Lock(&Guard);
    if (ActiveJob.IsValid())
    {
        ActiveJob->bCancelRequested.Store(true);
        const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> Job = ActiveJob;
        const TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> Extension = ViewExtension;
        ENQUEUE_RENDER_COMMAND(CausticsBakerCancel)([Extension, Job](FRHICommandListImmediate&)
        {
            if (Extension.IsValid()) Extension->CancelRenderJob_RenderThread(Job);
        });
    }
}

void FCausticsBakerRenderManager::ClearPreview(const uint32 RegionActorUniqueId)
{
    TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> Extension;
    {
        FScopeLock Lock(&Guard);
        Extension = ViewExtension;
    }
    if (Extension.IsValid())
    {
        ENQUEUE_RENDER_COMMAND(CausticsBakerClearPreview)([Extension, RegionActorUniqueId](FRHICommandListImmediate&)
        {
            Extension->ClearPreview_RenderThread(RegionActorUniqueId);
        });
    }
}

void FCausticsBakerRenderManager::PollReadback(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job)
{
    if (!Job.IsValid() || Job->Stage.Load() != ECausticsBakeJobState::Readback || Job->bReadbackPollQueued.Exchange(true)) return;
    TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> Extension;
    {
        FScopeLock Lock(&Guard);
        Extension = ViewExtension;
    }
    ENQUEUE_RENDER_COMMAND(CausticsBakerPollReadback)([Job, Extension](FRHICommandListImmediate&)
    {
        if (Job->bCancelRequested.Load())
        {
            Job->Stage.Store(ECausticsBakeJobState::Cancelled);
            ReleaseResources_RenderThread(*Job);
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        if (!Job->TextureReadback || !Job->GuideTextureReadback || !Job->TextureReadback->IsReady() || !Job->GuideTextureReadback->IsReady())
        {
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        int32 RowPitchPixels = 0;
        int32 BufferHeight = 0;
        void* Data = Job->TextureReadback->Lock(RowPitchPixels, &BufferHeight);
        if (!Data)
        {
            Job->SetError(TEXT("GPU texture readback returned no data."));
            Job->Stage.Store(ECausticsBakeJobState::Failed);
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        const int32 Resolution = Job->Request.Resolution;
        TArray<FFloat16Color> Pixels;
        Pixels.SetNumUninitialized(Resolution * Resolution);
        const FFloat16Color* Source = static_cast<const FFloat16Color*>(Data);
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            FMemory::Memcpy(Pixels.GetData() + Y * Resolution, Source + Y * RowPitchPixels,
                Resolution * sizeof(FFloat16Color));
        }
        Job->TextureReadback->Unlock();
        int32 GuideRowPitchPixels = 0;
        int32 GuideBufferHeight = 0;
        void* GuideData = Job->GuideTextureReadback->Lock(GuideRowPitchPixels, &GuideBufferHeight);
        if (!GuideData)
        {
            Job->SetError(TEXT("GPU guide readback returned no data."));
            Job->Stage.Store(ECausticsBakeJobState::Failed);
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        const FVector4f* GuideSource = static_cast<const FVector4f*>(GuideData);
        TArray<FVector3f> Normals;
        Normals.SetNumUninitialized(Resolution * Resolution);
        int32 UnmatchedGeometryPixels = 0;
        int32 NoGeometryPixels = 0;
        TMap<uint32, int32> UnmatchedPrimitiveCounts;
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const FVector4f& G = GuideSource[Y * GuideRowPitchPixels + X];
                Normals[Y * Resolution + X] = FVector3f(G.Y, G.Z, G.W).GetSafeNormal();
                if (G.Y < -1.5f)
                {
                    ++NoGeometryPixels;
                }
                else if (G.Y < -0.5f)
                {
                    ++UnmatchedGeometryPixels;
                    uint32 PrimitiveId = MAX_uint32;
                    FMemory::Memcpy(&PrimitiveId, &G.X, sizeof(PrimitiveId));
                    ++UnmatchedPrimitiveCounts.FindOrAdd(PrimitiveId);
                }
            }
        }
        Job->GuideTextureReadback->Unlock();

        bool bAnyCoverage = false;
        bool bAnyRadiance = false;
        int32 CoverageTexels = 0;
        int32 LitTexels = 0;
        float MaxRadiance = 0.0f;
        double TotalLuminance = 0.0;
        for (const FFloat16Color& Pixel : Pixels)
        {
            const float R = Pixel.R.GetFloat();
            const float G = Pixel.G.GetFloat();
            const float B = Pixel.B.GetFloat();
            if (Pixel.A.GetFloat() > 0.0f)
            {
                bAnyCoverage = true;
                ++CoverageTexels;
            }
            if (R > 0.0f || G > 0.0f || B > 0.0f)
            {
                bAnyRadiance = true;
                ++LitTexels;
                MaxRadiance = FMath::Max(MaxRadiance, FMath::Max3(R, G, B));
                TotalLuminance += FMath::Max(0.0f, 0.2126f * R + 0.7152f * G + 0.0722f * B);
            }
        }
        Job->SetResultStatistics(CoverageTexels, LitTexels, MaxRadiance, TotalLuminance);
        if (!bAnyCoverage)
        {
            if (UnmatchedGeometryPixels > 0)
            {
                uint32 MostFrequentPrimitive = MAX_uint32;
                int32 MostFrequentCount = 0;
                for (const TPair<uint32, int32>& Pair : UnmatchedPrimitiveCounts)
                {
                    if (Pair.Value > MostFrequentCount)
                    {
                        MostFrequentPrimitive = Pair.Key;
                        MostFrequentCount = Pair.Value;
                    }
                }
                FString ExpectedIds;
                for (const uint32 Id : Job->ReceiverPrimitiveIds)
                {
                    if (!ExpectedIds.IsEmpty()) ExpectedIds += TEXT(", ");
                    ExpectedIds += FString::FromInt(static_cast<int32>(Id));
                }
                Job->SetError(FString::Printf(
                    TEXT("Receiver guide hit ray-tracing geometry in %d pixels, but no hit matched a receiver ID. Most frequent hit ID: %u (%d pixels); expected receiver IDs: [%s]."),
                    UnmatchedGeometryPixels, MostFrequentPrimitive, MostFrequentCount, *ExpectedIds));
            }
            else
            {
                Job->SetError(FString::Printf(
                    TEXT("Receiver guide rays did not intersect any ray-tracing geometry (%d projection samples missed). Check region placement, +X projection direction, and TLAS visibility."),
                    NoGeometryPixels));
            }
            Job->TextureReadback.Reset();
            Job->GuideTextureReadback.Reset();
            Job->Stage.Store(ECausticsBakeJobState::Failed);
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        if (!bAnyRadiance)
        {
            Job->SetError(Job->Request.LightType == ECausticsRenderLightType::Directional
                ? TEXT("No caustic photons reached a receiver. Check that the light reaches a caster and that a reflected or refracted path can land on a receiver inside the projection box.")
                : TEXT("No caustic photons reached a receiver. For Point/Spot lights, check Attenuation Radius, light-to-caster occlusion, and whether the reflected or refracted path lands inside the projection box. Metal needs at least 1 optical bounce; closed solid glass normally needs at least 2."));
            Job->TextureReadback.Reset();
            Job->GuideTextureReadback.Reset();
            Job->Stage.Store(ECausticsBakeJobState::Failed);
            Job->bReadbackPollQueued.Store(false);
            return;
        }

        Job->TextureReadback.Reset();
        Job->GuideTextureReadback.Reset();
        if (Job->Request.bPreview)
        {
            if (!Extension.IsValid() || !Extension->PromoteValidatedPreview_RenderThread(Job))
            {
                Job->SetError(TEXT("The validated preview could not be attached to the editor viewport."));
                Job->Stage.Store(ECausticsBakeJobState::Failed);
            }
            else
            {
                Job->Stage.Store(ECausticsBakeJobState::Complete);
            }
            Job->bReadbackPollQueued.Store(false);
            return;
        }
        Job->SetPixels(MoveTemp(Pixels), MoveTemp(Normals));
        Job->Stage.Store(ECausticsBakeJobState::Saving);
        Job->bReadbackPollQueued.Store(false);
    });
}

TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> FCausticsBakerRenderManager::GetActiveJob() const
{
    FScopeLock Lock(&Guard);
    return ActiveJob;
}

void FCausticsBakerRenderManager::ForgetCompletedJob(const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe>& Job)
{
    FScopeLock Lock(&Guard);
    if (ActiveJob == Job)
    {
        ActiveJob.Reset();
        const TSharedPtr<FCausticsBakerViewExtension, ESPMode::ThreadSafe> Extension = ViewExtension;
        ENQUEUE_RENDER_COMMAND(CausticsBakerFinishJob)([Extension, Job](FRHICommandListImmediate&)
        {
            if (Extension.IsValid()) Extension->FinishRenderJob_RenderThread(Job);
        });
    }
}

FCausticsBakerRenderManager& GetCausticsBakerRenderManager()
{
    static FCausticsBakerRenderManager Manager;
    return Manager;
}

void AddCausticsRayGenerationShaders(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
#if RHI_RAYTRACING
    const TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> Job = GetCausticsBakerRenderManager().GetActiveJob();
    if (!Job.IsValid() || View.ViewActor.ActorUniqueId != Job->Request.CaptureOwnerUniqueId) return;
    TShaderMapRef<FCausticsGuideRG> GuideShader(View.ShaderMap);
    TShaderMapRef<FCausticsPhotonRG> PhotonShader(View.ShaderMap);
    OutRayGenShaders.Add(GuideShader.GetRayTracingShader());
    OutRayGenShaders.Add(PhotonShader.GetRayTracingShader());
#endif
}
