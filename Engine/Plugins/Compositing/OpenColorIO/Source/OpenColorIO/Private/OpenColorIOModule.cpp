// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOModule.h"

#include "Interfaces/IPluginManager.h"
#include "OpenColorIOLibHandler.h"
#include "OpenColorIODisplayManager.h"
#include "OpenColorIONativeConfiguration.h"
#include "ShaderCore.h"


DEFINE_LOG_CATEGORY(LogOpenColorIO);

#define LOCTEXT_NAMESPACE "OpenColorIOModule"

FOpenColorIOModule::FOpenColorIOModule()
	: DisplayManager(MakeUnique<FOpenColorIODisplayManager>())
{

}

void FOpenColorIOModule::StartupModule()
{
	bInitializedLib = FOpenColorIOLibHandler::Initialize();

	// Maps virtual shader source directory /Plugin/OpenCVLensDistortion to the plugin's actual Shaders directory.
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("OpenColorIO"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/OpenColorIO"), PluginShaderDir);
}

void FOpenColorIOModule::ShutdownModule()
{
	FOpenColorIOLibHandler::Shutdown();
}

FOpenColorIODisplayManager& FOpenColorIOModule::GetDisplayManager()
{
	return *DisplayManager;
}

FOpenColorIONativeInterchangeConfiguration* FOpenColorIOModule::GetNativeInterchangeConfig_Internal()
{
	if (bInitializedLib && NativeInterchangeConfig == nullptr)
	{
		/**
		 * Create a single config used for conversions between the working color spaceand the interchange space.
		 * Note that we delay the creation here to ensure that the global working color space has been loaded by
		 * the engine via renderer settings.
		 */
		NativeInterchangeConfig = MakeUnique<FOpenColorIONativeInterchangeConfiguration>();
	}

	return NativeInterchangeConfig.Get();
}
	
IMPLEMENT_MODULE(FOpenColorIOModule, OpenColorIO);

#undef LOCTEXT_NAMESPACE
