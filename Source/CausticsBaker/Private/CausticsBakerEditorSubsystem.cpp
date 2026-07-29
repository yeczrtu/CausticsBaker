#include "CausticsBakerEditorSubsystem.h"

#include "CausticsTextureOutput.h"

#include "CausticsBakeRegion.h"
#include "CausticsBakerRenderer.h"
#include "CausticsBakerSceneCapture.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/RendererSettings.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "OpenImageDenoise/oidn.hpp"
#include "RenderUtils.h"
#include "RHI.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "CausticsBakerSubsystem"

DEFINE_LOG_CATEGORY_STATIC(LogCausticsBakerSubsystem, Log, All);

namespace
{
    bool IsTerminalState(const ECausticsBakeJobState State)
    {
        return State == ECausticsBakeJobState::Complete || State == ECausticsBakeJobState::Failed ||
            State == ECausticsBakeJobState::Cancelled;
    }

    bool HasTranslucentMaterial(const UPrimitiveComponent* Primitive)
    {
        for (int32 Index = 0; Index < Primitive->GetNumMaterials(); ++Index)
        {
            const UMaterialInterface* Material = Primitive->GetMaterial(Index);
            if (Material && Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked)
            {
                return true;
            }
        }
        return false;
    }

    FBox GetPrimitiveBoundsInRegionSpace(const ACausticsBakeRegion* Region, const UPrimitiveComponent* Primitive)
    {
        FBox LocalBounds(ForceInit);
        if (!Region || !Primitive)
        {
            return LocalBounds;
        }

        const FBox WorldBounds = Primitive->Bounds.GetBox();
        const FTransform WorldToRegion = Region->GetActorTransform().Inverse();
        for (int32 X = 0; X < 2; ++X)
        {
            for (int32 Y = 0; Y < 2; ++Y)
            {
                for (int32 Z = 0; Z < 2; ++Z)
                {
                    LocalBounds += WorldToRegion.TransformPosition(FVector(
                        X ? WorldBounds.Max.X : WorldBounds.Min.X,
                        Y ? WorldBounds.Max.Y : WorldBounds.Min.Y,
                        Z ? WorldBounds.Max.Z : WorldBounds.Min.Z));
                }
            }
        }
        return LocalBounds;
    }

    bool OverlapsProjectionFootprint(const ACausticsBakeRegion* Region, const FBox& LocalBounds)
    {
        return LocalBounds.IsValid &&
            LocalBounds.Max.Y >= -Region->Width * 0.5 && LocalBounds.Min.Y <= Region->Width * 0.5 &&
            LocalBounds.Max.Z >= -Region->Height * 0.5 && LocalBounds.Min.Z <= Region->Height * 0.5;
    }

    bool IntersectsProjectionVolume(const ACausticsBakeRegion* Region, const FBox& LocalBounds)
    {
        return OverlapsProjectionFootprint(Region, LocalBounds) &&
            LocalBounds.Max.X >= 0.0 && LocalBounds.Min.X <= Region->Depth;
    }

    bool AutoFitFilteredReceiverDepth(ACausticsBakeRegion* Region, float& OutPreviousDepth)
    {
        OutPreviousDepth = Region ? Region->Depth : 0.0f;
        if (!Region || !Region->bAutoFitDepthToReceiverFilter || Region->Receivers.IsEmpty())
        {
            return false;
        }

        float RequiredDepth = Region->Depth;
        for (const FComponentReference& Reference : Region->Receivers)
        {
            const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Reference.GetComponent(Region));
            const FBox LocalBounds = GetPrimitiveBoundsInRegionSpace(Region, Primitive);
            if (OverlapsProjectionFootprint(Region, LocalBounds) && LocalBounds.Max.X > 0.0)
            {
                const float Padding = FMath::Max(1.0f, static_cast<float>(LocalBounds.Max.X) * 0.001f);
                RequiredDepth = FMath::Max(RequiredDepth, static_cast<float>(LocalBounds.Max.X) + Padding);
            }
        }

        if (RequiredDepth <= Region->Depth + KINDA_SMALL_NUMBER)
        {
            return false;
        }

