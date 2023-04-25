// Copyright Epic Games, Inc. All Rights Reserved.

#include "XRScribeModule.h"

#include "Modules/ModuleManager.h"
#include "XRScribeAPISurface.h"

IMPLEMENT_MODULE(FXRScribeModule, XRScribe)

void FXRScribeModule::StartupModule()
{
	RegisterOpenXRExtensionModularFeature();
	IModularFeatures::Get().RegisterModularFeature(GetFeatureName(), this);

	UE::XRScribe::BuildFunctionMaps();
}

void FXRScribeModule::ShutdownModule()
{
	UnregisterOpenXRExtensionModularFeature();
	IModularFeatures::Get().UnregisterModularFeature(GetFeatureName(), this);

	UE::XRScribe::ClearFunctionMaps();
	ChainedGetProcAddr = nullptr;
}

FString FXRScribeModule::GetDisplayName()
{
	return GetFeatureName().ToString();
}

bool FXRScribeModule::InsertOpenXRAPILayer(PFN_xrGetInstanceProcAddr& InOutGetProcAddr)
{
	ChainedGetProcAddr = InOutGetProcAddr;
	InOutGetProcAddr = &UE::XRScribe::xrGetInstanceProcAddr;
	
	// Ok to pass nullptr for GetProcAddr because that would only disable capture. Replay still valid!
	UE::XRScribe::IOpenXRAPILayerManager::Get().SetChainedGetProcAddr(ChainedGetProcAddr);

	return true;
}


FXRScribeModule* FXRScribeModule::Get()
{
	TArray<FXRScribeModule*> Impls = IModularFeatures::Get().GetModularFeatureImplementations<FXRScribeModule>(GetFeatureName());

	check(Impls.Num() <= 1);

	if (Impls.Num() > 0)
	{
		check(Impls[0]);
		return Impls[0];
	}
	return nullptr;
}

