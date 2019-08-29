// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SignalProcessingModule.h"
#include "Modules/ModuleManager.h"

class FSignalProcessingModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
	}
};

DEFINE_LOG_CATEGORY(LogSignalProcessing);

IMPLEMENT_MODULE(FSignalProcessingModule, SignalProcessing);
