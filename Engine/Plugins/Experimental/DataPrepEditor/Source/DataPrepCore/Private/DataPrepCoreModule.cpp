// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepCoreModule.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "DataprepCoreModule"

class FDataprepCoreModule : public IDataprepCoreModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE( FDataprepCoreModule, DataprepCore )
