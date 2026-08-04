#pragma once

#include "CoreMinimal.h"

enum class ECausticsBakeJobState : uint8;

namespace CausticsBaker::Math
{
    struct FSPPMUpdate
    {
        float HitCount = 0.0f;
        float Radius = 0.0f;
        FVector3f Tau = FVector3f::ZeroVector;
    };

    CAUSTICSBAKER_API FVector2f LocalPositionToProjectionUV(const FVector3f& LocalPosition, const FVector3f& RegionSize);
    CAUSTICSBAKER_API float FresnelDielectric(float CosThetaI, float EtaI, float EtaT);
    CAUSTICSBAKER_API float RefractiveIndexAtWavelength(float ReferenceIOR, float AbbeNumber, float WavelengthNm);
    CAUSTICSBAKER_API uint32 PhotonRecordCountPerBatch(uint32 PhotonsPerWavelength, bool bUseDispersion);
    CAUSTICSBAKER_API uint32 PhotonNormalizationCountPerBatch(uint32 PhotonsPerWavelength);
    CAUSTICSBAKER_API bool Refract(const FVector3f& Incident, const FVector3f& Normal, float EtaI, float EtaT,
        FVector3f& OutDirection);
    CAUSTICSBAKER_API FSPPMUpdate UpdateSPPM(float OldHitCount, float OldRadius, const FVector3f& OldTau,
        float BatchHitCount, const FVector3f& BatchFlux, float Alpha);
    CAUSTICSBAKER_API int32 FindSortedPrimitiveId(TConstArrayView<uint32> SortedIds, uint32 PrimitiveId);
    CAUSTICSBAKER_API bool IsValidJobTransition(ECausticsBakeJobState From, ECausticsBakeJobState To);
}