        Region->Modify();
        Region->Depth = RequiredDepth;
        Region->RefreshVisualization();
        Region->MarkResultsOutOfDate();
        return true;
    }

    FCausticsOIDNResult RunOIDN(TArray<FFloat16Color> Pixels, TArray<FVector3f> Normals, const int32 Resolution,
        const FQuat4f& WorldToProjectionRotation)
    {
        FCausticsOIDNResult Result;
        Result.Pixels = MoveTemp(Pixels);
        if (Normals.Num() != Result.Pixels.Num())
        {
            Result.Error = TEXT("OIDN guide normal readback size did not match the color image.");
            return Result;
        }

        TArray<FVector3f> Color;
        TArray<FVector3f> Albedo;
        TArray<FVector3f> Output;
        Color.SetNumUninitialized(Result.Pixels.Num());
        Albedo.Init(FVector3f(1.0f), Result.Pixels.Num());
        Output.SetNumUninitialized(Result.Pixels.Num());
        for (int32 Index = 0; Index < Result.Pixels.Num(); ++Index)
        {
            Color[Index] = FVector3f(Result.Pixels[Index].R.GetFloat(), Result.Pixels[Index].G.GetFloat(),
                Result.Pixels[Index].B.GetFloat());
            if (Result.Pixels[Index].A.GetFloat() <= 0.0f)
            {
                Normals[Index] = FVector3f(0.0f, 0.0f, 1.0f);
            }
            else
            {
                Normals[Index] = WorldToProjectionRotation.RotateVector(Normals[Index]).GetSafeNormal();
            }
        }

        oidn::DeviceRef Device = oidn::newDevice(oidn::DeviceType::CPU);
        Device.commit();
        oidn::FilterRef Filter = Device.newFilter("RT");
        Filter.setImage("color", Color.GetData(), oidn::Format::Float3, Resolution, Resolution);
        Filter.setImage("albedo", Albedo.GetData(), oidn::Format::Float3, Resolution, Resolution);
        Filter.setImage("normal", Normals.GetData(), oidn::Format::Float3, Resolution, Resolution);
        Filter.setImage("output", Output.GetData(), oidn::Format::Float3, Resolution, Resolution);
        Filter.set("hdr", true);
        Filter.set("cleanAux", true);
        Filter.set("quality", oidn::Quality::High);
        Filter.commit();
        Filter.execute();
        const char* ErrorMessage = nullptr;
        const oidn::Error Error = Device.getError(ErrorMessage);
        if (Error != oidn::Error::None)
        {
            Result.Error = FString::Printf(TEXT("Intel OIDN failed: %s"), ErrorMessage ? UTF8_TO_TCHAR(ErrorMessage) : TEXT("unknown error"));
            return Result;
        }
        for (int32 Index = 0; Index < Result.Pixels.Num(); ++Index)
        {
            const float Coverage = Result.Pixels[Index].A.GetFloat();
            Result.Pixels[Index] = FFloat16Color(FLinearColor(
                FMath::Max(0.0f, Output[Index].X) * Coverage,
                FMath::Max(0.0f, Output[Index].Y) * Coverage,
                FMath::Max(0.0f, Output[Index].Z) * Coverage,
                Coverage));
        }
        return Result;
    }
}

void UCausticsBakerEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    MapChangeHandle = FEditorDelegates::MapChange.AddUObject(this, &UCausticsBakerEditorSubsystem::OnMapChanged);
    SetStatus(ECausticsBakeJobState::Idle, 0.0f, LOCTEXT("Ready", "Ready"));
}

void UCausticsBakerEditorSubsystem::Deinitialize()
{
    Cancel();
    if (bOIDNRunning && OIDNFuture.IsValid())
    {
        OIDNFuture.Wait();
        bOIDNRunning = false;
    }
    if (MapChangeHandle.IsValid()) FEditorDelegates::MapChange.Remove(MapChangeHandle);
    DestroyCapture();
    if (RenderJob.IsValid())
    {
        GetCausticsBakerRenderManager().ForgetCompletedJob(RenderJob);
        RenderJob.Reset();
    }
    Super::Deinitialize();
}

TStatId UCausticsBakerEditorSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UCausticsBakerEditorSubsystem, STATGROUP_Tickables);
}

bool UCausticsBakerEditorSubsystem::IsBusy() const
{
    return Status.State != ECausticsBakeJobState::Idle && !IsTerminalState(Status.State);
}

void UCausticsBakerEditorSubsystem::SetStatus(const ECausticsBakeJobState NewState, const float Progress,
    const FText& Message, const bool bCancelPending)
{
    Status.State = NewState;
    Status.Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
    Status.Message = Message;
    Status.bCancelPending = bCancelPending;
    OnStatusChanged.Broadcast(Status);
    if (IsTerminalState(NewState))
    {
        UE_LOG(LogCausticsBakerSubsystem, Display, TEXT("Job ended in %s: %s"),
            *CausticsBaker::JobStateToText(NewState).ToString(), *Message.ToString());
    }
}

bool UCausticsBakerEditorSubsystem::RequestPreview(ACausticsBakeRegion* Region)
{
    return StartJob(Region, true);
}

bool UCausticsBakerEditorSubsystem::RequestBake(ACausticsBakeRegion* Region)
{
    return StartJob(Region, false);
}

