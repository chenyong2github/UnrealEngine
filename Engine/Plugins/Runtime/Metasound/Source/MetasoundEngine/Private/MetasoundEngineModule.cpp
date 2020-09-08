// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "MetasoundArchetypeRegistration.h"
#include "MetasoundSource.h"
#include "Modules/ModuleManager.h"
#include "MetasoundWave.h"
#include "MetasoundDataTypeRegistrationMacro.h"

DEFINE_LOG_CATEGORY(LogMetasoundEngine);

namespace Metasound
{
	REGISTER_METASOUND_DATATYPE(FWaveAsset, "Primitive:WaveAsset", ::Metasound::ELiteralArgType::UObjectProxy, USoundWave)
}

class FMetasoundEngineModule : public IMetasoundEngineModule
{
	virtual void StartupModule() override
	{
		Metasound::Frontend::RegisterArchetype<UMetasoundSource>();
		Metasound::Frontend::RegisterArchetype<UMetasound>();

		UE_LOG(LogMetasoundEngine, Log, TEXT("Metasound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
