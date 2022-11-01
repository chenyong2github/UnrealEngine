// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FNNIProfilingModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		
	}

	virtual void ShutdownModule() override
	{

	}
};

IMPLEMENT_MODULE(FNNIProfilingModule, NNIProfiling);
