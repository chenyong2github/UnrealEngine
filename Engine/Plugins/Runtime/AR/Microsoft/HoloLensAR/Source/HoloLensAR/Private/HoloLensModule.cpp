// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLensModule.h"
#include "Features/IModularFeatures.h"
#include "CoreGlobals.h"
#include "Misc/CoreDelegates.h"
#include "ARSupportInterface.h"
#include "Interfaces/IPluginManager.h"
#include "WindowsMixedRealityAvailability.h"

#define LOCTEXT_NAMESPACE "HoloLens"

TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> FHoloLensModuleAR::ARSystem;

IARSystemSupport* FHoloLensModuleAR::CreateARSystem()
{
	IARSystemSupport* ARSystemPtr = nullptr;
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	if (!ARSystem.IsValid())
	{
		ARSystem = MakeShareable(new FHoloLensARSystem());
	}
	ARSystemPtr = ARSystem.Get();
#endif
	return ARSystemPtr;
}

void FHoloLensModuleAR::SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop)
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	TSharedPtr<class FHoloLensARSystem, ESPMode::ThreadSafe> LocalARSystem = GetHoloLensARSystem();
	if (LocalARSystem.IsValid())
	{
		LocalARSystem->SetInterop(InWMRInterop);
	}
#endif
}

TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> FHoloLensModuleAR::GetHoloLensARSystem()
{
    return ARSystem;
}

void FHoloLensModuleAR::SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem)
{
#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	if (ARSystem.IsValid())
	{
		ARSystem->SetTrackingSystem(InTrackingSystem);
	}
#endif
}
void FHoloLensModuleAR::StartupModule()
{
	ensureMsgf(FModuleManager::Get().LoadModule("AugmentedReality"), TEXT("HoloLens depends on the AugmentedReality module."));

	FCoreDelegates::OnPreExit.AddRaw(this, &FHoloLensModuleAR::PreExit);

	// Map our shader directory
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("HoloLensAR"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/HoloLensAR"), PluginShaderDir);
}

void FHoloLensModuleAR::PreExit()
{
	if (ARSystem.IsValid())
	{
		ARSystem->Shutdown();
	}
	ARSystem = nullptr;
}

void FHoloLensModuleAR::ShutdownModule()
{

}

IMPLEMENT_MODULE(FHoloLensModuleAR, HoloLensAR);

DEFINE_LOG_CATEGORY(LogHoloLensAR);

#undef LOCTEXT_NAMESPACE
