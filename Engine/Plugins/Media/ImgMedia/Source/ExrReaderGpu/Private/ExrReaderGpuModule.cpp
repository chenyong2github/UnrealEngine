// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "ExrReaderGpuModule.h"
#include "Interfaces/IPluginManager.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogExrReaderGpu);

#define LOCTEXT_NAMESPACE "FExrReaderGpuModule"

void FExrReaderGpuModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ImgMedia"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/ExrReaderShaders"), PluginShaderDir);
}

void FExrReaderGpuModule::ShutdownModule()
{

}
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FExrReaderGpuModule, ExrReaderGpu);

#endif