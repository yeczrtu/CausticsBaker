#include "CausticsBakeRegion.h"

#include "CausticsBakerEditorSubsystem.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Light.h"
#include "EngineUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"

ACausticsBakeRegion::ACausticsBakeRegion()
{
    bIsEditorOnlyActor = true;
    PrimaryActorTick.bCanEverTick = false;
    SetActorEnableCollision(false);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    ProjectionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ProjectionBox"));
    ProjectionBox->SetupAttachment(SceneRoot);
    ProjectionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ProjectionBox->SetGenerateOverlapEvents(false);
    ProjectionBox->SetCanEverAffectNavigation(false);
    ProjectionBox->SetHiddenInGame(true);
    ProjectionBox->ShapeColor = FColor(45, 210, 255);

    ProjectionDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("ProjectionDirection"));
    ProjectionDirection->SetupAttachment(SceneRoot);
    ProjectionDirection->ArrowColor = FColor(255, 180, 40);
    ProjectionDirection->ArrowSize = 1.5f;
    ProjectionDirection->bIsScreenSizeScaled = true;

    OutputDirectory.Path = TEXT("/Game/Caustics");
    RefreshVisualization();
}

void ACausticsBakeRegion::PostLoad()
{
    Super::PostLoad();
    if (!LightActor)
    {
        if (const ULightComponent* LegacyLight = Cast<ULightComponent>(Light.GetComponent(this)))
        {
            LightActor = Cast<ALight>(LegacyLight->GetOwner());
        }
    }
}

void ACausticsBakeRegion::RefreshVisualization()
{
    Depth = FMath::Max(1.0f, Depth);
    Width = FMath::Max(1.0f, Width);
    Height = FMath::Max(1.0f, Height);
    if (ProjectionBox)
    {
        ProjectionBox->SetRelativeLocation(FVector(Depth * 0.5f, 0.0f, 0.0f));
        ProjectionBox->SetBoxExtent(FVector(Depth * 0.5f, Width * 0.5f, Height * 0.5f));
    }
    if (ProjectionDirection)
    {
        ProjectionDirection->SetRelativeLocation(FVector::ZeroVector);
        ProjectionDirection->SetRelativeRotation(FRotator::ZeroRotator);
        ProjectionDirection->ArrowLength = FMath::Clamp(Depth * 0.25f, 50.0f, 300.0f);
    }
}

void ACausticsBakeRegion::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshVisualization();
    MarkResultsOutOfDate();
    if (GEditor)
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>())
        {
            Subsystem->NotifyRegionChanged(this);
        }
    }
}

void ACausticsBakeRegion::PostEditMove(const bool bFinished)
{
    Super::PostEditMove(bFinished);
    if (bFinished)
    {
        MarkResultsOutOfDate();
        if (GEditor)
        {
            if (UCausticsBakerEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>())
            {
                Subsystem->NotifyRegionChanged(this);
            }
        }
    }
}

void ACausticsBakeRegion::Destroyed()
{
    if (GEditor)
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>())
        {
            Subsystem->NotifyRegionDestroyed(this);
        }
    }
    Super::Destroyed();
}

void ACausticsBakeRegion::MarkResultsOutOfDate()
{
    bOutputOutOfDate = !LastBakeSignature.IsEmpty() && LastBakeSignature != BuildBakeSignature();
    bPreviewOutOfDate = !LastPreviewSignature.IsEmpty() && LastPreviewSignature != BuildBakeSignature();
    if (LastBakeSignature.IsEmpty())
    {
        bOutputOutOfDate = true;
    }
    if (LastPreviewSignature.IsEmpty())
    {
        bPreviewOutOfDate = true;
    }
}

ULightComponent* ACausticsBakeRegion::ResolveLight() const
{
    if (LightActor)
    {
        return LightActor->GetLightComponent();
    }
    return Cast<ULightComponent>(Light.GetComponent(const_cast<ACausticsBakeRegion*>(this)));
}