TArray<FText> UCausticsBakerEditorSubsystem::ValidateRegion(const ACausticsBakeRegion* Region) const
{
    TArray<FText> Errors;
    if (!Region)
    {
        Errors.Add(LOCTEXT("NoRegion", "No Caustics Bake Region was supplied."));
        return Errors;
    }
    const UWorld* World = Region->GetWorld();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        Errors.Add(LOCTEXT("EditorWorldOnly", "The region must be in an Editor world."));
    }
    if (!GDynamicRHI || FString(GDynamicRHI->GetName()) != TEXT("D3D12"))
    {
        Errors.Add(LOCTEXT("DX12Required", "DirectX 12 is required."));
    }
    if (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM6)
    {
        Errors.Add(LOCTEXT("SM6Required", "Shader Model 6 is required."));
    }
    if (!GRHISupportsRayTracing || !GRHISupportsRayTracingShaders || !IsRayTracingEnabled())
    {
        Errors.Add(LOCTEXT("FullRTRequired", "Hardware ray tracing with the full ray-tracing pipeline is required. Restart the editor after enabling Support Hardware Ray Tracing."));
    }

    const ULightComponent* Light = Region->ResolveLight();
    if (!Light)
    {
        Errors.Add(LOCTEXT("MissingLight", "Choose one Directional, Point, or Spot Light actor."));
    }
    else if (!Light->IsA<UDirectionalLightComponent>() && !Light->IsA<UPointLightComponent>() && !Light->IsA<USpotLightComponent>())
    {
        Errors.Add(LOCTEXT("UnsupportedLight", "Only Directional, Point, and Spot lights are supported."));
    }
    else if (Light->GetWorld() != World)
    {
        Errors.Add(LOCTEXT("LightWorld", "The light must be in the same Editor world as the region."));
    }
    else if (Light->GetColoredLightBrightness().GetLuminance() <= SMALL_NUMBER)
    {
        Errors.Add(LOCTEXT("ZeroLightBrightness", "The selected light has zero effective brightness. Increase Intensity and use a non-black Light Color."));
    }

    if (Region->Casters.IsEmpty()) Errors.Add(LOCTEXT("NoCasters", "Add at least one caster."));

    TSet<const UPrimitiveComponent*> CasterSet;
    TSet<const UPrimitiveComponent*> ReceiverSet;
    bool bHasTranslucentCaster = false;
    bool bAnyCasterWithinLocalLightRange = false;
    const ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(Light);
    for (int32 Index = 0; Index < Region->Casters.Num(); ++Index)
    {
        const FCausticsCasterEntry& Entry = Region->Casters[Index];
        const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Entry.Component.GetComponent(const_cast<ACausticsBakeRegion*>(Region)));
        if (!CausticsBaker::IsSupportedPrimitiveComponent(Primitive))
        {
            Errors.Add(FText::Format(LOCTEXT("InvalidCaster", "Caster {0} must reference a StaticMesh, ISM, or HISM component."), Index));
            continue;
        }
        if (Primitive->GetWorld() != World)
        {
            Errors.Add(FText::Format(LOCTEXT("CasterWorld", "Caster {0} is in a different world."), Index));
        }
        if (!Primitive->bVisibleInRayTracing)
        {
            Errors.Add(FText::Format(LOCTEXT("CasterNotRT", "Caster {0} has Visible in Ray Tracing disabled."), Index));
        }
        if (!Primitive->GetSceneProxy())
        {
            Errors.Add(FText::Format(LOCTEXT("CasterNoProxy", "Caster {0} has no registered render proxy."), Index));
        }
        if (CasterSet.Contains(Primitive))
        {
            Errors.Add(FText::Format(LOCTEXT("CasterDuplicate", "Caster {0} is listed more than once."), Index));
        }
        CasterSet.Add(Primitive);
        if (LocalLight)
        {
            const double DistanceToCaster = FVector::Distance(LocalLight->GetComponentLocation(), Primitive->Bounds.Origin);
            bAnyCasterWithinLocalLightRange |=
                DistanceToCaster <= static_cast<double>(LocalLight->AttenuationRadius) + Primitive->Bounds.SphereRadius;
        }
        bHasTranslucentCaster |= HasTranslucentMaterial(Primitive);
        if (Entry.OpticalMode == ECausticsOpticalMode::DielectricOverride && Entry.IndexOfRefraction <= 1.0f)
        {
            Errors.Add(FText::Format(LOCTEXT("InvalidIOR", "Caster {0}: dielectric IOR must be greater than 1."), Index));
        }
    }
    if (LocalLight && !CasterSet.IsEmpty() && !bAnyCasterWithinLocalLightRange)
    {
        Errors.Add(LOCTEXT("LocalLightOutOfRange",
            "The Point/Spot Light Attenuation Radius does not reach any caster. Increase Attenuation Radius or move the light closer."));
    }
    if (!Region->Receivers.IsEmpty())
    {
        bool bAnyFilteredReceiverIntersectsProjection = false;
        for (int32 Index = 0; Index < Region->Receivers.Num(); ++Index)
        {
            const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Region->Receivers[Index].GetComponent(const_cast<ACausticsBakeRegion*>(Region)));
            if (!CausticsBaker::IsSupportedPrimitiveComponent(Primitive))
            {
                Errors.Add(FText::Format(LOCTEXT("InvalidReceiver", "Receiver filter {0} must reference a StaticMesh, ISM, or HISM component."), Index));
                continue;
            }
            if (Primitive->GetWorld() != World)
            {
                Errors.Add(FText::Format(LOCTEXT("ReceiverWorld", "Receiver filter {0} is in a different world."), Index));
            }
            if (!Primitive->bVisibleInRayTracing)
            {
                Errors.Add(FText::Format(LOCTEXT("ReceiverNotRT", "Receiver filter {0} has Visible in Ray Tracing disabled."), Index));
            }
            if (!Primitive->GetSceneProxy())
            {
                Errors.Add(FText::Format(LOCTEXT("ReceiverNoProxy", "Receiver filter {0} has no registered render proxy."), Index));
            }
            if (ReceiverSet.Contains(Primitive))
            {
                Errors.Add(FText::Format(LOCTEXT("ReceiverDuplicate", "Receiver filter {0} is listed more than once."), Index));
            }
            ReceiverSet.Add(Primitive);
            if (CasterSet.Contains(Primitive))
            {
                Errors.Add(FText::Format(LOCTEXT("CasterReceiverOverlap", "Receiver filter {0} is also assigned as a caster."), Index));
            }
            bAnyFilteredReceiverIntersectsProjection |=
                IntersectsProjectionVolume(Region, GetPrimitiveBoundsInRegionSpace(Region, Primitive));
        }
        if (!bAnyFilteredReceiverIntersectsProjection)
        {
            Errors.Add(LOCTEXT("FilteredReceiversOutsideProjection",
                "No filtered receiver intersects the projection volume. A receiver must overlap Width/Height and lie ahead along local +X before Depth. Enable Auto Fit Depth To Receiver Filter, increase Depth, or rotate the region."));
        }
    }
    else
    {
        TArray<UPrimitiveComponent*> AutoReceivers;
        Region->ResolveReceiverComponents(AutoReceivers);
        if (AutoReceivers.IsEmpty())
        {
            Errors.Add(LOCTEXT("NoAutoReceivers", "No ray-tracing-visible StaticMesh, ISM, or HISM receiver intersects the projection box."));
        }
    }
    const IConsoleVariable* ExcludeTranslucent = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.ExcludeTranslucent"));
    if (bHasTranslucentCaster && ExcludeTranslucent && ExcludeTranslucent->GetInt() != 0)
    {
        Errors.Add(LOCTEXT("TranslucentExcluded", "A translucent caster is assigned, but r.RayTracing.ExcludeTranslucent is 1."));
    }
    const FString PackageName = Region->GetEffectiveOutputPackageName();
    if (!FPackageName::IsValidLongPackageName(PackageName))
    {
        Errors.Add(FText::Format(LOCTEXT("InvalidOutputPath", "The output package path is invalid: {0}"), FText::FromString(PackageName)));
    }
    return Errors;
}

