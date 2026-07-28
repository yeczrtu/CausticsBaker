#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Math/Float16Color.h"
#include "EditorSubsystem.h"
#include "TickableEditorObject.h"
#include "CausticsBakeTypes.h"
#include "CausticsBakerEditorSubsystem.generated.h"

class ACausticsBakeRegion;
class UTexture2D;
class FCausticsRenderJob;

struct FCausticsOIDNResult
{
    TArray<FFloat16Color> Pixels;
    FString Error;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCausticsJobStatusChanged, const FCausticsJobStatus&, Status);

UCLASS()
class CAUSTICSBAKER_API UCausticsBakerEditorSubsystem final : public UEditorSubsystem, public FTickableEditorObject
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override { return !IsTemplate(); }

    UFUNCTION(BlueprintCallable, Category = "Caustics Baker")
    bool RequestPreview(ACausticsBakeRegion* Region);

    UFUNCTION(BlueprintCallable, Category = "Caustics Baker")
    bool RequestBake(ACausticsBakeRegion* Region);

    UFUNCTION(BlueprintCallable, Category = "Caustics Baker")
    void Cancel();

    UFUNCTION(BlueprintCallable, Category = "Caustics Baker")
    void ClearPreview(ACausticsBakeRegion* Region = nullptr);

    UFUNCTION(BlueprintPure, Category = "Caustics Baker")
    FCausticsJobStatus GetStatus() const { return Status; }

    UFUNCTION(BlueprintPure, Category = "Caustics Baker")
    bool IsBusy() const;

    UFUNCTION(BlueprintCallable, Category = "Caustics Baker")
    TArray<FText> ValidateRegion(const ACausticsBakeRegion* Region) const;

    void NotifyRegionDestroyed(const ACausticsBakeRegion* Region);
    void NotifyRegionChanged(const ACausticsBakeRegion* Region);

    UPROPERTY(BlueprintAssignable, Category = "Caustics Baker")
    FCausticsJobStatusChanged OnStatusChanged;

private:
    bool StartJob(ACausticsBakeRegion* Region, bool bPreview);
    void SetStatus(ECausticsBakeJobState NewState, float Progress, const FText& Message, bool bCancelPending = false);
    void FinalizeSuccessfulBake(UTexture2D* Texture);
    void OnMapChanged(uint32 MapChangeFlags);
    void DestroyCapture();
    void FinishTerminalJob(ECausticsBakeJobState TerminalState, const FText& Message);
    UTexture2D* CreateOrUpdateTexture(const TArray<FFloat16Color>& Pixels, FString& OutError);

    UPROPERTY(Transient)
    TObjectPtr<ACausticsBakeRegion> ActiveRegion;

    UPROPERTY(Transient)
    TObjectPtr<AActor> CaptureOwner;

    UPROPERTY(Transient)
    TObjectPtr<class USceneCaptureComponent2D> SceneCapture;

    FCausticsJobStatus Status;
    bool bPreviewJob = false;
    bool bCaptureSubmitted = false;
    bool bOIDNRunning = false;
    ECausticsBakeJobState LastObservedRenderState = ECausticsBakeJobState::Idle;
    int32 LastObservedBatch = INDEX_NONE;
    TSharedPtr<FCausticsRenderJob, ESPMode::ThreadSafe> RenderJob;
    TFuture<FCausticsOIDNResult> OIDNFuture;
    TArray<FFloat16Color> PendingBakePixels;
    TArray<FVector3f> PendingGuideNormals;
    FString ActiveSignature;
    FDelegateHandle MapChangeHandle;
};
