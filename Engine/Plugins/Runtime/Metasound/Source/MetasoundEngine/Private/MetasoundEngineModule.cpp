// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundWave.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMetasoundEngine);

REGISTER_METASOUND_DATATYPE(Metasound::FWaveAsset, "WaveAsset", Metasound::ELiteralType::UObjectProxy, USoundWave);

class FMetasoundEngineModule : public IMetasoundEngineModule
{
	virtual void StartupModule() override
	{
		// If there is no archetype name, use UMetasound
		Metasound::IMetasoundUObjectRegistry::RegisterUClassArchetype<UMetasound>(TEXT(""));

		// Register preferred archetypes
		Metasound::IMetasoundUObjectRegistry::RegisterUClassPreferredArchetypes<UMetasoundSource>();

		// flush node regsitration queue
		FMetasoundFrontendRegistryContainer::Get()->RegisterPendingNodes();

		FModuleManager::Get().LoadModuleChecked(TEXT("AudioCodecEngine"));

		UE_LOG(LogMetasoundEngine, Log, TEXT("Metasound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