bool UCausticsBakerEditorSubsystem::StartJob(ACausticsBakeRegion* Region, const bool bPreview)
{
    if (IsBusy() || GetCausticsBakerRenderManager().GetActiveJob().IsValid())
    {
        return false;
    }
    float PreviousDepth = 0.0f;
    const bool bDepthAutoFitted = AutoFitFilteredReceiverDepth(Region, PreviousDepth);
    SetStatus(ECausticsBakeJobState::Validating, 0.0f,
        bDepthAutoFitted
            ? FText::Format(LOCTEXT("ValidatingAutoFitDepth", "Auto-fitted projection Depth from {0} cm to {1} cm; validating ray-tracing configuration"),
                FText::AsNumber(PreviousDepth), FText::AsNumber(Region->Depth))
            : LOCTEXT("ValidatingJob", "Validating region and ray-tracing configuration"));
    const TArray<FText> Errors = ValidateRegion(Region);
    if (!Errors.IsEmpty())
    {
        SetStatus(ECausticsBakeJobState::Failed, 0.0f, FText::Join(FText::FromString(TEXT("\n")), Errors));
        return false;
    }

    int32 Resolution, BatchCount, PhotonsPerBatch, MaxBounces, AtrousIterations;
    Region->Settings.Resolve(bPreview, Resolution, BatchCount, PhotonsPerBatch, MaxBounces, AtrousIterations);

    UWorld* World = Region->GetWorld();
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("CausticsBakerCapture"));
    SpawnParameters.ObjectFlags |= RF_Transient;
    SpawnParameters.bHideFromSceneOutliner = true;
    CaptureOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
    if (!CaptureOwner)
    {
        SetStatus(ECausticsBakeJobState::Failed, 0.0f, LOCTEXT("CaptureActorFailed", "Could not create the temporary scene-capture actor."));
        return false;
    }
    CaptureOwner->bIsEditorOnlyActor = true;
    CaptureOwner->SetActorHiddenInGame(true);
    USceneComponent* CaptureRoot = NewObject<USceneComponent>(CaptureOwner, TEXT("CaptureRoot"), RF_Transient);
    CaptureOwner->SetRootComponent(CaptureRoot);
    CaptureOwner->AddInstanceComponent(CaptureRoot);
    CaptureRoot->RegisterComponent();

    SceneCapture = NewObject<UCausticsBakerSceneCaptureComponent2D>(CaptureOwner, TEXT("CausticsCapture"), RF_Transient);
    CaptureOwner->AddInstanceComponent(SceneCapture);
    SceneCapture->SetupAttachment(CaptureRoot);
    SceneCapture->RegisterComponent();
    UTextureRenderTarget2D* CaptureTarget = NewObject<UTextureRenderTarget2D>(CaptureOwner, TEXT("CausticsCaptureTarget"), RF_Transient);
    CaptureTarget->RenderTargetFormat = RTF_RGBA16f;
    CaptureTarget->InitAutoFormat(64, 64);
    CaptureTarget->UpdateResourceImmediate(true);
    SceneCapture->TextureTarget = CaptureTarget;
    SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    // UE 5.8 only gathers ray-tracing primitives for views with a persistent
    // FSceneViewState. A one-shot scene capture does not allocate one unless
    // this flag is set, which otherwise produces a valid but empty TLAS.
    SceneCapture->bAlwaysPersistRenderingState = true;
    SceneCapture->bUseRayTracingIfEnabled = true;
    SceneCapture->FOVAngle = 90.0f;
    SceneCapture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
    SceneCapture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Plugin;
    SceneCapture->PostProcessSettings.bOverride_TranslucencyType = true;
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    SceneCapture->PostProcessSettings.TranslucencyType = ETranslucencyType::RayTraced_Deprecated;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS

    FCausticsRenderRequest Request;
    Request.CaptureOwnerUniqueId = CaptureOwner->GetUniqueID();
    Request.RegionActorUniqueId = Region->GetUniqueID();
    Request.SceneInterface = World->Scene;
    Request.bPreview = bPreview;
    Request.bUseOIDN = !bPreview && Region->Settings.Denoiser == ECausticsDenoiser::AtrousThenOIDN;
    Request.Resolution = Resolution;
    Request.BatchCount = BatchCount;
    Request.PhotonsPerBatch = PhotonsPerBatch;
    Request.MaxBounces = MaxBounces;
    Request.GuideSamples = bPreview ? 1 : 4;
    Request.AtrousIterations = Region->Settings.Denoiser == ECausticsDenoiser::None ? 0 : AtrousIterations;
    Request.RandomSeed = Region->Settings.RandomSeed;
    Request.SPPMAlpha = FMath::Clamp(Region->Settings.SPPMConvergence, 0.01f, 1.0f);
    Request.InitialRadiusTexels = Region->Settings.InitialRadiusTexels;
    Request.FilterStrength = Region->Settings.FilterStrength;
    Request.DebugDisplay = Region->Settings.DebugDisplay;
    Request.RegionToWorld = FMatrix44f(Region->GetActorTransform().ToMatrixWithScale());
    Request.WorldToRegion = FMatrix44f(Region->GetActorTransform().ToInverseMatrixWithScale());
    Request.RegionSize = FVector3f(Region->Depth, Region->Width, Region->Height);
    const FTransform RegionTransform = Region->GetActorTransform();
    Request.WorldToRegionRotation = FQuat4f(RegionTransform.GetRotation().Inverse());
    const float TexelY = RegionTransform.TransformVector(FVector(0.0, Region->Width / Resolution, 0.0)).Size();
    const float TexelZ = RegionTransform.TransformVector(FVector(0.0, 0.0, Region->Height / Resolution)).Size();
    Request.ProjectionTexelWorldSize = FMath::Max(TexelY, TexelZ);

    FBox CaptureBounds = Region->GetWorldProjectionBounds();
    FBox CasterBounds(ForceInit);
    for (const FCausticsCasterEntry& Entry : Region->Casters)
    {
        UPrimitiveComponent* Primitive = CastChecked<UPrimitiveComponent>(Entry.Component.GetComponent(Region));
        FCausticsRenderCaster Caster;
        Caster.PrimitiveComponentId = Primitive->GetPrimitiveSceneId();
        Caster.OpticalMode = Entry.OpticalMode;
        Caster.ThicknessMode = Entry.ThicknessMode;
        Caster.IOR = Entry.IndexOfRefraction;
        Caster.Roughness = FMath::Clamp(Entry.Roughness, 0.001f, 1.0f);
        Caster.Tint = FVector3f(Entry.Tint.R, Entry.Tint.G, Entry.Tint.B);
        Caster.Absorption = FVector3f(Entry.Absorption.R, Entry.Absorption.G, Entry.Absorption.B);
        Caster.ThinThicknessCm = Entry.ThinThicknessCm;
        Request.Casters.Add(Caster);
        CaptureBounds += Primitive->Bounds.GetBox();
        CasterBounds += Primitive->Bounds.GetBox();
    }
    TArray<UPrimitiveComponent*> ResolvedReceivers;
    Region->ResolveReceiverComponents(ResolvedReceivers);
    const bool bAutomaticReceivers = Region->Receivers.IsEmpty();
    for (UPrimitiveComponent* Primitive : ResolvedReceivers)
    {
        Request.Receivers.Add(Primitive->GetPrimitiveSceneId());
        // The projection box already encloses the only receiver surface area
        // that guide rays can use. Auto discovery can conservatively include a
        // huge enclosing mesh (for example a sky sphere); adding that mesh's
        // full bounds would destroy capture-frustum precision.
        if (!bAutomaticReceivers)
        {
            CaptureBounds += Primitive->Bounds.GetBox();
        }
    }
    const FBoxSphereBounds CasterSphere(CasterBounds);
    Request.CasterBoundsCenter = FVector3f(CasterSphere.Origin);
    Request.CasterBoundsRadius = FMath::Max(1.0f, static_cast<float>(CasterSphere.SphereRadius));

    ULightComponent* Light = Region->ResolveLight();
    Request.LightPosition = FVector3f(Light->GetComponentLocation());
    Request.LightDirection = FVector3f(Light->GetDirection().GetSafeNormal());
    Request.LightColor = FVector3f(FLinearColor::FromSRGBColor(Light->LightColor));
    Request.LightIntensity = Light->Intensity;
    if (const UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Light))
    {
        Request.LightType = ECausticsRenderLightType::Directional;
        Request.LightSourceAngleRadians = FMath::DegreesToRadians(Directional->LightSourceAngle * 0.5f);
    }
    else if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
    {
        Request.LightType = ECausticsRenderLightType::Spot;
        Request.LightSourceRadius = Spot->SourceRadius;
        Request.LightAttenuationRadius = Spot->AttenuationRadius;
        Request.LightFalloffExponent = Spot->LightFalloffExponent;
        Request.bInverseSquaredFalloff = Spot->bUseInverseSquaredFalloff ? 1u : 0u;
        Request.SpotInnerCos = FMath::Cos(FMath::DegreesToRadians(Spot->InnerConeAngle));
        Request.SpotOuterCos = FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle));
    }
    else if (const UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
    {
        Request.LightType = ECausticsRenderLightType::Point;
        Request.LightSourceRadius = Point->SourceRadius;
        Request.LightAttenuationRadius = Point->AttenuationRadius;
        Request.LightFalloffExponent = Point->LightFalloffExponent;
        Request.bInverseSquaredFalloff = Point->bUseInverseSquaredFalloff ? 1u : 0u;
    }

    FVector LightDirection = FVector(Request.LightDirection).GetSafeNormal();
    FVector Tangent, Bitangent;
    LightDirection.FindBestAxisVectors(Tangent, Bitangent);
    Request.EmissionTangent = FVector3f(Tangent);
    Request.EmissionBitangent = FVector3f(Bitangent);
    FVector2f ProjectedHalfExtent(1.0f, 1.0f);
    const FVector CasterCenter = CasterBounds.GetCenter();
    for (int32 X = 0; X < 2; ++X)
    {
        for (int32 Y = 0; Y < 2; ++Y)
        {
            for (int32 Z = 0; Z < 2; ++Z)
            {
                const FVector Corner(X ? CasterBounds.Max.X : CasterBounds.Min.X,
                    Y ? CasterBounds.Max.Y : CasterBounds.Min.Y, Z ? CasterBounds.Max.Z : CasterBounds.Min.Z);
                const FVector Delta = Corner - CasterCenter;
                ProjectedHalfExtent.X = FMath::Max(ProjectedHalfExtent.X, static_cast<float>(FMath::Abs(FVector::DotProduct(Delta, Tangent))));
                ProjectedHalfExtent.Y = FMath::Max(ProjectedHalfExtent.Y, static_cast<float>(FMath::Abs(FVector::DotProduct(Delta, Bitangent))));
            }
        }
    }
    Request.EmissionHalfExtent = ProjectedHalfExtent * 1.01f;
    Request.EmissionCenter = Request.CasterBoundsCenter - Request.LightDirection * (Request.CasterBoundsRadius + 10.0f);
    CaptureBounds += FVector(Request.LightPosition);
    CaptureBounds += FVector(Request.EmissionCenter);

    const FBoxSphereBounds CaptureSphere(CaptureBounds);
    const float CaptureRadius = FMath::Max(100.0f, static_cast<float>(CaptureSphere.SphereRadius));
    const FVector CaptureCenter = CaptureSphere.Origin;
    const FVector CaptureLocation = CaptureCenter - FVector(1.0f, 0.0f, 0.0f) * CaptureRadius * 2.5f;
    CaptureOwner->SetActorLocationAndRotation(CaptureLocation, (CaptureCenter - CaptureLocation).Rotation());
    SceneCapture->MaxViewDistanceOverride = CaptureRadius * 8.0f;

    RenderJob = GetCausticsBakerRenderManager().Start(MoveTemp(Request));
    if (!RenderJob.IsValid())
    {
        DestroyCapture();
        SetStatus(ECausticsBakeJobState::Failed, 0.0f, LOCTEXT("RendererBusy", "The render bridge could not accept the job."));
        return false;
    }
    ActiveRegion = Region;
    ActiveSignature = Region->BuildBakeSignature();
    bPreviewJob = bPreview;
    bCaptureSubmitted = false;
    bOIDNRunning = false;
    LastObservedRenderState = ECausticsBakeJobState::Idle;
    LastObservedBatch = INDEX_NONE;
    PendingBakePixels.Reset();
    PendingGuideNormals.Reset();
    SetStatus(ECausticsBakeJobState::BuildingGuide, 0.02f, LOCTEXT("BuildingGuide", "Building receiver guide"));
    return true;
}