void ACausticsBakeRegion::ResolveReceiverComponents(TArray<UPrimitiveComponent*>& OutReceivers) const
{
    OutReceivers.Reset();

    // A non-empty array is an explicit filter. Validation reports bad or
    // duplicate references; this resolver only supplies usable components.
    if (!Receivers.IsEmpty())
    {
        for (const FComponentReference& Reference : Receivers)
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Reference.GetComponent(const_cast<ACausticsBakeRegion*>(this))))
            {
                OutReceivers.AddUnique(Primitive);
            }
        }
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    TSet<const UPrimitiveComponent*> CasterComponents;
    for (const FCausticsCasterEntry& Caster : Casters)
    {
        if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Caster.Component.GetComponent(const_cast<ACausticsBakeRegion*>(this))))
        {
            CasterComponents.Add(Primitive);
        }
    }

    const FTransform WorldToRegion = GetActorTransform().Inverse();
    const FBox RegionBox(FVector(0.0, -Width * 0.5, -Height * 0.5),
        FVector(Depth, Width * 0.5, Height * 0.5));

    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(*ActorIt);
        for (UPrimitiveComponent* Primitive : PrimitiveComponents)
        {
            if (!CausticsBaker::IsSupportedPrimitiveComponent(Primitive) ||
                CasterComponents.Contains(Primitive) || !Primitive->IsRegistered() ||
                !Primitive->bVisibleInRayTracing || !Primitive->GetSceneProxy())
            {
                continue;
            }

            const FBox WorldBounds = Primitive->Bounds.GetBox();
            FBox LocalBounds(ForceInit);
            for (int32 X = 0; X < 2; ++X)
            {
                for (int32 Y = 0; Y < 2; ++Y)
                {
                    for (int32 Z = 0; Z < 2; ++Z)
                    {
                        const FVector Corner(X ? WorldBounds.Max.X : WorldBounds.Min.X,
                            Y ? WorldBounds.Max.Y : WorldBounds.Min.Y,
                            Z ? WorldBounds.Max.Z : WorldBounds.Min.Z);
                        LocalBounds += WorldToRegion.TransformPosition(Corner);
                    }
                }
            }
            if (RegionBox.Intersect(LocalBounds))
            {
                OutReceivers.Add(Primitive);
            }
        }
    }

    OutReceivers.Sort([](const UPrimitiveComponent& A, const UPrimitiveComponent& B)
    {
        return A.GetPathName() < B.GetPathName();
    });
}

FString ACausticsBakeRegion::GetEffectiveOutputPackageName() const
{
    FString Directory = OutputDirectory.Path.IsEmpty() ? TEXT("/Game/Caustics") : OutputDirectory.Path;
    Directory.RemoveFromEnd(TEXT("/"));
    FString AssetName = OutputTextureName.IsEmpty() ? FString::Printf(TEXT("T_Caustics_%s"), *GetName()) : OutputTextureName;
    AssetName = ObjectTools::SanitizeObjectName(AssetName);
    return Directory / AssetName;
}

