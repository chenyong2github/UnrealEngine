// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutputLogController.h"

#include "Widgets/ConcertServerTabs.h"

#include "EditorStyleSet.h"
#include "Framework/Docking/TabManager.h"
#include "OutputLogCreationParams.h"
#include "OutputLogModule.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void FOutputLogController::Init(const FConcertComponentInitParams& Params)
{
	FGlobalTabmanager::Get()->RegisterTabSpawner(
			ConcertServerTabs::GetOutputLogTabId(),
			FOnSpawnTab::CreateRaw(this, &FOutputLogController::SpawnOutputLogTab)
		)
		.SetDisplayName(LOCTEXT("OutputLogTabTitle", "Output Log"))
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Log.TabIcon"));
}

TSharedRef<SDockTab> FOutputLogController::SpawnOutputLogTab(const FSpawnTabArgs& Args)
{
	FOutputLogCreationParams Params;
	Params.bCreateDockInLayoutButton = false;
	Params.SettingsMenuCreationFlags = EOutputLogSettingsMenuFlags::SkipClearOnPie;
	Params.DefaultCategorySelection = { { "LogSlate", false }, { "LogWindowsTextInputMethodSystem", false } };
	Params.AllowAsInitialLogCategory = FAllowLogCategoryCallback::CreateLambda([](const FName LogCategoryName)
	{
		const FString LogCategoryAsString = LogCategoryName.ToString();
		return LogCategoryAsString.Contains("Sync") || LogCategoryAsString.Contains("Concert");
	});
	
	return SNew(SDockTab)
		.Label(LOCTEXT("OutputLogTabTitle", "Output Log"))
		.TabRole(MajorTab)
		[
			FOutputLogModule::Get().MakeOutputLogWidget(Params)
		];
}

#undef LOCTEXT_NAMESPACE
