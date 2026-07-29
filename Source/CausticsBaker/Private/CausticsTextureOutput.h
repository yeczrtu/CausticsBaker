#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"

namespace CausticsBaker::TextureOutput
{
    /** Converts linear HDR irradiance to sRGB BGRA8. Alpha coverage remains linear and is not white-level scaled. */
    TArray<FColor> ConvertToLDR8(TConstArrayView<FFloat16Color> Pixels, float WhiteLevel);
}
