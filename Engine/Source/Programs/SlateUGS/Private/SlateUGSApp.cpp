// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateUGSApp.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Framework/Application/SlateApplication.h"

#include "SUnrealGameSyncWindow.h"
#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_APPLICATION(SlateUGS, "SlateUGS");

#define LOCTEXT_NAMESPACE "SlateUGS"

namespace
{
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Arguments)
	{
		TArray<TSharedPtr<HordeBuildRowInfo>> HordeBuilds;
		for (int i = 0; i < 35; i++)
		{
			TSharedPtr<HordeBuildRowInfo> Row = MakeShared<HordeBuildRowInfo>();
			Row->bBuildStatus = i % 2;
			Row->Changelist   = FText::FromString("12345678");
			Row->Time         = FText::FromString("11:48 AM");
			Row->Author       = FText::FromString("Robert Seiver");
			Row->Description  = FText::FromString("Fixed the thing");
			Row->Status       = FText::FromString("Used by Brandon Schaefer, Michael Sartain, ...");

			HordeBuilds.Add(Row);
		}

		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SUnrealGameSyncWindow)
					.HordeBuilds(HordeBuilds)
			];
	}
	
	void BuildWindow()
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner("SlateUGS", FOnSpawnTab::CreateStatic(SpawnTab));
		
		TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("StarshipSuite_Layout")
		->AddArea
		(
			FTabManager::NewArea(1230, 900)
			->Split
			(
				FTabManager::NewStack()
				->AddTab("SlateUGS", ETabState::OpenedTab)
				->SetForegroundTab(FName("SlateUGS"))
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
	FGlobalTabmanager::Get()->SetApplicationTitle(LOCTEXT("AppTitle", "Slate UGS"));

	FAppStyle::SetAppStyleSetName(FAppStyle::GetAppStyleSetName());
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
