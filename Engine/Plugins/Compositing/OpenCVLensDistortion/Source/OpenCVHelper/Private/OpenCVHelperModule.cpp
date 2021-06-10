// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOpenCVHelperModule.h"
#include "Modules/ModuleManager.h" // for IMPLEMENT_MODULE()
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"

#if WITH_OPENCV
#include "OpenCVHelper.h"
OPENCV_INCLUDES_START
#undef check 
#include "opencv2/unreal.hpp"
OPENCV_INCLUDES_END
#endif

class FOpenCVHelperModule : public IOpenCVHelperModule
{
public:
	FOpenCVHelperModule();

public:
	//~ IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void* OpenCvDllHandle;
};

FOpenCVHelperModule::FOpenCVHelperModule()
	: OpenCvDllHandle(nullptr)
{}

void FOpenCVHelperModule::StartupModule()
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("OpenCVLensDistortion"))->GetBaseDir();

#if WITH_OPENCV

	const FString OpenCvBinPath = PluginDir / TEXT(PREPROCESSOR_TO_STRING(OPENCV_PLATFORM_PATH));
	const FString DLLPath = OpenCvBinPath / TEXT(PREPROCESSOR_TO_STRING(OPENCV_DLL_NAME));

	FPlatformProcess::PushDllDirectory(*OpenCvBinPath);
	OpenCvDllHandle = FPlatformProcess::GetDllHandle(*DLLPath);
	FPlatformProcess::PopDllDirectory(*OpenCvBinPath);

	// We need to tell OpenCV to use Unreal's memory allocator to avoid crashes.
	// These may happen when Unreal passes a container to OpenCV, then OpenCV allocates memory for that container
	// and then Unreal tries to release the memory in it.
	cv::unreal::SetMallocAndFree(&FMemory::Malloc, &FMemory::Free);

#endif
}

void FOpenCVHelperModule::ShutdownModule()
{
#if WITH_OPENCV
	if (OpenCvDllHandle)
	{
		FPlatformProcess::FreeDllHandle(OpenCvDllHandle);
		OpenCvDllHandle = nullptr;
	}

	// Note: Seems safer to not put back the original new/delete in OpenCV and keep Unreal's versions even after this module unloads.

#endif
}

IMPLEMENT_MODULE(FOpenCVHelperModule, OpenCVHelper);
