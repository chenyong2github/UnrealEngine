// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "MetasoundAudioBus.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundOutputSubsystem.h"
#include "MetasoundOutputWatcher.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "MetasoundUObjectRegistry.h"
#include "MetasoundWave.h"
#include "MetasoundWaveTable.h"
#include "Analysis/MetasoundFrontendAnalyzerRegistry.h"
#include "Analysis/MetasoundFrontendVertexAnalyzerForwardValue.h"
#include "Interfaces/MetasoundDeprecatedInterfaces.h"
#include "Interfaces/MetasoundInterface.h"
#include "Interfaces/MetasoundInterfaceBindingsPrivate.h"
#include "Modules/ModuleManager.h"
#include "Sound/AudioSettings.h"

DEFINE_LOG_CATEGORY(LogMetasoundEngine);


REGISTER_METASOUND_DATATYPE(Metasound::FAudioBusAsset, "AudioBusAsset", Metasound::ELiteralType::UObjectProxy, UAudioBus);
REGISTER_METASOUND_DATATYPE(Metasound::FWaveAsset, "WaveAsset", Metasound::ELiteralType::UObjectProxy, USoundWave);
REGISTER_METASOUND_DATATYPE(WaveTable::FWaveTable, "WaveTable", Metasound::ELiteralType::FloatArray)
REGISTER_METASOUND_DATATYPE(Metasound::FWaveTableBankAsset, "WaveTableBankAsset", Metasound::ELiteralType::UObjectProxy, UWaveTableBank);


class FMetasoundEngineModule : public IMetasoundEngineModule
{
	virtual void StartupModule() override
	{
		using namespace Metasound;
		using namespace Metasound::Engine;

		METASOUND_LLM_SCOPE;
		FModuleManager::Get().LoadModuleChecked("MetasoundGraphCore");
		FModuleManager::Get().LoadModuleChecked("MetasoundFrontend");
		FModuleManager::Get().LoadModuleChecked("MetasoundStandardNodes");
		FModuleManager::Get().LoadModuleChecked("MetasoundGenerator");
		FModuleManager::Get().LoadModuleChecked("AudioCodecEngine");
		FModuleManager::Get().LoadModuleChecked("WaveTable");

		// Register engine-level parameter interfaces if not done already.
		// (Potentially not already called if plugin is loaded while cooking.)
		UAudioSettings* AudioSettings = GetMutableDefault<UAudioSettings>();
		check(AudioSettings);
		AudioSettings->RegisterParameterInterfaces();

		IMetasoundUObjectRegistry::Get().RegisterUClass(MakeUnique<TMetasoundUObjectRegistryEntry<UMetaSoundPatch>>());
		IMetasoundUObjectRegistry::Get().RegisterUClass(MakeUnique<TMetasoundUObjectRegistryEntry<UMetaSoundSource>>());

		Engine::RegisterDeprecatedInterfaces();
		Engine::RegisterInterfaces();
		Engine::RegisterInternalInterfaceBindings();

		// Flush node registration queue
		FMetasoundFrontendRegistryContainer::Get()->RegisterPendingNodes();

		// Register Analyzers
		Metasound::Frontend::IVertexAnalyzerRegistry::Get().RegisterAnalyzerFactories();

		// Register output watcher ops
		METASOUND_PRIVATE_REGISTER_GENERATOR_OUTPUT_WATCHER_TYPE_OPERATIONS(float, 0.0f)
		METASOUND_PRIVATE_REGISTER_GENERATOR_OUTPUT_WATCHER_TYPE_OPERATIONS(int32, 0)
		METASOUND_PRIVATE_REGISTER_GENERATOR_OUTPUT_WATCHER_TYPE_OPERATIONS(bool, false)
		METASOUND_PRIVATE_REGISTER_GENERATOR_OUTPUT_WATCHER_TYPE_OPERATIONS(FString, "")

		// Register passthrough output analyzers
		UMetaSoundOutputSubsystem::RegisterPassthroughAnalyzerForType(
			GetMetasoundDataTypeName<float>(),
			Frontend::FVertexAnalyzerForwardFloat::GetAnalyzerName());
		UMetaSoundOutputSubsystem::RegisterPassthroughAnalyzerForType(
			GetMetasoundDataTypeName<int32>(),
			Frontend::FVertexAnalyzerForwardInt::GetAnalyzerName());
		UMetaSoundOutputSubsystem::RegisterPassthroughAnalyzerForType(
			GetMetasoundDataTypeName<bool>(),
			Frontend::FVertexAnalyzerForwardBool::GetAnalyzerName());
		UMetaSoundOutputSubsystem::RegisterPassthroughAnalyzerForType(
			GetMetasoundDataTypeName<FString>(),
			Frontend::FVertexAnalyzerForwardString::GetAnalyzerName());

		UE_LOG(LogMetasoundEngine, Log, TEXT("MetaSound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
