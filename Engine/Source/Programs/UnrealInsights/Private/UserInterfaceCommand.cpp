// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UserInterfaceCommand.h"

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/LayoutService.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "ISlateReflectorModule.h"
#include "ISourceCodeAccessModule.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "StandaloneRenderer.h"
#include "Widgets/Docking/SDockTab.h"

// Insights
#include "Insights/IUnrealInsightsModule.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define IDEAL_FRAMERATE 60

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UserInterfaceCommand
{
	TSharedPtr<FTabManager::FLayout> ApplicationLayout;
	TSharedRef<FWorkspaceItem> DeveloperTools = FWorkspaceItem::NewGroup(NSLOCTEXT("UnrealInsights", "DeveloperToolsMenu", "Developer Tools"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::Run()
{
	FString UnrealInsightsLayoutIni = FPaths::GetPath(GEngineIni) + "/UnrealInsightsLayout.ini";

	FCoreStyle::ResetToDefault();

	// Load required modules.
	FModuleManager::Get().LoadModuleChecked("EditorStyle");
	FModuleManager::Get().LoadModuleChecked("TraceInsights");

	// Load plug-ins.
	// @todo: allow for better plug-in support in standalone Slate applications
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::PreDefault);

	// Load optional modules.
	FModuleManager::Get().LoadModule("SettingsEditor");

	InitializeSlateApplication(UnrealInsightsLayoutIni);

	// Initialize source code access.
	// Load the source code access module.
	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>(FName("SourceCodeAccess"));

	// Manually load in the source code access plugins, as standalone programs don't currently support plugins.
#if PLATFORM_MAC
	//im:TODO: IModuleInterface& XCodeSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>(FName("XCodeSourceCodeAccess"));
	//im:TODO: SourceCodeAccessModule.SetAccessor(FName("XCodeSourceCodeAccess"));
#elif PLATFORM_WINDOWS
	//im:TODO: IModuleInterface& VisualStudioSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>(FName("VisualStudioSourceCodeAccess"));
	//im:TODO: SourceCodeAccessModule.SetAccessor(FName("VisualStudioSourceCodeAccess"));
#endif

#if WITH_SHARED_POINTER_TESTS
	SharedPointerTesting::TestSharedPointer<ESPMode::Fast>();
	SharedPointerTesting::TestSharedPointer<ESPMode::ThreadSafe>();
#endif

	// Enter main loop.
	double DeltaTime = 0.0;
	double LastTime = FPlatformTime::Seconds();
	const float IdealFrameTime = 1.0f / IDEAL_FRAMERATE;

	while (!IsEngineExitRequested())
	{
		// Save the state of the tabs here rather than after close of application (the tabs are undesirably saved out with ClosedTab state on application close).
		//UserInterfaceCommand::UserConfiguredNewLayout = FGlobalTabmanager::Get()->PersistLayout();

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
		FTicker::GetCoreTicker().Tick(DeltaTime);

		// Throttle frame rate.
		FPlatformProcess::Sleep(FMath::Max<float>(0.0f, IdealFrameTime - (FPlatformTime::Seconds() - LastTime)));

		double CurrentTime = FPlatformTime::Seconds();
		DeltaTime =  CurrentTime - LastTime;
		LastTime = CurrentTime;

		FStats::AdvanceFrame(false);

		GLog->FlushThreadedLogs(); //im: ???
	}

	//im: ??? FCoreDelegates::OnExit.Broadcast();

	ShutdownSlateApplication(UnrealInsightsLayoutIni);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::InitializeSlateApplication(const FString& LayoutIni)
{
	//TODO: FSlateApplication::InitHighDPI(true);

	// Crank up a normal Slate application using the platform's standalone renderer.
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

	// Menu anims aren't supported. See Runtime\Slate\Private\Framework\Application\MenuStack.cpp.
	FSlateApplication::Get().EnableMenuAnimations(false);

	// Set the application name.
	FGlobalTabmanager::Get()->SetApplicationTitle(NSLOCTEXT("UnrealInsights", "AppTitle", "Unreal Insights"));

	// Load widget reflector.
	const bool bAllowDebugTools = FParse::Param(FCommandLine::Get(), TEXT("DebugTools"));
	if (bAllowDebugTools)
	{
		FModuleManager::LoadModuleChecked<ISlateReflectorModule>("SlateReflector").RegisterTabSpawner(UserInterfaceCommand::DeveloperTools);
	}

	TSharedRef<FTabManager::FLayout> NewLayout = FTabManager::NewLayout("UnrealInsightsLayout_v1.0");

	// Allow TraceInsights module to update the layout.
	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	TraceInsightsModule.OnNewLayout(NewLayout);

	// Create area and tab for Slate's WidgetReflector.
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);
	NewLayout->AddArea
	(
		FTabManager::NewArea(600.0f * DPIScaleFactor, 600.0f * DPIScaleFactor)
			->SetWindow(FVector2D(10.0f * DPIScaleFactor, 10.0f * DPIScaleFactor), false)
			->Split
			(
				FTabManager::NewStack()->AddTab("WidgetReflector", bAllowDebugTools ? ETabState::OpenedTab : ETabState::ClosedTab)
			)
	);

	// Restore application layout.
	UserInterfaceCommand::ApplicationLayout = FLayoutSaveRestore::LoadFromConfig(LayoutIni, NewLayout);
	FGlobalTabmanager::Get()->RestoreFrom(UserInterfaceCommand::ApplicationLayout.ToSharedRef(), TSharedPtr<SWindow>());

	TraceInsightsModule.OnLayoutRestored(FGlobalTabmanager::Get());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::ShutdownSlateApplication(const FString& LayoutIni)
{
	check(UserInterfaceCommand::ApplicationLayout.IsValid());

	// Save application layout.
	FLayoutSaveRestore::SaveToConfig(LayoutIni, UserInterfaceCommand::ApplicationLayout.ToSharedRef());
	GConfig->Flush(false, LayoutIni);

	// Shut down application.
	FSlateApplication::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
