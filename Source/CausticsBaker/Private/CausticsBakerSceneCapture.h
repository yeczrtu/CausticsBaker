#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "CausticsBakerSceneCapture.generated.h"

/** Gives the renderer a stable owner identity for the baker-only capture view. */
UCLASS(Transient, NotBlueprintable)
class UCausticsBakerSceneCaptureComponent2D final : public USceneCaptureComponent2D
{
    GENERATED_BODY()

public:
    virtual const AActor* GetViewOwner() const override { return GetOwner(); }
};
