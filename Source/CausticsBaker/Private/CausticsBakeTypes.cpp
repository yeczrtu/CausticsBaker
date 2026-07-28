#include "CausticsBakeTypes.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"

bool CausticsBaker::IsSupportedPrimitiveComponent(const UPrimitiveComponent* Component)
{
    return Component && (Component->IsA<UStaticMeshComponent>() ||
        Component->IsA<UInstancedStaticMeshComponent>() || Component->IsA<UHierarchicalInstancedStaticMeshComponent>());
}

FText CausticsBaker::JobStateToText(const ECausticsBakeJobState State)
{
    switch (State)
    {
    case ECausticsBakeJobState::Idle: return NSLOCTEXT("CausticsBaker", "Idle", "Idle");
    case ECausticsBakeJobState::Validating: return NSLOCTEXT("CausticsBaker", "Validating", "Validating");
    case ECausticsBakeJobState::BuildingGuide: return NSLOCTEXT("CausticsBaker", "BuildingGuide", "Building receiver guide");
    case ECausticsBakeJobState::TracingPhotons: return NSLOCTEXT("CausticsBaker", "TracingPhotons", "Tracing photons");
    case ECausticsBakeJobState::Filtering: return NSLOCTEXT("CausticsBaker", "Filtering", "Density estimation and filtering");
    case ECausticsBakeJobState::Readback: return NSLOCTEXT("CausticsBaker", "Readback", "Reading GPU result");
    case ECausticsBakeJobState::Saving: return NSLOCTEXT("CausticsBaker", "Saving", "Creating texture asset");
    case ECausticsBakeJobState::Complete: return NSLOCTEXT("CausticsBaker", "Complete", "Complete");
    case ECausticsBakeJobState::Failed: return NSLOCTEXT("CausticsBaker", "Failed", "Failed");
    case ECausticsBakeJobState::Cancelled: return NSLOCTEXT("CausticsBaker", "Cancelled", "Cancelled");
    default: return FText::GetEmpty();
    }
}
