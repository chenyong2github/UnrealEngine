// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateUGSApp.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Framework/Application/SlateApplication.h"

#include "SUnrealGameSyncWindow.h"

#include "Widgets/SWorkspaceWindow.h"
#include "Widgets/SEmptyTab.h"

#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_APPLICATION(SlateUGS, "SlateUGS");

#define LOCTEXT_NAMESPACE "SlateUGS"

namespace
{
	TSharedRef<SDockTab> SpawnEmptyTab(const FSpawnTabArgs& Arguments)
	{
		return SNew(SDockTab).TabRole(ETabRole::MajorTab)
			[
				SNew(SEmptyTab)
			];
	}
	TSharedRef<SDockTab> SpawnActiveTab(const FSpawnTabArgs& Arguments)
	{
		// Todo: replace with real Horde build data (or gather this in the SUnrealGameSyncWindow construct)
		TArray<TSharedPtr<HordeBuildRowInfo>> HordeBuilds;
		for (int i = 0; i < 35; i++)
		{
			TSharedPtr<HordeBuildRowInfo> Row = MakeShared<HordeBuildRowInfo>();
			Row->bBuildStatus = !!(i % 2);
			Row->Changelist   = FText::FromString("12345678");
			Row->Time         = FText::FromString("11:48 AM");
			Row->Author       = FText::FromString("Robert Seiver");
			Row->Description  = FText::FromString("Fixed the thing");
			Row->Status       = FText::FromString("Used by Brandon Schaefer, Michael Sartain, ...");

			HordeBuilds.Add(Row);
		}

		return SNew(SDockTab).TabRole(ETabRole::MajorTab)
			[
				SNew(SUnrealGameSyncWindow)
					.HordeBuilds(HordeBuilds)
			];
	}
	
	void BuildWindow()
	{
		FGlobalTabmanager::Get()->RegisterTabSpawner("EmptyTab", FOnSpawnTab::CreateStatic(SpawnEmptyTab));
		FGlobalTabmanager::Get()->RegisterTabSpawner("ActiveTab", FOnSpawnTab::CreateStatic(SpawnActiveTab));
		TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("UGS_Layout")
		->AddArea
		(
			FTabManager::NewArea(1230, 900)
			->Split
			(
				FTabManager::NewStack()
				->AddTab("EmptyTab", ETabState::OpenedTab)
				->AddTab("ActiveTab", ETabState::ClosedTab) // Todo: seems to be only one tab per ID, need several tabs all with UGSActiveTab contents
				->SetForegroundTab(FName("EmptyTab"))
			)
		);
		FGlobalTabmanager::Get()->RestoreFrom(Layout, TSharedPtr<SWindow>());
	}
}

int RunSlateUGS(const TCHAR* CommandLine)
{
	FTaskTagScope TaskTagScope(ETaskTag::EGameThread);	

	// start up the main loop
	GEngineLoop.PreInit(CommandLine);

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();
	
	// Tell the module manager it may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	// crank up a normal Slate application using the platform's standalone renderer
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

	FSlateApplication::InitHighDPI(true);

	// set the application name
	FGlobalTabmanager::Get()->SetApplicationTitle(LOCTEXT("AppTitle", "Unreal Game Sync"));

	FAppStyle::SetAppStyleSetName(FAppStyle::GetAppStyleSetName());

	// Build the slate UI for the program window
	BuildWindow();

	// loop while the server does the rest
	while (!IsEngineExitRequested())
	{
		BeginExitIfRequested();

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FStats::AdvanceFrame(false);
		FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();		
		FPlatformProcess::Sleep(0.01f);
	
		GFrameCounter++;
	}

	FCoreDelegates::OnExit.Broadcast();
	FSlateApplication::Shutdown();
	FModuleManager::Get().UnloadModulesAtShutdown();

	GEngineLoop.AppPreExit();
	GEngineLoop.AppExit();

	return 0;
}

#undef LOCTEXT_NAMESPACE
