#include "CausticsBakeRegion.h"

#include "ComponentVisualizer.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "SceneManagement.h"

class FCausticsBakeRegionVisualizer final : public FComponentVisualizer
{
public:
    virtual void DrawVisualization(const UActorComponent* Component, const FSceneView*, FPrimitiveDrawInterface* PDI) override
    {
        const UBoxComponent* BoxComponent = Cast<UBoxComponent>(Component);
        const ACausticsBakeRegion* Region = BoxComponent ? Cast<ACausticsBakeRegion>(BoxComponent->GetOwner()) : nullptr;
        if (!Region || Region->ProjectionBox != BoxComponent) return;

        const FTransform ActorTransform = Region->GetActorTransform();
        const FBox LocalBox(FVector(0.0, -Region->Width * 0.5, -Region->Height * 0.5),
            FVector(Region->Depth, Region->Width * 0.5, Region->Height * 0.5));
        DrawWireBox(PDI, ActorTransform.ToMatrixWithScale(), LocalBox, FColor(45, 210, 255), SDPG_World, 2.0f);

        const FVector Origin = ActorTransform.TransformPosition(FVector::ZeroVector);
        const FVector End = ActorTransform.TransformPosition(FVector(FMath::Min(Region->Depth, 250.0f), 0.0, 0.0));
        PDI->DrawLine(Origin, End, FColor(255, 180, 40), SDPG_Foreground, 3.0f);
        const FVector Direction = (End - Origin).GetSafeNormal();
        FVector Side, Up;
        Direction.FindBestAxisVectors(Side, Up);
        const float Head = 20.0f;
        PDI->DrawLine(End, End - Direction * Head + Side * Head * 0.5f, FColor(255, 180, 40), SDPG_Foreground, 3.0f);
        PDI->DrawLine(End, End - Direction * Head - Side * Head * 0.5f, FColor(255, 180, 40), SDPG_Foreground, 3.0f);

        for (const FCausticsCasterEntry& Caster : Region->Casters)
        {
            if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Caster.Component.GetComponent(const_cast<ACausticsBakeRegion*>(Region))))
            {
                DrawWireBox(PDI, FMatrix::Identity, Primitive->Bounds.GetBox(), FColor(255, 120, 35), SDPG_World, 1.5f);
            }
        }
        // In automatic mode the cyan projection box is the receiver scope.
        // Avoid scanning and drawing every candidate on every viewport frame;
        // explicit filters remain useful to visualize individually in green.
        if (!Region->Receivers.IsEmpty())
        {
            TArray<UPrimitiveComponent*> ResolvedReceivers;
            Region->ResolveReceiverComponents(ResolvedReceivers);
            for (const UPrimitiveComponent* Primitive : ResolvedReceivers)
            {
                DrawWireBox(PDI, FMatrix::Identity, Primitive->Bounds.GetBox(), FColor(60, 255, 110), SDPG_World, 1.5f);
            }
        }
    }
};

TSharedRef<FComponentVisualizer> MakeCausticsBakeRegionVisualizer()
{
    return MakeShared<FCausticsBakeRegionVisualizer>();
}
