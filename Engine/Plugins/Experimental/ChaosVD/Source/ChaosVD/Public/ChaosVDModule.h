// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Modules/ModuleManager.h"

class SDockTab;
class FSpawnTabArgs;

class FChaosVDModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	void RegisterClassesCustomDetails() const;

	TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args) const;
};
