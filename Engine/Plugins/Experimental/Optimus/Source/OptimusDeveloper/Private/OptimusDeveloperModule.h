// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "IOptimusDeveloperModule.h"

class FOptimusDeveloperModule : public IOptimusDeveloperModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:

private:
};

DECLARE_LOG_CATEGORY_EXTERN(LogOptimusDeveloper, Log, All);
