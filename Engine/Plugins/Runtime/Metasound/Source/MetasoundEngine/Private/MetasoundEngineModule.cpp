// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundEngineArchetypes.h"
#include "MetasoundInterface.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundWave.h"
#include "Modules/ModuleManager.h"
#include "Sound/AudioSettings.h"

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

		// Register engine-level parameter interfaces if not done already.
		// (Potentially not already called if plugin is loaded while cooking.)
		UAudioSettings* AudioSettings = GetMutableDefault<UAudioSettings>();
		check(AudioSettings);
		AudioSettings->RegisterParameterInterfaces();

		// Register interfaces
		Metasound::Engine::RegisterInterfaces();

		// Flush node registration queue
		FMetasoundFrontendRegistryContainer::Get()->RegisterPendingNodes();

		UE_LOG(LogMetasoundEngine, Log, TEXT("MetaSound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
