// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeCPUModule.h"
#include "NNECore.h"
#include "NNERuntimeCPU.h"
#include "NNXCore.h"
#include "NNXRuntimeCPU.h"
#include "UObject/WeakInterfacePtr.h"

void FNNERuntimeCPUModule::StartupModule()
{
	// NNX runtime startup
	CPURuntime = NNX::FRuntimeCPUStartup();
	if (CPURuntime)
	{
		NNX::RegisterRuntime(CPURuntime);
	}

	// NNE runtime startup
	NNERuntimeCPU = NewObject<UNNERuntimeCPUImpl>();
	if (NNERuntimeCPU.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeCPUInterface(NNERuntimeCPU.Get());
		
		NNERuntimeCPU->AddToRoot();
		UE::NNECore::RegisterRuntime(RuntimeCPUInterface);
	}
}

void FNNERuntimeCPUModule::ShutdownModule()
{
	// NNX runtime shutdown
	if (CPURuntime)
	{
		NNX::UnregisterRuntime(CPURuntime);
		CPURuntime = nullptr;
	}
	NNX::FRuntimeCPUShutdown();

	// NNE runtime shutdown
	if (NNERuntimeCPU.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeCPUInterface(NNERuntimeCPU.Get());
		
		UE::NNECore::UnregisterRuntime(RuntimeCPUInterface);
		NNERuntimeCPU->RemoveFromRoot();
		NNERuntimeCPU = TWeakObjectPtr<UNNERuntimeCPUImpl>(nullptr);
	}
}

IMPLEMENT_MODULE(FNNERuntimeCPUModule, NNXRuntimeCPU);
