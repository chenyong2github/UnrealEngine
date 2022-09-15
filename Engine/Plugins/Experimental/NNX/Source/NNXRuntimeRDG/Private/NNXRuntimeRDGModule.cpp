// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "NNXRuntime.h"

#include "NNXRuntimeRDG.h"

#include "Modules/ModuleManager.h"

//
//
//
class FNNXRuntimeRDGModule : public IModuleInterface
{
	NNX::IRuntime* DmlRuntime { nullptr };
	NNX::IRuntime* HlslRuntime{ nullptr };

public:

	virtual void StartupModule() override
	{
#if PLATFORM_WINDOWS
		DmlRuntime = NNX::FMLRuntimeDmlStartup();

		if (DmlRuntime)
		{
			NNX::RegisterRuntime(DmlRuntime);
		}
#endif

		HlslRuntime = NNX::FMLRuntimeHlslStartup();

		if (HlslRuntime)
		{
			NNX::RegisterRuntime(HlslRuntime);
		}
	}

	virtual void ShutdownModule() override
	{
#if PLATFORM_WINDOWS
		if (DmlRuntime)
		{
			NNX::UnregisterRuntime(DmlRuntime);
			DmlRuntime = nullptr;
		}

		NNX::FMLRuntimeDmlShutdown();
#endif

		if (HlslRuntime)
		{
			NNX::UnregisterRuntime(HlslRuntime);
			HlslRuntime = nullptr;
		}

		NNX::FMLRuntimeHlslShutdown();
	}
};

IMPLEMENT_MODULE(FNNXRuntimeRDGModule, NNXRuntimeRDG)
