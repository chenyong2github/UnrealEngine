// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#include "Misc/Paths.h"
#include "ShaderCore.h"

class FNNIHlslShadersModule : public IModuleInterface
{
public:
	// Begin IModuleInterface
	virtual void StartupModule() override
	{
		FString BaseDir;

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NNX"));
		
		if (Plugin.IsValid())
		{
			BaseDir = Plugin->GetBaseDir() + TEXT("/Source/NNIHlslShaders");
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Shaders directory not added. Failed to find NNI plugin"));
		}

		FString ModuleShaderDir = FPaths::Combine(BaseDir, TEXT("Shaders"));
		
		AddShaderSourceDirectoryMapping(TEXT("/NNI"), ModuleShaderDir);
	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FNNIHlslShadersModule, NNIHlslShaders);