void UCausticsBakerEditorSubsystem::Tick(float)
{
    if (!RenderJob.IsValid()) return;
    if (ActiveRegion && ActiveSignature != ActiveRegion->BuildBakeSignature() && !RenderJob->bCancelRequested.Load())
    {
        Cancel();
    }
    const ECausticsBakeJobState RenderState = RenderJob->Stage.Load();
    const int32 CompletedBatches = RenderJob->CompletedBatches.Load();
    if (RenderState != LastObservedRenderState || CompletedBatches != LastObservedBatch)
    {
        LastObservedRenderState = RenderState;
        LastObservedBatch = CompletedBatches;
        bCaptureSubmitted = false;
    }

    if (RenderJob->bCancelRequested.Load() && RenderState != ECausticsBakeJobState::Cancelled)
    {
        SetStatus(RenderState, Status.Progress, LOCTEXT("CancelPending", "Cancel pending; waiting for the current GPU batch"), true);
    }
    if (RenderJob->bCancelRequested.Load() && RenderState == ECausticsBakeJobState::Saving && !bOIDNRunning)
    {
        FinishTerminalJob(ECausticsBakeJobState::Cancelled, LOCTEXT("CancelledBeforeSave", "Caustics job cancelled before asset creation"));
        return;
    }

    if ((RenderState == ECausticsBakeJobState::BuildingGuide || RenderState == ECausticsBakeJobState::TracingPhotons ||
        RenderState == ECausticsBakeJobState::Filtering) && !bCaptureSubmitted && SceneCapture)
    {
        bCaptureSubmitted = true;
        SceneCapture->CaptureScene();
    }

    if (RenderState == ECausticsBakeJobState::TracingPhotons)
    {
        const float Progress = 0.08f + 0.78f * (static_cast<float>(CompletedBatches) / FMath::Max(1, RenderJob->Request.BatchCount));
        SetStatus(RenderState, Progress, FText::Format(LOCTEXT("PhotonProgress", "Tracing photon batch {0} / {1}"),
            CompletedBatches + 1, RenderJob->Request.BatchCount), RenderJob->bCancelRequested.Load());
    }
    else if (RenderState == ECausticsBakeJobState::Filtering)
    {
        SetStatus(RenderState, 0.88f, LOCTEXT("Filtering", "Running SPPM density estimate and edge-aware a-trous filter"),
            RenderJob->bCancelRequested.Load());
    }
    else if (RenderState == ECausticsBakeJobState::Readback)
    {
        SetStatus(RenderState, 0.93f, LOCTEXT("Readback", "Waiting for asynchronous GPU texture readback"),
            RenderJob->bCancelRequested.Load());
        GetCausticsBakerRenderManager().PollReadback(RenderJob);
    }
    else if (RenderState == ECausticsBakeJobState::Saving)
    {
        if (bOIDNRunning)
        {
            if (!OIDNFuture.IsReady()) return;
            FCausticsOIDNResult OIDNResult = OIDNFuture.Get();
            bOIDNRunning = false;
            if (RenderJob->bCancelRequested.Load())
            {
                FinishTerminalJob(ECausticsBakeJobState::Cancelled, LOCTEXT("CancelledAfterOIDN", "Caustics job cancelled after OIDN completed"));
                return;
            }
            if (!OIDNResult.Error.IsEmpty())
            {
                RenderJob->SetError(OIDNResult.Error);
                FinishTerminalJob(ECausticsBakeJobState::Failed, FText::FromString(OIDNResult.Error));
                return;
            }
            PendingBakePixels = MoveTemp(OIDNResult.Pixels);
        }
        else if (PendingBakePixels.IsEmpty())
        {
            if (!RenderJob->ConsumePixels(PendingBakePixels, PendingGuideNormals)) return;
            bool bAnyCoverage = false;
            for (const FFloat16Color& Pixel : PendingBakePixels)
            {
                if (Pixel.A.GetFloat() > 0.0f) { bAnyCoverage = true; break; }
            }
            if (!bAnyCoverage)
            {
                const FString RenderDiagnostic = RenderJob->GetError();
                FinishTerminalJob(ECausticsBakeJobState::Failed,
                    RenderDiagnostic.IsEmpty()
                        ? LOCTEXT("ZeroCoverage", "Receiver guide coverage is zero. Check the projection direction, region bounds, and receivers.")
                        : FText::FromString(RenderDiagnostic));
                return;
            }
            if (RenderJob->Request.bUseOIDN)
            {
                SetStatus(ECausticsBakeJobState::Saving, 0.96f, LOCTEXT("OIDN", "Running Intel OIDN with white albedo and projected guide normals"));
                TArray<FFloat16Color> OIDNPixels = MoveTemp(PendingBakePixels);
                TArray<FVector3f> OIDNNormals = MoveTemp(PendingGuideNormals);
                const int32 Resolution = RenderJob->Request.Resolution;
                const FQuat4f WorldToProjectionRotation = RenderJob->Request.WorldToRegionRotation;
                OIDNFuture = Async(EAsyncExecution::ThreadPool,
                    [Pixels = MoveTemp(OIDNPixels), Normals = MoveTemp(OIDNNormals), Resolution,
                        WorldToProjectionRotation]() mutable
                    {
                        return RunOIDN(MoveTemp(Pixels), MoveTemp(Normals), Resolution, WorldToProjectionRotation);
                    });
                bOIDNRunning = true;
                return;
            }
        }

        SetStatus(ECausticsBakeJobState::Saving, 0.98f,
            ActiveRegion && ActiveRegion->Settings.OutputFormat == ECausticsOutputFormat::LDR8
                ? LOCTEXT("CreatingLDRAsset", "Creating or updating the 8-bit LDR texture asset")
                : LOCTEXT("CreatingHDRAsset", "Creating or updating the 16-bit HDR texture asset"));
        FString Error;
        UTexture2D* Texture = CreateOrUpdateTexture(PendingBakePixels, Error);
        if (!Texture)
        {
            FinishTerminalJob(ECausticsBakeJobState::Failed, FText::FromString(Error));
            return;
        }
        FinalizeSuccessfulBake(Texture);
        FinishTerminalJob(ECausticsBakeJobState::Complete, LOCTEXT("BakeComplete", "Caustics texture bake complete; package is dirty and was not auto-saved."));
    }
    else if (RenderState == ECausticsBakeJobState::Complete && bPreviewJob)
    {
        if (ActiveRegion)
        {
            ActiveRegion->Modify();
            ActiveRegion->LastPreviewSignature = ActiveRegion->BuildBakeSignature();
            ActiveRegion->bPreviewOutOfDate = false;
        }
        FinishTerminalJob(ECausticsBakeJobState::Complete, LOCTEXT("PreviewComplete", "Caustics preview complete"));
    }
    else if (RenderState == ECausticsBakeJobState::Failed)
    {
        const FString Error = RenderJob->GetError();
        FinishTerminalJob(ECausticsBakeJobState::Failed,
            Error.IsEmpty() ? LOCTEXT("RenderFailed", "The caustics render job failed.") : FText::FromString(Error));
    }
    else if (RenderState == ECausticsBakeJobState::Cancelled)
    {
        FinishTerminalJob(ECausticsBakeJobState::Cancelled, LOCTEXT("Cancelled", "Caustics job cancelled"));
    }
}

