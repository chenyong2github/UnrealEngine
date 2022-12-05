// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "NNXRuntime.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogNNX);

namespace NNX
{
	class FRegistry
	{
	public:

		static FRegistry* Get()
		{
			static FRegistry Inst;

			return &Inst;
		}

		bool Add(IRuntime* Runtime)
		{
			if (FindByName(Runtime->GetRuntimeName()) >= 0)
			{
				UE_LOG(LogNNX, Warning, TEXT("Runtime %s is already registered"), *Runtime->GetRuntimeName());
				return false;
			}

			Runtimes.Add(Runtime);

			return true;
		}

		bool Remove(IRuntime* Runtime)
		{
			int Index = FindByName(Runtime->GetRuntimeName());

			if (Index >= 0)
			{
				Runtimes.RemoveAt(Index);
				return true;
			}

			return false;
		}

		TArray<IRuntime*> GetAllRuntimes()
		{
			return Runtimes;
		}

	private:

		int FindByName(const FString& Name)
		{
			int Index = -1;

			for (int Idx = 0; Idx < Runtimes.Num(); ++Idx)
			{
				if (Runtimes[Idx]->GetRuntimeName() == Name)
				{
					Index = Idx;
					break;
				}
			}

			return Index;
		}

		TArray<IRuntime*>	Runtimes;
	};

	// Register runtime
	bool RegisterRuntime(IRuntime* Runtime)
	{
		return FRegistry::Get()->Add(Runtime);
	}

	// Unregister runtime
	bool UnregisterRuntime(IRuntime* Runtime)
	{
		return FRegistry::Get()->Remove(Runtime);
	}

	// Get matching runtime by name if any
	IRuntime* GetRuntime(const FString& Name)
	{
		for (auto Runtime : GetAllRuntimes())
		{
			if (Runtime->GetRuntimeName() == Name)
				return Runtime;
		}
		return nullptr;
	}

	// Enumerate all available runtime modules
	TArray<IRuntime*> GetAllRuntimes()
	{
		return FRegistry::Get()->GetAllRuntimes();

		/*
		TArray<INNXRuntimeModule*>	Modules;

		// Find all modules that implement NNXRuntime
		TArray<FName> ModuleNames;

		FModuleManager::Get().FindModules(L"NNXRuntime*", ModuleNames);

		for (auto ModuleName : ModuleNames)
		{
			// TODO: Report that module loading has failed
			//INNXRuntimeModule* Module = FModuleManager::Get().LoadModulePtr<INNXRuntimeModule>(ModuleName);
			EModuleLoadResult	ModuleLoadRes;

			auto Module = FModuleManager::Get().LoadModuleWithFailureReason(ModuleName, ModuleLoadRes);

			if (Module)
			{
				Modules.Add(static_cast<INNXRuntimeModule*>(Module));
			}
		}

		return Modules;
		*/
	}
}
