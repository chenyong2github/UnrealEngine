// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

#include "BazelExecutor.h"

BAZELEXECUTOR_API DECLARE_LOG_CATEGORY_EXTERN(LogBazelExecutor, Display, All);

class FBazelExecutorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override;

private:
	FBazelExecutor BazelExecution;
};
