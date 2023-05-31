// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPreloadDerivedDataCacheModule.h"

#include "DisplayClusterPreloadDerivedDataCacheLog.h"
#include "DisplayClusterPreloadDerivedDataCacheWorker.h"

#include "Commandlets/DerivedDataCacheCommandlet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IProjectManager.h"
#include "Modules/ModuleManager.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterPreloadDerivedDataCacheModule"

FDisplayClusterPreloadDerivedDataCacheModule& FDisplayClusterPreloadDerivedDataCacheModule::Get()
{
	return FModuleManager::GetModuleChecked<FDisplayClusterPreloadDerivedDataCacheModule>("DisplayClusterPreloadDerivedDataCacheModule");
}

void FDisplayClusterPreloadDerivedDataCacheModule::StartupModule()
{
	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDisplayClusterPreloadDerivedDataCacheModule::OnFEngineLoopInitComplete);
}

void FDisplayClusterPreloadDerivedDataCacheModule::OnFEngineLoopInitComplete()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Build");

	FToolMenuSection& Section = Menu->FindOrAddSection("LevelEditorAutomation");
		
	Section.AddMenuEntry(
		"PreloadDerivedDataCache",
		LOCTEXT("PreloadDerivedDataCache", "Preload Derived Data Cache"),
		LOCTEXT("PreloadDerivedDataCacheTooltip", "Precompile all shaders and preprocess Nanite and other heavy data ahead of time for all assets in this project for all checked platforms in Project Settings > Supported Platforms. \nThis is useful to avoid shader compilation and pop-in when opening a level or running PIE/Standalone. \nNote that this operation can take a very long time to complete and can take up a lot of disk space, depending on the size of your project."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDisplayClusterPreloadDerivedDataCacheModule::CreateAsyncTaskWorker)));
}

void FDisplayClusterPreloadDerivedDataCacheModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	if (AsyncTaskWorker)
	{
		AsyncTaskWorker->CancelTask();
		AsyncTaskWorker = nullptr;
	}
}

void FDisplayClusterPreloadDerivedDataCacheModule::CreateAsyncTaskWorker()
{
	if (AsyncTaskWorker)
	{
		AsyncTaskWorker->CancelTask();
	}
	AsyncTaskWorker = MakeUnique<FDisplayClusterPreloadDerivedDataCacheWorker>();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDisplayClusterPreloadDerivedDataCacheModule, DisplayClusterPreloadDerivedDataCache)
