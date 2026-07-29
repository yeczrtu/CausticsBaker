#include "CausticsTextureOutput.h"

namespace
{
    float SanitizeNonNegative(const float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
    }
}

TArray<FColor> CausticsBaker::TextureOutput::ConvertToLDR8(
    const TConstArrayView<FFloat16Color> Pixels, const float WhiteLevel)
{
    const float SafeWhiteLevel = FMath::IsFinite(WhiteLevel)
        ? FMath::Max(WhiteLevel, 1.0e-4f)
        : 1.0f;
    const float InvWhiteLevel = 1.0f / SafeWhiteLevel;

    TArray<FColor> Result;
    Result.SetNumUninitialized(Pixels.Num());
    for (int32 Index = 0; Index < Pixels.Num(); ++Index)
    {
        const FFloat16Color& Pixel = Pixels[Index];
        const FLinearColor LDR(
            FMath::Min(SanitizeNonNegative(Pixel.R.GetFloat()) * InvWhiteLevel, 1.0f),
            FMath::Min(SanitizeNonNegative(Pixel.G.GetFloat()) * InvWhiteLevel, 1.0f),
            FMath::Min(SanitizeNonNegative(Pixel.B.GetFloat()) * InvWhiteLevel, 1.0f),
            FMath::Min(SanitizeNonNegative(Pixel.A.GetFloat()), 1.0f));
        Result[Index] = LDR.ToFColor(true);
    }
    return Result;
}
