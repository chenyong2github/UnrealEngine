// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "IOptimusCoreModule.h"

class FOptimusCoreModule : public IOptimusCoreModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:

private:
};

DECLARE_LOG_CATEGORY_EXTERN(LogOptimusCore, Log, All);
