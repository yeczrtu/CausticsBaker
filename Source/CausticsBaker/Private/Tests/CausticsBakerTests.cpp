#include "CausticsBakerMath.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CausticsBakeTypes.h"
#include "CausticsBakeRegion.h"
#include "CausticsBakerEditorSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UObject/Package.h"

namespace
{
    class FCausticsGpuBakeWaitCommand final : public IAutomationLatentCommand
    {
    public:
        FCausticsGpuBakeWaitCommand(FAutomationTestBase* InTest,
            UCausticsBakerEditorSubsystem* InSubsystem, ACausticsBakeRegion* InRegion,
            TArray<TWeakObjectPtr<AActor>>&& InActors, const double InDeadline)
            : Test(InTest)
            , Subsystem(InSubsystem)
            , Region(InRegion)
            , Actors(MoveTemp(InActors))
            , Deadline(InDeadline)
        {
        }

        virtual bool Update() override
        {
            UCausticsBakerEditorSubsystem* Baker = Subsystem.Get();
            ACausticsBakeRegion* BakeRegion = Region.Get();
            if (!Baker || !BakeRegion)
            {
                Test->AddError(TEXT("The GPU smoke-test subsystem or region was destroyed while the bake was running."));
                Cleanup();
                return true;
            }

            const FCausticsJobStatus Status = Baker->GetStatus();
            if (bWaitingForPreview)
            {
                if (Status.State == ECausticsBakeJobState::Failed ||
                    Status.State == ECausticsBakeJobState::Cancelled)
                {
                    Test->AddError(FString::Printf(TEXT("GPU preview ended in %s: %s"),
                        *CausticsBaker::JobStateToText(Status.State).ToString(), *Status.Message.ToString()));
                    Cleanup();
                    return true;
                }
                if (Status.State == ECausticsBakeJobState::Complete)
                {
                    // Keep several editor frames between jobs. A validated
                    // preview must remain current instead of being released on
                    // the first frame after its temporary capture is destroyed.
                    if (++PreviewHoldFrames < 5)
                    {
                        return false;
                    }
                    Test->TestFalse(TEXT("Validated preview remains current"), BakeRegion->bPreviewOutOfDate);
                    Test->TestFalse(TEXT("Validated preview has a signature"), BakeRegion->LastPreviewSignature.IsEmpty());
                    if (!Baker->RequestBake(BakeRegion))
                    {
                        Test->AddError(FString::Printf(TEXT("Could not start GPU bake after persistent preview: %s"),
                            *Baker->GetStatus().Message.ToString()));
                        Cleanup();
                        return true;
                    }
                    bWaitingForPreview = false;
                    return false;
                }
            }
            if (Status.State == ECausticsBakeJobState::Complete ||
                Status.State == ECausticsBakeJobState::Failed ||
                Status.State == ECausticsBakeJobState::Cancelled)
            {
                if (Status.State != ECausticsBakeJobState::Complete)
                {
                    Test->AddError(FString::Printf(TEXT("GPU bake ended in %s: %s"),
                        *CausticsBaker::JobStateToText(Status.State).ToString(), *Status.Message.ToString()));
                }
                else
                {
                    UTexture2D* Texture = BakeRegion->OutputTexture;
                    Test->TestNotNull(TEXT("GPU bake created an output texture"), Texture);
                    if (Texture)
                    {
                        Test->TestEqual(TEXT("GPU output width"), Texture->Source.GetSizeX(), static_cast<int64>(256));
                        Test->TestEqual(TEXT("GPU output height"), Texture->Source.GetSizeY(), static_cast<int64>(256));
                        Test->TestEqual(TEXT("GPU output source format"), Texture->Source.GetFormat(), TSF_RGBA16F);
                        Test->TestEqual(TEXT("GPU output compression"), Texture->CompressionSettings, TC_HDR);
                        Test->TestFalse(TEXT("GPU output is linear"), Texture->SRGB);

                        bool bHasReceiverCoverage = false;
                        bool bRadianceIsFinite = true;
                        float MaxRadiance = 0.0f;
                        double TotalRadiance = 0.0;
                        double TotalLuminance = 0.0;
                        double FocusLuminance = 0.0;
                        double WeightedX = 0.0;
                        double WeightedY = 0.0;
                        float MaxLuminance = 0.0f;
                        FIntPoint MaxLuminancePixel = FIntPoint::ZeroValue;
                        TArray<float> LuminancePixels;
                        LuminancePixels.SetNumZeroed(256 * 256);
                        if (const uint8* MipData = Texture->Source.LockMipReadOnly(0))
                        {
                            const FFloat16Color* Pixels = reinterpret_cast<const FFloat16Color*>(MipData);
                            for (int32 Index = 0; Index < 256 * 256; ++Index)
                            {
                                const float R = Pixels[Index].R.GetFloat();
                                const float G = Pixels[Index].G.GetFloat();
                                const float B = Pixels[Index].B.GetFloat();
                                bRadianceIsFinite &= FMath::IsFinite(R) && FMath::IsFinite(G) && FMath::IsFinite(B);
                                if (FMath::IsFinite(R) && FMath::IsFinite(G) && FMath::IsFinite(B))
                                {
                                    MaxRadiance = FMath::Max(MaxRadiance, FMath::Max3(R, G, B));
                                    TotalRadiance += FMath::Max(0.0f, R) + FMath::Max(0.0f, G) + FMath::Max(0.0f, B);
                                    const double Luminance = FMath::Max(0.0f, 0.2126f * R + 0.7152f * G + 0.0722f * B);
                                    LuminancePixels[Index] = static_cast<float>(Luminance);
                                    TotalLuminance += Luminance;
                                    const int32 X = Index % 256;
                                    const int32 Y = Index / 256;
                                    WeightedX += X * Luminance;
                                    WeightedY += Y * Luminance;
                                    if (Luminance > MaxLuminance)
                                    {
                                        MaxLuminance = static_cast<float>(Luminance);
                                        MaxLuminancePixel = FIntPoint(X, Y);
                                    }
                                    if (FMath::Square(X - 127.5f) + FMath::Square(Y - 127.5f) <= FMath::Square(8.0f))
                                    {
                                        FocusLuminance += Luminance;
                                    }
                                }
                                if (Pixels[Index].A.GetFloat() > 0.0f)
                                {
                                    bHasReceiverCoverage = true;
                                }
                            }
                            Texture->Source.UnlockMip(0);
                        }
                        Test->TestTrue(TEXT("Receiver guide produced nonzero coverage"), bHasReceiverCoverage);
                        Test->TestTrue(TEXT("GPU caustics radiance is finite"), bRadianceIsFinite);
                        Test->TestTrue(TEXT("GPU photon mapping produced nonzero caustics radiance"),
                            MaxRadiance > 1.0e-8f && TotalRadiance > 1.0e-6);
                        const double FocusFraction = TotalLuminance > 0.0 ? FocusLuminance / TotalLuminance : 0.0;
                        double PeakNeighborhoodLuminance = 0.0;
                        for (int32 Y = FMath::Max(0, MaxLuminancePixel.Y - 8); Y <= FMath::Min(255, MaxLuminancePixel.Y + 8); ++Y)
                        {
                            for (int32 X = FMath::Max(0, MaxLuminancePixel.X - 8); X <= FMath::Min(255, MaxLuminancePixel.X + 8); ++X)
                            {
                                if (FMath::Square(X - MaxLuminancePixel.X) + FMath::Square(Y - MaxLuminancePixel.Y) <= FMath::Square(8))
                                {
                                    PeakNeighborhoodLuminance += LuminancePixels[Y * 256 + X];
                                }
                            }
                        }
                        const double PeakNeighborhoodFraction = TotalLuminance > 0.0
                            ? PeakNeighborhoodLuminance / TotalLuminance : 0.0;
                        const FVector2D LuminanceCentroid = TotalLuminance > 0.0
                            ? FVector2D(WeightedX / TotalLuminance, WeightedY / TotalLuminance)
                            : FVector2D::ZeroVector;
                        Test->AddInfo(FString::Printf(
                            TEXT("Polished glass: center8=%.6f, peak8=%.6f, peak=(%d,%d), centroid=(%.2f,%.2f), max=%.6g, total=%.6g"),
                            FocusFraction, PeakNeighborhoodFraction, MaxLuminancePixel.X, MaxLuminancePixel.Y,
                            LuminanceCentroid.X, LuminanceCentroid.Y, MaxLuminance, TotalLuminance));
                        Test->TestTrue(TEXT("Polished solid glass concentrates photons near its optical focus"),
                            PeakNeighborhoodFraction >= 0.2);
                    }
                }
                Cleanup();
                return true;
            }

            if (FPlatformTime::Seconds() >= Deadline)
            {
                Test->AddError(FString::Printf(TEXT("GPU bake timed out in %s: %s"),
                    *CausticsBaker::JobStateToText(Status.State).ToString(), *Status.Message.ToString()));
                Baker->Cancel();
                FlushRenderingCommands();
                Cleanup();
                return true;
            }
            return false;
        }

