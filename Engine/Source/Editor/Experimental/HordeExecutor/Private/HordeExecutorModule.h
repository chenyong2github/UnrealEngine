// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

#include "HordeExecutor.h"

HORDEEXECUTOR_API DECLARE_LOG_CATEGORY_EXTERN(LogHordeExecutor, Display, All);

class FHordeExecutorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override;

private:
	FHordeExecutor HordeExecution;
};
