#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CausticsBakeTypes.h"
#include "CausticsBakeRegion.generated.h"

class UArrowComponent;
class UBoxComponent;
class ULightComponent;
class UPrimitiveComponent;
class USceneComponent;
class UTexture2D;
class ALight;

UCLASS(NotBlueprintable, meta = (DisplayName = "Caustics Bake Region"))
class CAUSTICSBAKER_API ACausticsBakeRegion : public AActor
{
    GENERATED_BODY()

public:
    ACausticsBakeRegion();

    virtual bool IsEditorOnly() const override { return true; }
    virtual bool ShouldTickIfViewportsOnly() const override { return false; }
    virtual void PostLoad() override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void Destroyed() override;

    UPROPERTY(VisibleAnywhere, Category = "Region")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Region")
    TObjectPtr<UBoxComponent> ProjectionBox;

    UPROPERTY(VisibleAnywhere, Category = "Region")
    TObjectPtr<UArrowComponent> ProjectionDirection;

    UPROPERTY(EditAnywhere, Category = "Region", meta = (ClampMin = "1.0", UIMin = "10.0", Units = "cm"))
    float Depth = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Region", meta = (ClampMin = "1.0", UIMin = "10.0", Units = "cm"))
    float Width = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Region", meta = (ClampMin = "1.0", UIMin = "10.0", Units = "cm"))
    float Height = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Region", meta = (DisplayName = "Auto Fit Depth To Receiver Filter", ToolTip = "Before Preview or Bake, extend Depth when an explicitly filtered receiver is farther along local +X but still overlaps the Width/Height footprint."))
    bool bAutoFitDepthToReceiverFilter = true;

    UPROPERTY(EditInstanceOnly, Category = "Scene", meta = (DisplayName = "Light Actor", AllowedClasses = "/Script/Engine.DirectionalLight,/Script/Engine.PointLight,/Script/Engine.SpotLight", ToolTip = "Directional, Point, or Spot Light actor used to emit photons."))
    TObjectPtr<ALight> LightActor;

    // Kept serialized for regions created by the first plugin build. New regions
    // use LightActor so the Details panel gets the normal actor picker/eyedropper.
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use LightActor."))
    FComponentReference Light;

    UPROPERTY(EditInstanceOnly, Category = "Scene", meta = (TitleProperty = "Component"))
    TArray<FCausticsCasterEntry> Casters;

    UPROPERTY(EditInstanceOnly, Category = "Scene", meta = (UseComponentPicker, DisplayName = "Receiver Filter (Optional)", ToolTip = "Leave empty to project automatically onto every supported ray-tracing-visible mesh intersecting the box. Add entries only to restrict receiving surfaces."))
    TArray<FComponentReference> Receivers;

    UPROPERTY(EditAnywhere, Category = "Bake", meta = (DisplayName = "Quality Settings"))
    FCausticsBakeSettings Settings;

    UPROPERTY(EditAnywhere, Category = "Output")
    FDirectoryPath OutputDirectory;

    UPROPERTY(EditAnywhere, Category = "Output")
    FString OutputTextureName;

    UPROPERTY(VisibleAnywhere, Category = "Output")
    TObjectPtr<UTexture2D> OutputTexture;

    UPROPERTY(VisibleAnywhere, Category = "Status")
    FString LastBakeSignature;

    UPROPERTY(VisibleAnywhere, Category = "Status")
    FString LastPreviewSignature;

    UPROPERTY(VisibleAnywhere, Category = "Status")
    bool bOutputOutOfDate = true;

    UPROPERTY(VisibleAnywhere, Category = "Status")
    bool bPreviewOutOfDate = true;

    UFUNCTION(CallInEditor, Category = "Caustics")
    void Preview();

    UFUNCTION(CallInEditor, Category = "Caustics")
    void Bake();

    UFUNCTION(CallInEditor, Category = "Caustics")
    void Cancel();

    UFUNCTION(CallInEditor, Category = "Caustics")
    void ClearPreview();

    UFUNCTION(CallInEditor, Category = "Caustics")
    void OpenOutputTexture();

    UFUNCTION(BlueprintCallable, Category = "Caustics")
    ULightComponent* ResolveLight() const;

    FString GetEffectiveOutputPackageName() const;
    FString BuildBakeSignature() const;
    void ResolveReceiverComponents(TArray<UPrimitiveComponent*>& OutReceivers) const;
    FVector GetLocalProjectionOrigin() const { return FVector::ZeroVector; }
    FVector GetLocalProjectionExtent() const { return FVector(Depth, Width * 0.5f, Height * 0.5f); }
    FBox GetWorldProjectionBounds() const;
    void RefreshVisualization();
    void MarkResultsOutOfDate();
};