    private:
        void Cleanup()
        {
            if (UCausticsBakerEditorSubsystem* Baker = Subsystem.Get())
            {
                Baker->ClearPreview(Region.Get());
            }
            if (ACausticsBakeRegion* BakeRegion = Region.Get())
            {
                if (UTexture2D* Texture = BakeRegion->OutputTexture)
                {
                    if (UPackage* Package = Texture->GetOutermost())
                    {
                        Package->SetDirtyFlag(false);
                    }
                    BakeRegion->OutputTexture = nullptr;
                }
            }
            for (const TWeakObjectPtr<AActor>& Actor : Actors)
            {
                if (AActor* ActorPtr = Actor.Get())
                {
                    if (UWorld* World = ActorPtr->GetWorld())
                    {
                        World->DestroyActor(ActorPtr, true);
                    }
                }
            }
            Actors.Reset();
        }

        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UCausticsBakerEditorSubsystem> Subsystem;
        TWeakObjectPtr<ACausticsBakeRegion> Region;
        TArray<TWeakObjectPtr<AActor>> Actors;
        double Deadline = 0.0;
        bool bWaitingForPreview = true;
        int32 PreviewHoldFrames = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsProjectionCoordinatesTest,
    "CausticsBaker.Coordinates.ProjectionUV", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsProjectionCoordinatesTest::RunTest(const FString&)
{
    const FVector3f Size(100.0f, 200.0f, 400.0f);
    TestEqual(TEXT("-Y is U=0"), CausticsBaker::Math::LocalPositionToProjectionUV(FVector3f(0, -100, 0), Size).X, 0.0f);
    TestEqual(TEXT("+Y is U=1"), CausticsBaker::Math::LocalPositionToProjectionUV(FVector3f(0, 100, 0), Size).X, 1.0f);
    TestEqual(TEXT("+Z is V=0"), CausticsBaker::Math::LocalPositionToProjectionUV(FVector3f(0, 0, 200), Size).Y, 0.0f);
    TestEqual(TEXT("-Z is V=1"), CausticsBaker::Math::LocalPositionToProjectionUV(FVector3f(0, 0, -200), Size).Y, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsOpticsTest,
    "CausticsBaker.Optics.SnellFresnelTIR", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsOpticsTest::RunTest(const FString&)
{
    TestTrue(TEXT("Air/glass normal-incidence Fresnel is 4%"),
        FMath::IsNearlyEqual(CausticsBaker::Math::FresnelDielectric(1.0f, 1.0f, 1.5f), 0.04f, 1.0e-4f));
    const float IncidentAngle = FMath::DegreesToRadians(30.0f);
    const FVector3f Incident(FMath::Sin(IncidentAngle), 0.0f, -FMath::Cos(IncidentAngle));
    FVector3f Refracted;
    TestTrue(TEXT("Air to glass refracts"), CausticsBaker::Math::Refract(Incident, FVector3f(0, 0, 1), 1.0f, 1.5f, Refracted));
    const float RefractedAngle = FMath::Acos(FMath::Clamp(-Refracted.Z, -1.0f, 1.0f));
    TestTrue(TEXT("Snell angle"), FMath::IsNearlyEqual(RefractedAngle, FMath::Asin(FMath::Sin(IncidentAngle) / 1.5f), 1.0e-4f));
    const float TIRAngle = FMath::DegreesToRadians(50.0f);
    TestFalse(TEXT("Glass to air above critical angle totally internally reflects"),
        CausticsBaker::Math::Refract(FVector3f(FMath::Sin(TIRAngle), 0, -FMath::Cos(TIRAngle)),
            FVector3f(0, 0, 1), 1.5f, 1.0f, Refracted));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsSPPMTest,
    "CausticsBaker.Density.SPPMConvergence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsSPPMTest::RunTest(const FString&)
{
    const auto First = CausticsBaker::Math::UpdateSPPM(0.0f, 3.0f, FVector3f::ZeroVector,
        10.0f, FVector3f(10.0f), 0.7f);
    TestTrue(TEXT("Radius shrinks by sqrt(alpha) on first populated iteration"),
        FMath::IsNearlyEqual(First.Radius, 3.0f * FMath::Sqrt(0.7f), 1.0e-5f));
    TestEqual(TEXT("Effective hit count"), First.HitCount, 7.0f);
    const auto Empty = CausticsBaker::Math::UpdateSPPM(First.HitCount, First.Radius, First.Tau,
        0.0f, FVector3f::ZeroVector, 0.7f);
    TestTrue(TEXT("Empty iteration preserves radius"), FMath::IsNearlyEqual(Empty.Radius, First.Radius));
    TestTrue(TEXT("Tau remains finite and nonnegative"), Empty.Tau.X >= 0.0f && FMath::IsFinite(Empty.Tau.X));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsPrimitiveIdTest,
    "CausticsBaker.Scene.PrimitiveIdLookup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsPrimitiveIdTest::RunTest(const FString&)
{
    const TArray<uint32> Ids { 2u, 11u, 19u, 1001u };
    TestEqual(TEXT("Find first"), CausticsBaker::Math::FindSortedPrimitiveId(Ids, 2), 0);
    TestEqual(TEXT("Find middle"), CausticsBaker::Math::FindSortedPrimitiveId(Ids, 19), 2);
    TestEqual(TEXT("Reject missing"), CausticsBaker::Math::FindSortedPrimitiveId(Ids, 20), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsSettingsAndStateTest,
    "CausticsBaker.Job.PresetsAndTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsSettingsAndStateTest::RunTest(const FString&)
{
    FCausticsBakeSettings Settings;
    int32 Resolution, Batches, Photons, Bounces, Atrous;
    Settings.Resolve(true, Resolution, Batches, Photons, Bounces, Atrous);
    TestEqual(TEXT("Preview resolution"), Resolution, 512);
    TestEqual(TEXT("Preview batches"), Batches, 8);
    TestEqual(TEXT("Preview photons"), Photons, 131072);
    TestEqual(TEXT("Preview bounces"), Bounces, 6);
    TestEqual(TEXT("Preview a-trous"), Atrous, 2);

    Settings.Preset = ECausticsQualityPreset::Custom;
    Settings.Resolution = 1000;
    Settings.PhotonBatches = 3;
    Settings.PhotonsPerBatch = 4096;
    Settings.MaxBounces = 5;
    Settings.AtrousIterations = 1;
    Settings.Resolve(true, Resolution, Batches, Photons, Bounces, Atrous);
    TestEqual(TEXT("Custom preview resolution rounds to a power of two"), Resolution, 1024);
    TestEqual(TEXT("Custom preview batches"), Batches, 3);
    TestEqual(TEXT("Custom preview photons"), Photons, 4096);
    TestEqual(TEXT("Custom preview bounces"), Bounces, 5);
    TestEqual(TEXT("Custom preview a-trous"), Atrous, 1);

    Settings.Resolve(false, Resolution, Batches, Photons, Bounces, Atrous);
    TestEqual(TEXT("Custom bake batches"), Batches, 3);
    TestEqual(TEXT("Custom bake photons"), Photons, 4096);
    TestTrue(TEXT("Validating to guide"), CausticsBaker::Math::IsValidJobTransition(
        ECausticsBakeJobState::Validating, ECausticsBakeJobState::BuildingGuide));
    TestTrue(TEXT("Tracing to cancellation"), CausticsBaker::Math::IsValidJobTransition(
        ECausticsBakeJobState::TracingPhotons, ECausticsBakeJobState::Cancelled));
    TestTrue(TEXT("Validated preview readback can complete"), CausticsBaker::Math::IsValidJobTransition(
        ECausticsBakeJobState::Readback, ECausticsBakeJobState::Complete));
    TestFalse(TEXT("Cannot skip guide"), CausticsBaker::Math::IsValidJobTransition(
        ECausticsBakeJobState::Validating, ECausticsBakeJobState::Filtering));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsTextureSettingsTest,
    "CausticsBaker.Asset.HDRTextureSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCausticsTextureSettingsTest::RunTest(const FString&)
{
    UTexture2D* Texture = NewObject<UTexture2D>();
    TArray<FFloat16Color> Pixels;
    Pixels.Init(FFloat16Color(FLinearColor(2.0f, 1.0f, 0.5f, 0.25f)), 16);
    Texture->Source.Init(4, 4, 1, 1, TSF_RGBA16F, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->CompressionSettings = TC_HDR;
    Texture->SRGB = false;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    TestEqual(TEXT("Source format"), Texture->Source.GetFormat(), TSF_RGBA16F);
    TestEqual(TEXT("HDR compression"), Texture->CompressionSettings, TC_HDR);
    TestFalse(TEXT("Linear texture"), Texture->SRGB);
    TestEqual(TEXT("Clamp X"), Texture->AddressX, TA_Clamp);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCausticsGpuBakeSmokeTest,
    "CausticsBaker.GPU.EndToEndBake", EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::ProductFilter)
bool FCausticsGpuBakeSmokeTest::RunTest(const FString&)
{
    if (!GEditor || !GDynamicRHI || !GRHISupportsRayTracing || !GRHISupportsRayTracingShaders ||
        !IsRayTracingEnabled() || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM6)
    {
        AddWarning(TEXT("GPU end-to-end bake was skipped because D3D12/SM6 full-pipeline hardware ray tracing is unavailable."));
        return true;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World || World->WorldType != EWorldType::Editor)
    {
        AddError(TEXT("GPU smoke test requires the current Editor world."));
        return false;
    }

    UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* ReceiverMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!SphereMesh || !ReceiverMesh)
    {
        AddError(TEXT("Could not load the engine Sphere and Cube meshes for the GPU smoke test."));
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    SpawnParameters.bHideFromSceneOutliner = true;

    const FTransform RegionTransform(FRotator(-7.0, 37.0, 4.0), FVector(1200.0, -800.0, 350.0));
    AStaticMeshActor* CasterActor = World->SpawnActor<AStaticMeshActor>(
        RegionTransform.TransformPosition(FVector(150.0, 0.0, 0.0)), RegionTransform.Rotator(), SpawnParameters);
    AStaticMeshActor* ReceiverActor = World->SpawnActor<AStaticMeshActor>(
        // The engine sphere has a 50 cm radius.  For IOR 1.5 its paraxial
        // back focal length is approximately 25 cm beyond the rear surface.
        RegionTransform.TransformPosition(FVector(230.0, 0.0, 0.0)), RegionTransform.Rotator(), SpawnParameters);
    ADirectionalLight* LightActor = World->SpawnActor<ADirectionalLight>(
        RegionTransform.TransformPosition(FVector(-200.0, 0.0, 0.0)), RegionTransform.Rotator(), SpawnParameters);
    ACausticsBakeRegion* Region = World->SpawnActor<ACausticsBakeRegion>(
        RegionTransform.GetLocation(), RegionTransform.Rotator(), SpawnParameters);
    if (!CasterActor || !ReceiverActor || !LightActor || !Region)
    {
        AddError(TEXT("Could not spawn the GPU smoke-test actors."));
        if (CasterActor) World->DestroyActor(CasterActor, true);
        if (ReceiverActor) World->DestroyActor(ReceiverActor, true);
        if (LightActor) World->DestroyActor(LightActor, true);
        if (Region) World->DestroyActor(Region, true);
        return false;
    }

    UStaticMeshComponent* CasterComponent = CasterActor->GetStaticMeshComponent();
    UStaticMeshComponent* ReceiverComponent = ReceiverActor->GetStaticMeshComponent();
    CasterComponent->SetStaticMesh(SphereMesh);
    ReceiverComponent->SetStaticMesh(ReceiverMesh);
    ReceiverActor->SetActorScale3D(FVector(0.1, 5.0, 5.0));
    CasterComponent->bVisibleInRayTracing = true;
    ReceiverComponent->bVisibleInRayTracing = true;
    LightActor->GetLightComponent()->SetIntensity(10.0f);

    Region->Depth = 500.0f;
    Region->Width = 500.0f;
    Region->Height = 500.0f;
    Region->RefreshVisualization();
    Region->LightActor = LightActor;

    FCausticsCasterEntry CasterEntry;
    CasterEntry.Component.OverrideComponent = CasterComponent;
    // Exercise the deterministic explicit glass path.  In particular this
    // verifies that Substrate projects do not replace the requested optical
    // roughness or closed-volume setting with material top-surface values.
    CasterEntry.OpticalMode = ECausticsOpticalMode::DielectricOverride;
    CasterEntry.ThicknessMode = ECausticsThicknessMode::Solid;
    CasterEntry.IndexOfRefraction = 1.5f;
    CasterEntry.Roughness = 0.001f;
    Region->Casters.Add(CasterEntry);

    Region->Settings.Preset = ECausticsQualityPreset::Custom;
    Region->Settings.Resolution = 256;
    Region->Settings.PhotonBatches = 4;
    Region->Settings.PhotonsPerBatch = 16384;
    Region->Settings.MaxBounces = 4;
    Region->Settings.AtrousIterations = 0;
    Region->Settings.InitialRadiusTexels = 1.0f;
    Region->Settings.Denoiser = ECausticsDenoiser::None;
    Region->OutputDirectory.Path = TEXT("/Game/__CausticsBakerTests");
    Region->OutputTextureName = FString::Printf(TEXT("T_GpuSmoke_%u"), Region->GetUniqueID());

    CasterComponent->MarkRenderStateDirty();
    ReceiverComponent->MarkRenderStateDirty();
    FlushRenderingCommands();

    TestEqual(TEXT("Light actor resolves its light component"), Region->ResolveLight(), LightActor->GetLightComponent());
    TArray<UPrimitiveComponent*> AutoReceivers;
    Region->ResolveReceiverComponents(AutoReceivers);
    TestTrue(TEXT("Empty receiver filter auto-detects the projected receiver"), AutoReceivers.Contains(ReceiverComponent));
    TestFalse(TEXT("Automatic receiver detection excludes an assigned caster"), AutoReceivers.Contains(CasterComponent));

    // Reproduce an editor setup where the user explicitly chooses a receiver
    // just beyond the visible box. Starting the job must fit Depth before the
    // guide is dispatched instead of producing a zero-coverage GPU readback.
    Region->Depth = 200.0f;
    Region->RefreshVisualization();
    FComponentReference ReceiverReference;
    ReceiverReference.OverrideComponent = ReceiverComponent;
    Region->Receivers.Add(ReceiverReference);

    UCausticsBakerEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>();
    if (!Subsystem || !Subsystem->RequestPreview(Region))
    {
        const FString Reason = Subsystem ? Subsystem->GetStatus().Message.ToString() : TEXT("Subsystem unavailable");
        AddError(FString::Printf(TEXT("Could not start GPU smoke preview: %s"), *Reason));
        World->DestroyActor(Region, true);
        World->DestroyActor(CasterActor, true);
        World->DestroyActor(ReceiverActor, true);
        World->DestroyActor(LightActor, true);
        return false;
    }
    TestTrue(TEXT("Filtered receiver beyond Depth is auto-fitted before rendering"), Region->Depth > 230.0f);

    TArray<TWeakObjectPtr<AActor>> Actors;
    Actors.Add(Region);
    Actors.Add(CasterActor);
    Actors.Add(ReceiverActor);
    Actors.Add(LightActor);
    ADD_LATENT_AUTOMATION_COMMAND(FCausticsGpuBakeWaitCommand(
        this, Subsystem, Region, MoveTemp(Actors), FPlatformTime::Seconds() + 180.0));
    return true;
}

#endif
