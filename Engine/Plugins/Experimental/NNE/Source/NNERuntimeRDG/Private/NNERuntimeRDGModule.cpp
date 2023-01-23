// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "NNXRuntime.h"

#include "NNERuntimeRDG.h"

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
#ifdef NNE_USE_DIRECTML
		DmlRuntime = UE::NNERuntimeRDG::Private::Dml::FRuntimeDmlStartup();

		if (DmlRuntime)
		{
			NNX::RegisterRuntime(DmlRuntime);
		}
#endif

		HlslRuntime = UE::NNERuntimeRDG::Private::Hlsl::FRuntimeHlslStartup();

		if (HlslRuntime)
		{
			NNX::RegisterRuntime(HlslRuntime);
		}
	}

	virtual void ShutdownModule() override
	{
#ifdef NNE_USE_DIRECTML
		if (DmlRuntime)
		{
			NNX::UnregisterRuntime(DmlRuntime);
			DmlRuntime = nullptr;
		}

		UE::NNERuntimeRDG::Private::Dml::FRuntimeDmlShutdown();
#endif

		if (HlslRuntime)
		{
			NNX::UnregisterRuntime(HlslRuntime);
			HlslRuntime = nullptr;
		}

		UE::NNERuntimeRDG::Private::Hlsl::FRuntimeHlslShutdown();
	}
};

IMPLEMENT_MODULE(FNNXRuntimeRDGModule, NNXRuntimeRDG)