UTexture2D* UCausticsBakerEditorSubsystem::CreateOrUpdateTexture(const TArray<FFloat16Color>& Pixels, FString& OutError)
{
    if (!ActiveRegion || !RenderJob.IsValid())
    {
        OutError = TEXT("The region or render job disappeared before asset creation.");
        return nullptr;
    }
    const FString PackageName = ActiveRegion->GetEffectiveOutputPackageName();
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Could not create package %s."), *PackageName);
        return nullptr;
    }
    UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
    const bool bNewTexture = Texture == nullptr;
    if (!Texture)
    {
        if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName))
        {
            OutError = FString::Printf(TEXT("%s already contains a non-Texture2D object named %s."), *PackageName, *AssetName);
            return nullptr;
        }
        Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    }
    Texture->Modify();
    Texture->PreEditChange(nullptr);
    if (ActiveRegion->Settings.OutputFormat == ECausticsOutputFormat::LDR8)
    {
        const TArray<FColor> LDRPixels = CausticsBaker::TextureOutput::ConvertToLDR8(
            Pixels, ActiveRegion->Settings.LDRWhiteLevel);
        Texture->Source.Init(RenderJob->Request.Resolution, RenderJob->Request.Resolution, 1, 1, TSF_BGRA8,
            reinterpret_cast<const uint8*>(LDRPixels.GetData()));
        Texture->CompressionSettings = TC_Default;
        Texture->SRGB = true;
    }
    else
    {
        Texture->Source.Init(RenderJob->Request.Resolution, RenderJob->Request.Resolution, 1, 1, TSF_RGBA16F,
            reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->CompressionSettings = TC_HDR;
        Texture->SRGB = false;
    }
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->LODGroup = TEXTUREGROUP_Effects;
    Texture->MipGenSettings = TMGS_FromTextureGroup;
    Texture->NeverStream = false;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();
    Package->MarkPackageDirty();
    if (bNewTexture)
    {
        FAssetRegistryModule::AssetCreated(Texture);
    }
    return Texture;
}

