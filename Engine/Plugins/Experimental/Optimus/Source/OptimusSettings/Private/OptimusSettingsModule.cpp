// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusSettingsModule.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

void FOptimusSettingsModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(IMeshDeformerProvider::ModularFeatureName, this);
}

void FOptimusSettingsModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IMeshDeformerProvider::ModularFeatureName, this);
}

TSoftObjectPtr<UMeshDeformer> FOptimusSettingsModule::GetDefaultMeshDeformer()
{
	// todo[CF]: Make this a plugin .ini setting
	return nullptr;
}

IMPLEMENT_MODULE(FOptimusSettingsModule, OptimusSettings)
