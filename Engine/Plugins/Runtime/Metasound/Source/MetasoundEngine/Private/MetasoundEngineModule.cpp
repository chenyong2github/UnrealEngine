// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "Modules/ModuleManager.h"


DEFINE_LOG_CATEGORY(LogMetasoundEngine);

class FMetasoundEngineModule : public IMetasoundEngineModule
{
	virtual void StartupModule() override
	{
		UE_LOG(LogMetasoundEngine, Log, TEXT("Metasound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