void UCausticsBakerEditorSubsystem::FinalizeSuccessfulBake(UTexture2D* Texture)
{
    check(ActiveRegion);
    ActiveRegion->Modify();
    ActiveRegion->OutputTexture = Texture;
    ActiveRegion->LastBakeSignature = ActiveRegion->BuildBakeSignature();
    ActiveRegion->bOutputOutOfDate = false;
    ActiveRegion->MarkPackageDirty();
}

void UCausticsBakerEditorSubsystem::FinishTerminalJob(const ECausticsBakeJobState TerminalState, const FText& Message)
{
    SetStatus(TerminalState, TerminalState == ECausticsBakeJobState::Complete ? 1.0f : Status.Progress, Message);
    DestroyCapture();
    if (RenderJob.IsValid())
    {
        GetCausticsBakerRenderManager().ForgetCompletedJob(RenderJob);
        RenderJob.Reset();
    }
    ActiveRegion = nullptr;
    ActiveSignature.Reset();
    PendingBakePixels.Reset();
    PendingGuideNormals.Reset();
    bCaptureSubmitted = false;
}

void UCausticsBakerEditorSubsystem::Cancel()
{
    if (!RenderJob.IsValid()) return;
    RenderJob->bCancelRequested.Store(true);
    GetCausticsBakerRenderManager().Cancel();
    SetStatus(RenderJob->Stage.Load(), Status.Progress,
        LOCTEXT("CancelRequested", "Cancel requested; the active GPU batch will finish first"), true);
    // Readback has no further capture to drive the cancel path, so polling performs the ordered cleanup.
    if (RenderJob->Stage.Load() == ECausticsBakeJobState::Readback)
    {
        GetCausticsBakerRenderManager().PollReadback(RenderJob);
    }
}

