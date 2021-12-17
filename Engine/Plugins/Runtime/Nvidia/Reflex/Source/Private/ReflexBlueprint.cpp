// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReflexBlueprint.h"

#include "ReflexLatencyMarkers.h"
#include "ReflexMaxTickRateHandler.h"	

UReflexBlueprintLibrary::UReflexBlueprintLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UReflexBlueprintLibrary::GetReflexAvailable()
{
	bool bIsMaxTickRateHandlerEnabled = false;

	TArray<IMaxTickRateHandlerModule*> MaxTickRateHandlerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<IMaxTickRateHandlerModule>(IMaxTickRateHandlerModule::GetModularFeatureName());
	for (IMaxTickRateHandlerModule* MaxTickRateHandler : MaxTickRateHandlerModules)
	{
		bIsMaxTickRateHandlerEnabled = bIsMaxTickRateHandlerEnabled || MaxTickRateHandler->GetAvailable();
	}

	bool bIsLatencyMarkerModuleEnabled = false;

	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		bIsLatencyMarkerModuleEnabled = bIsLatencyMarkerModuleEnabled || LatencyMarkerModule->GetAvailable(); 
	}
	
	return bIsMaxTickRateHandlerEnabled && bIsLatencyMarkerModuleEnabled;

}

void UReflexBlueprintLibrary::SetReflexMode(const EReflexMode Mode)
{
	TArray<IMaxTickRateHandlerModule*> MaxTickRateHandlerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<IMaxTickRateHandlerModule>(IMaxTickRateHandlerModule::GetModularFeatureName());
	for (IMaxTickRateHandlerModule* MaxTickRateHandler : MaxTickRateHandlerModules)
	{
		MaxTickRateHandler->SetEnabled(Mode!=EReflexMode::Disabled);
		MaxTickRateHandler->SetFlags(static_cast<uint32>(Mode));
	}

	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		LatencyMarkerModule->SetEnabled(true);
	}
}

EReflexMode UReflexBlueprintLibrary::GetReflexMode()
{
	TArray<IMaxTickRateHandlerModule*> MaxTickRateHandlerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<IMaxTickRateHandlerModule>(IMaxTickRateHandlerModule::GetModularFeatureName());
	for (IMaxTickRateHandlerModule* MaxTickRateHandler : MaxTickRateHandlerModules)
	{
		return static_cast<EReflexMode>(MaxTickRateHandler->GetFlags());
	}

	return EReflexMode::Disabled;
}

void UReflexBlueprintLibrary::SetFlashIndicatorEnabled(const bool bEnabled)
{
	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		LatencyMarkerModule->SetFlashIndicatorEnabled(bEnabled);
	}
}

bool UReflexBlueprintLibrary::GetFlashIndicatorEnabled()
{
	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		return LatencyMarkerModule->GetFlashIndicatorEnabled();
	}
	
	return false;
}

float UReflexBlueprintLibrary::GetGameToRenderLatencyInMs()
{
	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		return LatencyMarkerModule->GetTotalLatencyInMs();
	}

	return 0.f;
}

float UReflexBlueprintLibrary::GetGameLatencyInMs()
{
	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		return LatencyMarkerModule->GetGameLatencyInMs();
	}

	return 0.f;
}

float UReflexBlueprintLibrary::GetRenderLatencyInMs()
{
	TArray<ILatencyMarkerModule*> LatencyMarkerModules = IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* LatencyMarkerModule : LatencyMarkerModules)
	{
		return LatencyMarkerModule->GetRenderLatencyInMs();
	}

	return 0.f;
}