FString ACausticsBakeRegion::BuildBakeSignature() const
{
    FString Source;
    Source.Reserve(4096);
    Source += GetActorTransform().ToHumanReadableString();
    Source += FString::Printf(TEXT("|%.9g|%.9g|%.9g|%d|%d|%d|%d|%d|%d|%d|%.9g|%.9g|%.9g|%d|%d"),
        Depth, Width, Height, static_cast<int32>(Settings.Preset), Settings.Resolution, Settings.PhotonBatches,
        Settings.PhotonsPerBatch, Settings.MaxBounces, Settings.AtrousIterations, Settings.RandomSeed, Settings.SPPMConvergence,
        Settings.InitialRadiusTexels, Settings.FilterStrength, static_cast<int32>(Settings.Denoiser),
        static_cast<int32>(Settings.DebugDisplay));
    Source += bAutoFitDepthToReceiverFilter ? TEXT("|AutoFitDepth1") : TEXT("|AutoFitDepth0");

    const auto AppendComponent = [&Source](const UActorComponent* ActorComponent)
    {
        Source += TEXT("|");
        Source += GetPathNameSafe(ActorComponent);
        if (const USceneComponent* SceneComponent = Cast<USceneComponent>(ActorComponent))
        {
            Source += SceneComponent->GetComponentTransform().ToHumanReadableString();
        }
        if (const ULightComponent* LightComponent = Cast<ULightComponent>(ActorComponent))
        {
            Source += FString::Printf(TEXT("|%s|%.9g|%s"), *LightComponent->LightColor.ToString(),
                LightComponent->Intensity, *LightComponent->GetColoredLightBrightness().ToString());
            if (const UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(LightComponent))
            {
                Source += FString::Printf(TEXT("|%.9g"), Directional->LightSourceAngle);
            }
            if (const UPointLightComponent* Point = Cast<UPointLightComponent>(LightComponent))
            {
                Source += FString::Printf(TEXT("|%.9g|%.9g"), Point->SourceRadius, Point->AttenuationRadius);
            }
            if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComponent))
            {
                Source += FString::Printf(TEXT("|%.9g|%.9g"), Spot->InnerConeAngle, Spot->OuterConeAngle);
            }
        }
        if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(ActorComponent))
        {
            Source += FString::Printf(TEXT("|RT%d"), Primitive->bVisibleInRayTracing ? 1 : 0);
            for (int32 MaterialIndex = 0; MaterialIndex < Primitive->GetNumMaterials(); ++MaterialIndex)
            {
                if (const UMaterialInterface* Material = Primitive->GetMaterial(MaterialIndex))
                {
                    Source += Material->GetPathName();
                    Source += Material->GetLightingGuid().ToString(EGuidFormats::Digits);
                }
            }
            if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Primitive))
            {
                if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
                {
                    Source += StaticMesh->GetPathName();
                    Source += StaticMesh->GetLightingGuid().ToString(EGuidFormats::Digits);
                }
            }
            if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Primitive))
            {
                Source += FString::FromInt(Instanced->GetInstanceCount());
                for (int32 InstanceIndex = 0; InstanceIndex < Instanced->GetInstanceCount(); ++InstanceIndex)
                {
                    FTransform InstanceTransform;
                    if (Instanced->GetInstanceTransform(InstanceIndex, InstanceTransform, false))
                    {
                        Source += InstanceTransform.ToHumanReadableString();
                    }
                }
            }
        }
    };

    AppendComponent(ResolveLight());
    for (const FCausticsCasterEntry& Caster : Casters)
    {
        AppendComponent(Caster.Component.GetComponent(const_cast<ACausticsBakeRegion*>(this)));
        Source += FString::Printf(TEXT("|%d|%d|%.9g|%.9g|%s|%s|%.9g"), static_cast<int32>(Caster.OpticalMode),
            static_cast<int32>(Caster.ThicknessMode), Caster.IndexOfRefraction, Caster.Roughness,
            *Caster.Tint.ToString(), *Caster.Absorption.ToString(), Caster.ThinThicknessCm);
    }
    Source += Receivers.IsEmpty() ? TEXT("|AutoReceivers") : TEXT("|FilteredReceivers");
    TArray<UPrimitiveComponent*> ResolvedReceivers;
    ResolveReceiverComponents(ResolvedReceivers);
    for (const UPrimitiveComponent* Receiver : ResolvedReceivers)
    {
        AppendComponent(Receiver);
    }
    return FMD5::HashAnsiString(*Source);
}

FBox ACausticsBakeRegion::GetWorldProjectionBounds() const
{
    const FBox LocalBox(FVector(0.0f, -Width * 0.5f, -Height * 0.5f), FVector(Depth, Width * 0.5f, Height * 0.5f));
    return LocalBox.TransformBy(GetActorTransform());
}

void ACausticsBakeRegion::Preview()
{
    if (GEditor)
    {
        GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>()->RequestPreview(this);
    }
}

void ACausticsBakeRegion::Bake()
{
    if (GEditor)
    {
        GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>()->RequestBake(this);
    }
}

void ACausticsBakeRegion::Cancel()
{
    if (GEditor)
    {
        GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>()->Cancel();
    }
}

void ACausticsBakeRegion::ClearPreview()
{
    if (GEditor)
    {
        GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>()->ClearPreview(this);
    }
}

void ACausticsBakeRegion::OpenOutputTexture()
{
    if (OutputTexture && GEditor)
    {
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(OutputTexture);
    }
}
