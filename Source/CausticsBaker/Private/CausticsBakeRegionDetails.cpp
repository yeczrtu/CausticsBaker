#include "CausticsBakeRegion.h"

#include "CausticsBakerEditorSubsystem.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
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

        DetailBuilder.HideCategory(TEXT("Scene"));
        DetailBuilder.HideCategory(TEXT("Bake"));
        DetailBuilder.HideCategory(TEXT("Status"));

        IDetailCategoryBuilder& Setup = DetailBuilder.EditCategory(TEXT("Caustics Setup"),
            LOCTEXT("SetupCategory", "Caustics Setup"), ECategoryPriority::Important);
        MoveProperty(DetailBuilder, Setup, GET_MEMBER_NAME_CHECKED(ACausticsBakeRegion, LightActor));
        MoveProperty(DetailBuilder, Setup, GET_MEMBER_NAME_CHECKED(ACausticsBakeRegion, Casters));
        MoveProperty(DetailBuilder, Setup, GET_MEMBER_NAME_CHECKED(ACausticsBakeRegion, Receivers));

        Setup.AddCustomRow(LOCTEXT("ReceiverModeFilter", "Receiver automatic projection filter"))
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

        IDetailCategoryBuilder& Quality = DetailBuilder.EditCategory(TEXT("Caustics Quality and Actions"),
            LOCTEXT("QualityActionsCategory", "Caustics Quality & Actions"), ECategoryPriority::TypeSpecific);

        const TSharedRef<IPropertyHandle> Settings = DetailBuilder.GetProperty(
            GET_MEMBER_NAME_CHECKED(ACausticsBakeRegion, Settings));
        DetailBuilder.HideProperty(Settings);
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, Preset));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, Resolution));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, PhotonBatches));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, PhotonsPerBatch));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, MaxBounces));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, AtrousIterations));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, RandomSeed));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, SPPMConvergence));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, InitialRadiusTexels));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, FilterStrength));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, Denoiser));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, DebugDisplay));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, OutputFormat));
        AddSettingProperty(Quality, Settings, GET_MEMBER_NAME_CHECKED(FCausticsBakeSettings, LDRWhiteLevel));

        AddEffectiveQualityRow(Quality, true, LOCTEXT("EffectivePreview", "Effective Preview"));
        AddEffectiveQualityRow(Quality, false, LOCTEXT("EffectiveBake", "Effective Bake"));

        Quality.AddCustomRow(LOCTEXT("ButtonsFilter", "Preview Bake Cancel Clear Open"))
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

        Quality.AddCustomRow(LOCTEXT("ProgressFilter", "Progress Status"))
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

        Quality.AddCustomRow(LOCTEXT("MessageFilter", "Message Error"))
        .WholeRowContent()
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda([this]()
            {
                return GetSubsystem() ? GetSubsystem()->GetStatus().Message : FText::GetEmpty();
            })
        ];

        Quality.AddCustomRow(LOCTEXT("FreshnessFilter", "Out of Date Signature"))
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
    static void MoveProperty(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Destination,
        const FName PropertyName)
    {
        const TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(PropertyName);
        if (Property->IsValidHandle())
        {
            DetailBuilder.HideProperty(Property);
            Destination.AddProperty(Property);
        }
    }

    static void AddSettingProperty(IDetailCategoryBuilder& Destination,
        const TSharedRef<IPropertyHandle>& Settings, const FName PropertyName)
    {
        const TSharedPtr<IPropertyHandle> Property = Settings->GetChildHandle(PropertyName);
        if (Property.IsValid() && Property->IsValidHandle())
        {
            Destination.AddProperty(Property.ToSharedRef());
        }
    }

    void AddEffectiveQualityRow(IDetailCategoryBuilder& Destination, const bool bPreview, const FText& Label)
    {
        Destination.AddCustomRow(Label)
        .NameContent()
        [
            SNew(STextBlock)
            .Font(IDetailLayoutBuilder::GetDetailFont())
            .Text(Label)
        ]
        .ValueContent()
        .MinDesiredWidth(300.0f)
        [
            SNew(STextBlock)
            .Font(IDetailLayoutBuilder::GetDetailFont())
            .Text_Lambda([this, bPreview]() { return GetEffectiveQualityText(bPreview); })
            .ToolTipText_Lambda([this, bPreview]() { return GetEffectiveQualityText(bPreview); })
        ];
    }

    FText GetEffectiveQualityText(const bool bPreview) const
    {
        if (!Region.IsValid()) return FText::GetEmpty();

        int32 Resolution = 0;
        int32 Batches = 0;
        int32 PhotonsPerBatch = 0;
        int32 Bounces = 0;
        int32 AtrousIterations = 0;
        Region->Settings.Resolve(bPreview, Resolution, Batches, PhotonsPerBatch, Bounces, AtrousIterations);
        const int64 TotalPhotons = static_cast<int64>(Batches) * static_cast<int64>(PhotonsPerBatch);
        return FText::Format(LOCTEXT("EffectiveQualityFormat", "{0} x {1} = {2} photons | {3} x {3} px | {4} bounces | {5} a-trous"),
            FText::AsNumber(Batches), FText::AsNumber(PhotonsPerBatch), FText::AsNumber(TotalPhotons),
            FText::AsNumber(Resolution), FText::AsNumber(Bounces), FText::AsNumber(AtrousIterations));
    }

    UCausticsBakerEditorSubsystem* GetSubsystem() const
    {
        return GEditor ? GEditor->GetEditorSubsystem<UCausticsBakerEditorSubsystem>() : nullptr;
    }

    FReply Preview()
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GetSubsystem(); Region.IsValid() && Subsystem)
        {
            Subsystem->RequestPreview(Region.Get());
        }
        return FReply::Handled();
    }

    FReply Bake()
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GetSubsystem(); Region.IsValid() && Subsystem)
        {
            Subsystem->RequestBake(Region.Get());
        }
        return FReply::Handled();
    }

    FReply Cancel()
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GetSubsystem()) Subsystem->Cancel();
        return FReply::Handled();
    }

    FReply Clear()
    {
        if (UCausticsBakerEditorSubsystem* Subsystem = GetSubsystem(); Region.IsValid() && Subsystem)
        {
            Subsystem->ClearPreview(Region.Get());
        }
        return FReply::Handled();
    }

    FReply OpenOutput()
    {
        if (Region.IsValid()) Region->OpenOutputTexture();
        return FReply::Handled();
    }

    TWeakObjectPtr<ACausticsBakeRegion> Region;
    FString CachedSignature;
    double LastSignatureRefreshSeconds = -1.0;
};

TSharedRef<IDetailCustomization> MakeCausticsBakeRegionDetails()
{
    return MakeShared<FCausticsBakeRegionDetails>();
}

#undef LOCTEXT_NAMESPACE
