// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraEditorModule.h"
#include "VirtualCameraEditorStyle.h"

#include "Modules/ModuleManager.h"
#include "VirtualCameraTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FVirtualCameraEditorModule"


class FVirtualCameraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FVirtualCameraEditorStyle::Register();
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> DeveloperToolsGroup = MenuStructure.GetDeveloperToolsMiscCategory();
		SVirtualCameraTab::RegisterNomadTabSpawner(DeveloperToolsGroup);
	}

	virtual void ShutdownModule() override
	{
		if (!IsEngineExitRequested() && UObjectInitialized())
		{
			FVirtualCameraEditorStyle::Unregister();
			SVirtualCameraTab::UnregisterNomadTabSpawner();
		}
	}
};

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)

#undef LOCTEXT_NAMESPACE
