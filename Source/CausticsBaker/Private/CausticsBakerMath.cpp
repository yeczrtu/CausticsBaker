#include "CausticsBakerMath.h"

#include "CausticsBakeTypes.h"

FVector2f CausticsBaker::Math::LocalPositionToProjectionUV(const FVector3f& LocalPosition, const FVector3f& RegionSize)
{
    return FVector2f(LocalPosition.Y / RegionSize.Y + 0.5f, 0.5f - LocalPosition.Z / RegionSize.Z);
}

float CausticsBaker::Math::FresnelDielectric(float CosThetaI, const float EtaI, const float EtaT)
{
    CosThetaI = FMath::Clamp(CosThetaI, -1.0f, 1.0f);
    const float SinThetaT = EtaI / EtaT * FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosThetaI * CosThetaI));
    if (SinThetaT >= 1.0f) return 1.0f;
    const float CosThetaT = FMath::Sqrt(FMath::Max(0.0f, 1.0f - SinThetaT * SinThetaT));
    CosThetaI = FMath::Abs(CosThetaI);
    const float Rs = (EtaT * CosThetaI - EtaI * CosThetaT) / (EtaT * CosThetaI + EtaI * CosThetaT);
    const float Rp = (EtaI * CosThetaI - EtaT * CosThetaT) / (EtaI * CosThetaI + EtaT * CosThetaT);
    return 0.5f * (Rs * Rs + Rp * Rp);
}

float CausticsBaker::Math::RefractiveIndexAtWavelength(const float ReferenceIOR, const float AbbeNumber,
    const float WavelengthNm)
{
    constexpr float LambdaD = 0.58756f;
    constexpr float LambdaF = 0.48613f;
    constexpr float LambdaC = 0.65627f;
    const float SafeIOR = FMath::Max(1.0f, ReferenceIOR);
    const float SafeAbbe = FMath::Max(5.0f, AbbeNumber);
    const float Lambda = FMath::Max(0.001f, WavelengthNm * 0.001f);
    const float PrincipalDispersionDenominator =
        1.0f / FMath::Square(LambdaF) - 1.0f / FMath::Square(LambdaC);
    const float CauchyB = (SafeIOR - 1.0f) / (SafeAbbe * PrincipalDispersionDenominator);
    return SafeIOR + CauchyB * (1.0f / FMath::Square(Lambda) - 1.0f / FMath::Square(LambdaD));
}

uint32 CausticsBaker::Math::PhotonRecordCountPerBatch(const uint32 PhotonsPerWavelength, const bool bUseDispersion)
{
    const uint64 Count = static_cast<uint64>(PhotonsPerWavelength) * (bUseDispersion ? 3ull : 1ull);
    return static_cast<uint32>(FMath::Min<uint64>(Count, MAX_uint32));
}

uint32 CausticsBaker::Math::PhotonNormalizationCountPerBatch(const uint32 PhotonsPerWavelength)
{
    // RGB paths each transport only one wavelength. Keep the configured
    // per-wavelength count as the energy denominator even when 3N records run.
    return FMath::Max(1u, PhotonsPerWavelength);
}

bool CausticsBaker::Math::Refract(const FVector3f& Incident, const FVector3f& Normal, const float EtaI,
    const float EtaT, FVector3f& OutDirection)
{
    const FVector3f I = Incident.GetSafeNormal();
    const FVector3f N = Normal.GetSafeNormal();
    const float Eta = EtaI / EtaT;
    const float CosI = -FVector3f::DotProduct(I, N);
    const float K = 1.0f - Eta * Eta * (1.0f - CosI * CosI);
    if (K < 0.0f)
    {
        OutDirection = FVector3f::ZeroVector;
        return false;
    }
    OutDirection = (Eta * I + (Eta * CosI - FMath::Sqrt(K)) * N).GetSafeNormal();
    return true;
}

CausticsBaker::Math::FSPPMUpdate CausticsBaker::Math::UpdateSPPM(const float OldHitCount, const float OldRadius,
    const FVector3f& OldTau, const float BatchHitCount, const FVector3f& BatchFlux, const float Alpha)
{
    FSPPMUpdate Result;
    Result.HitCount = OldHitCount + Alpha * BatchHitCount;
    const float Ratio = BatchHitCount > 0.0f
        ? (OldHitCount + Alpha * BatchHitCount) / FMath::Max(1.0e-6f, OldHitCount + BatchHitCount)
        : 1.0f;
    Result.Radius = OldRadius * FMath::Sqrt(FMath::Max(0.0f, Ratio));
    const float RadiusScale = FMath::Square(Result.Radius) / FMath::Max(1.0e-8f, FMath::Square(OldRadius));
    Result.Tau = (OldTau + BatchFlux) * RadiusScale;
    return Result;
}

int32 CausticsBaker::Math::FindSortedPrimitiveId(const TConstArrayView<uint32> SortedIds, const uint32 PrimitiveId)
{
    int32 Low = 0;
    int32 High = SortedIds.Num() - 1;
    while (Low <= High)
    {
        const int32 Mid = (Low + High) >> 1;
        if (SortedIds[Mid] == PrimitiveId) return Mid;
        if (SortedIds[Mid] < PrimitiveId) Low = Mid + 1; else High = Mid - 1;
    }
    return INDEX_NONE;
}

bool CausticsBaker::Math::IsValidJobTransition(const ECausticsBakeJobState From, const ECausticsBakeJobState To)
{
    if (From == To) return true;
    if ((From == ECausticsBakeJobState::Idle || From == ECausticsBakeJobState::Complete ||
        From == ECausticsBakeJobState::Failed || From == ECausticsBakeJobState::Cancelled) &&
        To == ECausticsBakeJobState::Validating) return true;
    if (To == ECausticsBakeJobState::Failed || To == ECausticsBakeJobState::Cancelled) return From != ECausticsBakeJobState::Idle;
    switch (From)
    {
    case ECausticsBakeJobState::Validating: return To == ECausticsBakeJobState::BuildingGuide;
    case ECausticsBakeJobState::BuildingGuide: return To == ECausticsBakeJobState::TracingPhotons;
    case ECausticsBakeJobState::TracingPhotons: return To == ECausticsBakeJobState::Filtering;
    case ECausticsBakeJobState::Filtering: return To == ECausticsBakeJobState::Readback || To == ECausticsBakeJobState::Complete;
    case ECausticsBakeJobState::Readback:
        return To == ECausticsBakeJobState::Saving || To == ECausticsBakeJobState::Complete;
    case ECausticsBakeJobState::Saving: return To == ECausticsBakeJobState::Complete;
    default: return false;
    }
}
