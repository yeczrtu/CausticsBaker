#include "CausticsBakeRegion.h"

#include "CausticsBakerEditorSubsystem.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CausticsBakeRegionDetails"

class FCausticsBakeRegionDetails final : public IDetailCustomization
{
public:
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
    {
        TArray<TWeakObjectPtr<UObject>> Objects;
        DetailBuilder.GetObjectsBeingCustomized(Objects);
        if (Objects.IsEmpty()) return;
        Region = Cast<ACausticsBakeRegion>(Objects[0].Get());
        if (!Region.IsValid()) return;

        IDetailCategoryBuilder& Scene = DetailBuilder.EditCategory(TEXT("Scene"));
        Scene.AddCustomRow(LOCTEXT("ReceiverModeFilter", "Receiver automatic projection filter"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            .Text_Lambda([this]()
            {
                if (!Region.IsValid()) return FText::GetEmpty();
                return Region->Receivers.IsEmpty()
                    ? LOCTEXT("AutomaticReceivers", "Receiver mode: Automatic — projects onto supported ray-tracing-visible meshes inside the box. No Receiver setup is required.")
                    : FText::Format(LOCTEXT("FilteredReceivers", "Receiver mode: Filtered — only the {0} listed component(s) can receive the projection."), Region->Receivers.Num());
            })
        ];

        IDetailCategoryBuilder& Actions = DetailBuilder.EditCategory(TEXT("Caustics Actions"),
            LOCTEXT("Actions", "Caustics Actions"), ECategoryPriority::Important);
        Actions.AddCustomRow(LOCTEXT("ButtonsFilter", "Preview Bake Cancel Clear Open"))
        .WholeRowContent()
        [
            SNew(SGridPanel)
            + SGridPanel::Slot(0, 0).Padding(2)
            [
                SNew(SButton).Text(LOCTEXT("Preview", "Preview")).OnClicked(this, &FCausticsBakeRegionDetails::Preview)
            ]
            + SGridPanel::Slot(1, 0).Padding(2)
            [
                SNew(SButton).Text(LOCTEXT("Bake", "Bake")).OnClicked(this, &FCausticsBakeRegionDetails::Bake)
            ]
            + SGridPanel::Slot(2, 0).Padding(2)
            [
                SNew(SButton).Text(LOCTEXT("Cancel", "Cancel")).OnClicked(this, &FCausticsBakeRegionDetails::Cancel)
            ]
            + SGridPanel::Slot(0, 1).ColumnSpan(2).Padding(2)
            [
                SNew(SButton).Text(LOCTEXT("Clear", "Clear Preview")).OnClicked(this, &FCausticsBakeRegionDetails::Clear)
            ]
            + SGridPanel::Slot(2, 1).Padding(2)
            [
                SNew(SButton).Text(LOCTEXT("Open", "Open Output")).OnClicked(this, &FCausticsBakeRegionDetails::OpenOutput)
            ]
        ];

        Actions.AddCustomRow(LOCTEXT("ProgressFilter", "Progress Status"))
        .NameContent()[SNew(STextBlock).Text(LOCTEXT("Progress", "Progress"))]
        .ValueContent().MinDesiredWidth(300.0f)
        [
            SNew(SProgressBar)
            .Percent_Lambda([this]() -> TOptional<float>
            {
                if (const UCausticsBakerEditorSubsystem* Subsystem = GetSubsystem()) return Subsystem->GetStatus().Progress;
                return 0.0f;
            })
        ];

        Actions.AddCustomRow(LOCTEXT("MessageFilter", "Message Error"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda([this]()
            {
                return GetSubsystem() ? GetSubsystem()->GetStatus().Message : FText::GetEmpty();
            })
        ];

        Actions.AddCustomRow(LOCTEXT("FreshnessFilter", "Out of Date Signature"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                if (!Region.IsValid()) return FText::GetEmpty();
                const double Now = FPlatformTime::Seconds();
                if (CachedSignature.IsEmpty() || Now - LastSignatureRefreshSeconds >= 1.0)
                {
                    CachedSignature = Region->BuildBakeSignature();
                    LastSignatureRefreshSeconds = Now;
                }
                const bool bPreviewStale = Region->LastPreviewSignature.IsEmpty() || Region->LastPreviewSignature != CachedSignature;
                const bool bOutputStale = Region->LastBakeSignature.IsEmpty() || Region->LastBakeSignature != CachedSignature;
                return FText::Format(LOCTEXT("Freshness", "Preview: {0}    Output: {1}"),
                    bPreviewStale ? LOCTEXT("PreviewStale", "Out of Date") : LOCTEXT("PreviewCurrent", "Current"),
                    bOutputStale ? LOCTEXT("OutputStale", "Out of Date") : LOCTEXT("OutputCurrent", "Current"));
            })
        ];
    }

private:
    UCausticsBakerEditorSubsystem* GetSubsystem() const
    {
        return GEditor ? GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>() : nullptr;
    }
    FReply Preview() { if (Region.IsValid()) GetSubsystem()->RequestPreview(Region.Get()); return FReply::Handled(); }
    FReply Bake() { if (Region.IsValid()) GetSubsystem()->RequestBake(Region.Get()); return FReply::Handled(); }
    FReply Cancel() { if (GetSubsystem()) GetSubsystem()->Cancel(); return FReply::Handled(); }
    FReply Clear() { if (Region.IsValid()) GetSubsystem()->ClearPreview(Region.Get()); return FReply::Handled(); }
    FReply OpenOutput() { if (Region.IsValid()) Region->OpenOutputTexture(); return FReply::Handled(); }

    TWeakObjectPtr<ACausticsBakeRegion> Region;
    FString CachedSignature;
    double LastSignatureRefreshSeconds = -1.0;
};

TSharedRef<IDetailCustomization> MakeCausticsBakeRegionDetails()
{
    return MakeShared<FCausticsBakeRegionDetails>();
}

#undef LOCTEXT_NAMESPACE
