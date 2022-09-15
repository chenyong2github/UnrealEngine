// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module.h"

// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
void FNNXRuntimeCPUModule::StartupModule() 
{
	CPURuntime = NNX::FRuntimeCPUStartup();

	if (CPURuntime)
	{
		NNX::RegisterRuntime(CPURuntime);
	}
}

// This function may be called during shutdown to clean up your module. For modules that support dynamic reloading,
// we call this function before unloading the module.
void FNNXRuntimeCPUModule::ShutdownModule() 
{ 
	if (CPURuntime)
	{
		NNX::UnregisterRuntime(CPURuntime);
		CPURuntime = nullptr;
	}

	NNX::FRuntimeCPUShutdown();
}

IMPLEMENT_MODULE(FNNXRuntimeCPUModule, NNXRuntimeCPU);
