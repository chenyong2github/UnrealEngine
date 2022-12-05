// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#include "Misc/Paths.h"
#include "ShaderCore.h"

class FNNEHlslShadersModule : public IModuleInterface
{
public:
	// Begin IModuleInterface
	virtual void StartupModule() override
	{
		FString BaseDir;

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NNE"));
		
		if (Plugin.IsValid())
		{
			BaseDir = Plugin->GetBaseDir() + TEXT("/Source/NNEHlslShaders");
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Shaders directory not added. Failed to find NNE plugin"));
		}

		FString ModuleShaderDir = FPaths::Combine(BaseDir, TEXT("Shaders"));
		
		AddShaderSourceDirectoryMapping(TEXT("/NNE"), ModuleShaderDir);
	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FNNEHlslShadersModule, NNEHlslShaders);
