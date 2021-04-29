// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#if WITH_CUDA
#include "cuda.h"
DECLARE_MULTICAST_DELEGATE(FOnPostCUDAInit)
#endif // WITH_CUDA

DECLARE_LOG_CATEGORY_EXTERN(LogCUDA, Log, All);
class CUDA_API FCUDAModule : public IModuleInterface
{
#if WITH_CUDA
public:
	virtual void StartupModule() override;

	CUcontext GetCudaContext();
	CUcontext GetCudaContextForDevice(int DeviceIndex);

	FOnPostCUDAInit OnPostCUDAInit;

private:
	void InitCuda();

	uint32                   rhiDeviceIndex;
	TMap<uint32, CUcontext>  contextMap;

#endif // WITH_CUDA
};