void UCausticsBakerEditorSubsystem::ClearPreview(ACausticsBakeRegion* Region)
{
    GetCausticsBakerRenderManager().ClearPreview(Region ? Region->GetUniqueID() : 0u);
    if (Region)
    {
        Region->Modify();
        Region->LastPreviewSignature.Reset();
        Region->bPreviewOutOfDate = true;
    }
}

void UCausticsBakerEditorSubsystem::DestroyCapture()
{
    SceneCapture = nullptr;
    if (CaptureOwner)
    {
        UWorld* World = CaptureOwner->GetWorld();
        if (World) World->DestroyActor(CaptureOwner, true);
        CaptureOwner = nullptr;
    }
}

void UCausticsBakerEditorSubsystem::NotifyRegionDestroyed(const ACausticsBakeRegion* Region)
{
    GetCausticsBakerRenderManager().ClearPreview(Region ? Region->GetUniqueID() : 0u);
    if (ActiveRegion == Region) Cancel();
}

void UCausticsBakerEditorSubsystem::NotifyRegionChanged(const ACausticsBakeRegion* Region)
{
    if (ActiveRegion == Region) Cancel();
}

void UCausticsBakerEditorSubsystem::OnMapChanged(uint32)
{
    Cancel();
    ClearPreview();
}

#undef LOCTEXT_NAMESPACE
