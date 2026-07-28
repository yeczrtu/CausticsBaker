#pragma once

#include "Modules/ModuleManager.h"

class FCausticsBakerRenderManager;

class CAUSTICSBAKER_API FCausticsBakerModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static FCausticsBakerModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FCausticsBakerModule>(TEXT("CausticsBaker"));
    }

private:
    void OnPostEngineInit();
    void OnPrepareRayTracing(const class FViewInfo& View, TArray<class FRHIRayTracingShader*>& OutRayGenShaders);
    void OnAnyRayTracingPassEnabled(bool& bAnyRayTracingPassEnabled);

    FDelegateHandle PostEngineInitHandle;
    FDelegateHandle PrepareRayTracingHandle;
    FDelegateHandle AnyRayTracingHandle;
    TSharedPtr<class FCausticsBakerViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
