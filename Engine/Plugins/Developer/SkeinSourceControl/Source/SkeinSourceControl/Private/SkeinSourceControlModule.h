// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "SkeinSourceControlProvider.h"

class FSkeinSourceControlModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Access the Skein source control provider */
	FSkeinSourceControlProvider& GetProvider()
	{
		return SkeinSourceControlProvider;
	}

private:
	/** The Skein source control provider */
	FSkeinSourceControlProvider SkeinSourceControlProvider;
};
