// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "STimedDataMonitorPanel.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"


class FTimedDataMonitorEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		STimedDataMonitorPanel::RegisterNomadTabSpawner(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());
	}

	virtual void ShutdownModule() override
	{
		if (!IsRunningCommandlet() && UObjectInitialized() && !IsEngineExitRequested())
		{
			STimedDataMonitorPanel::UnregisterNomadTabSpawner();
		}
	}
};

IMPLEMENT_MODULE(FTimedDataMonitorEditorModule, TimedDataMonitorEditor);

