#include "CausticsBakerModule.h"

#include "CausticsBakeRegion.h"
#include "CausticsBakerRenderer.h"
#include "ComponentVisualizer.h"
#include "Components/BoxComponent.h"
#include "DeferredShadingRenderer.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

#if ENGINE_MAJOR_VERSION != 5 || ENGINE_MINOR_VERSION != 8
#error CausticsBaker uses UE Renderer private APIs and intentionally supports Unreal Engine 5.8 only.
#endif

class FCausticsBakeRegionVisualizer;
TSharedRef<FComponentVisualizer> MakeCausticsBakeRegionVisualizer();
TSharedRef<class IDetailCustomization> MakeCausticsBakeRegionDetails();

void FCausticsBakerModule::StartupModule()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CausticsBaker"));
    checkf(Plugin.IsValid(), TEXT("CausticsBaker plugin descriptor could not be found."));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/CausticsBaker"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));

    PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FCausticsBakerModule::OnPostEngineInit);
    PrepareRayTracingHandle = FGlobalIlluminationPluginDelegates::PrepareRayTracing().AddRaw(
        this, &FCausticsBakerModule::OnPrepareRayTracing);
    AnyRayTracingHandle = FGlobalIlluminationPluginDelegates::AnyRayTracingPassEnabled().AddRaw(
        this, &FCausticsBakerModule::OnAnyRayTracingPassEnabled);
}

void FCausticsBakerModule::OnPostEngineInit()
{
    ViewExtension = FSceneViewExtensions::NewExtension<FCausticsBakerViewExtension>();
    GetCausticsBakerRenderManager().Initialize(ViewExtension);

    if (GUnrealEd)
    {
        GUnrealEd->RegisterComponentVisualizer(UBoxComponent::StaticClass()->GetFName(), MakeCausticsBakeRegionVisualizer());
    }

    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    PropertyEditor.RegisterCustomClassLayout(ACausticsBakeRegion::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(&MakeCausticsBakeRegionDetails));
    PropertyEditor.NotifyCustomizationModuleChanged();
}

void FCausticsBakerModule::ShutdownModule()
{
    GetCausticsBakerRenderManager().Shutdown();

    if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
    {
        FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyEditor.UnregisterCustomClassLayout(ACausticsBakeRegion::StaticClass()->GetFName());
    }
    if (GUnrealEd)
    {
        GUnrealEd->UnregisterComponentVisualizer(UBoxComponent::StaticClass()->GetFName());
    }

    if (PostEngineInitHandle.IsValid())
    {
        FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
    }
    if (PrepareRayTracingHandle.IsValid())
    {
        FGlobalIlluminationPluginDelegates::PrepareRayTracing().Remove(PrepareRayTracingHandle);
    }
    if (AnyRayTracingHandle.IsValid())
    {
        FGlobalIlluminationPluginDelegates::AnyRayTracingPassEnabled().Remove(AnyRayTracingHandle);
    }
    ViewExtension.Reset();
}

void FCausticsBakerModule::OnPrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
    AddCausticsRayGenerationShaders(View, OutRayGenShaders);
}

void FCausticsBakerModule::OnAnyRayTracingPassEnabled(bool& bAnyRayTracingPassEnabled)
{
    bAnyRayTracingPassEnabled |= GetCausticsBakerRenderManager().GetActiveJob().IsValid();
}

IMPLEMENT_MODULE(FCausticsBakerModule, CausticsBaker)
