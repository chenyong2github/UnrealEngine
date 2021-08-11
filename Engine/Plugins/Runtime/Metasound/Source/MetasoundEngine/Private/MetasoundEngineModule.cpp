// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundEngineArchetypes.h"
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
		FModuleManager::Get().LoadModuleChecked("MetasoundGraphCore");
		FModuleManager::Get().LoadModuleChecked("MetasoundFrontend");
		FModuleManager::Get().LoadModuleChecked("MetasoundStandardNodes");
		FModuleManager::Get().LoadModuleChecked("MetasoundGenerator");
		FModuleManager::Get().LoadModuleChecked("AudioCodecEngine");

		Metasound::Engine::RegisterArchetypes();

		// If there is no archetype name, use UMetaSound
		FMetasoundFrontendVersion DefaultVersion;
		Metasound::IMetasoundUObjectRegistry::RegisterUClassArchetype<UMetaSound>(DefaultVersion);

		// Register preferred archetypes
		Metasound::IMetasoundUObjectRegistry::RegisterUClassPreferredArchetypes<UMetaSound>();
		Metasound::IMetasoundUObjectRegistry::RegisterUClassPreferredArchetypes<UMetaSoundSource>();

		// flush node registration queue
		FMetasoundFrontendRegistryContainer::Get()->RegisterPendingNodes();

		UE_LOG(LogMetasoundEngine, Log, TEXT("Metasound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
