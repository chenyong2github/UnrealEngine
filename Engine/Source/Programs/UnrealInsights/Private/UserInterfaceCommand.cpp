// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserInterfaceCommand.h"

#include "Async/TaskGraphInterfaces.h"
//#include "Brushes/SlateImageBrush.h"
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
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "StandaloneRenderer.h"
#include "Widgets/Docking/SDockTab.h"

// Insights
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/Version.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define IDEAL_FRAMERATE 60

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UserInterfaceCommand
{
	TSharedRef<FWorkspaceItem> DeveloperTools = FWorkspaceItem::NewGroup(NSLOCTEXT("UnrealInsights", "DeveloperToolsMenu", "Developer Tools"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::Run()
{
	FCoreStyle::ResetToDefault();

	// Load required modules.
	FModuleManager::Get().LoadModuleChecked("EditorStyle");
	FModuleManager::Get().LoadModuleChecked("TraceInsights");

	// Load plug-ins.
	// @todo: allow for better plug-in support in standalone Slate applications
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::PreDefault);

	// Load optional modules.
	FModuleManager::Get().LoadModule("SettingsEditor");

	InitializeSlateApplication();

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

	ShutdownSlateApplication();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::InitializeSlateApplication()
{
	//TODO: FSlateApplication::InitHighDPI(true);

	// Crank up a normal Slate application using the platform's standalone renderer.
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

	//const FSlateBrush* AppIcon = new FSlateImageBrush(FPaths::EngineContentDir() / "Editor/Slate/Icons/Insights/AppIcon_24x.png", FVector2D(24.0f, 24.0f));
	//FSlateApplication::Get().SetAppIcon(AppIcon);

	// Menu anims aren't supported. See Runtime\Slate\Private\Framework\Application\MenuStack.cpp.
	FSlateApplication::Get().EnableMenuAnimations(false);

	// Set the application name.
	const FText ApplicationTitle = FText::Format(NSLOCTEXT("UnrealInsights", "AppTitle", "Unreal Insights {0}"), FText::FromString(TEXT(UNREAL_INSIGHTS_VERSION_STRING_EX)));
	FGlobalTabmanager::Get()->SetApplicationTitle(ApplicationTitle);

	// Load widget reflector.
	const bool bAllowDebugTools = FParse::Param(FCommandLine::Get(), TEXT("DebugTools"));
	if (bAllowDebugTools)
	{
		FModuleManager::LoadModuleChecked<ISlateReflectorModule>("SlateReflector").RegisterTabSpawner(UserInterfaceCommand::DeveloperTools);
	}

	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	const uint32 MaxPath = FPlatformMisc::GetMaxPathLength();
	TCHAR* TraceFile = new TCHAR[MaxPath + 1];
	TraceFile[0] = 0;
	bool bUseTraceFile = FParse::Value(FCommandLine::Get(), TEXT("-Trace="), TraceFile, MaxPath, true);

	if (bUseTraceFile)
	{
		TraceInsightsModule.CreateSessionViewer(bAllowDebugTools);
		TraceInsightsModule.StartAnalysisForTraceFile(TraceFile);
	}
	else
	{
		const bool bSingleProcess = FParse::Param(FCommandLine::Get(), TEXT("SingleProcess"));
		TraceInsightsModule.CreateSessionBrowser(bAllowDebugTools, bSingleProcess);
	}

	delete[] TraceFile;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::ShutdownSlateApplication()
{
	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	TraceInsightsModule.ShutdownUserInterface();

	// Shut down application.
	FSlateApplication::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
