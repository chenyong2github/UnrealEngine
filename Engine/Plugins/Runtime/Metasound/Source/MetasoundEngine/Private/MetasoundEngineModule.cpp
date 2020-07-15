// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEngineModule.h"

#include "Metasound.h"
#include "Modules/ModuleManager.h"


DEFINE_LOG_CATEGORY(LogMetasoundEngine);

class FMetasoundEngineModule : public IMetasoundEngineModule
{
	virtual UMetasound* DeserializeMetasound(const FString& InPath) override
	{
		FMetasoundDocument MetasoundDoc;
		ensureAlwaysMsgf(false, TEXT("Implement the actual loading part!"));

		UMetasound* NewMetasoundNode = NewObject<UMetasound>();
		NewMetasoundNode->SetMetasoundDocument(MetasoundDoc);

		return NewMetasoundNode;
	}

	virtual void SerializeMetasound(const UMetasound& InMetasound, const FString& InPath) override
	{
		ensureAlwaysMsgf(false, TEXT("Implement the actual saving part!"));
		ensureAlwaysMsgf(false, TEXT("Decide whether to delete this graph from the asset that owns it!"));
	}

	virtual void StartupModule() override
	{
		UE_LOG(LogMetasoundEngine, Log, TEXT("Metasound Engine Initialized"));
	}
};

IMPLEMENT_MODULE(FMetasoundEngineModule, MetasoundEngine);
