// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterModule.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

//////////////////////////////////////////////////////////////////////////
// FStateMachineModule

DEFINE_LOG_CATEGORY(LogWater);


class FWaterModule : public IWaterModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("Water"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/Water"), PluginShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FWaterModule, Water);

