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

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#if PLATFORM_UNIX
#include <sys/file.h>
#include <errno.h>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

#define IDEAL_FRAMERATE 60

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UserInterfaceCommand
{
	TSharedRef<FWorkspaceItem> DeveloperTools = FWorkspaceItem::NewGroup(NSLOCTEXT("UnrealInsights", "DeveloperToolsMenu", "Developer Tools"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool CheckSessionBrowserSingleInstance()
{
#if PLATFORM_WINDOWS
	// Create a named event that other processes can use to detect a running recorder and connect to it automatically.
	// See usage in \Engine\Source\Runtime\Launch\Private\LaunchEngineLoop.cpp
	HANDLE SessionBrowserEvent = CreateEvent(NULL, true, false, TEXT("Local\\UnrealInsightsRecorder"));
	if (SessionBrowserEvent == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// Another Session Browser process is already running.

		if (SessionBrowserEvent != NULL)
		{
			CloseHandle(SessionBrowserEvent);
		}

		// Activate the respective window.
		HWND Window = FindWindowW(0, L"Unreal Insights");
		if (Window)
		{
			ShowWindow(Window, SW_SHOW);
			SetForegroundWindow(Window);

			FLASHWINFO FlashInfo;
			FlashInfo.cbSize = sizeof(FLASHWINFO);
			FlashInfo.hwnd = Window;
			FlashInfo.dwFlags = FLASHW_ALL;
			FlashInfo.uCount = 3;
			FlashInfo.dwTimeout = 0;
			FlashWindowEx(&FlashInfo);
		}

		return false;
	}
#endif // PLATFORM_WINDOWS

#if PLATFORM_UNIX
	int FileHandle = open("/var/run/UnrealInsights.pid", O_CREAT | O_RDWR, 0666);
	int Ret = flock(FileHandle, LOCK_EX | LOCK_NB);
	if (Ret && EWOULDBLOCK == errno)
	{
		// Another Session Browser process is already running.

		// Activate the respective window.
		//TODO: "wmctrl -a Insights"

		return false;
	}
#endif

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::Run()
{
	const uint32 MaxPath = FPlatformMisc::GetMaxPathLength();
	TCHAR* TraceFile = new TCHAR[MaxPath + 1];
	TraceFile[0] = 0;
	bool bOpenTraceFile = false;

	// Only a single instance of Session Browser window/process is allowed.
	{
		bool bBrowserMode = true;

		if (bBrowserMode)
		{
			bBrowserMode = FCString::Strifind(FCommandLine::Get(), TEXT("-OpenTraceId=")) == nullptr;
		}
		if (bBrowserMode)
		{
			bOpenTraceFile = GetTraceFileFromCmdLine(TraceFile, MaxPath);
			bBrowserMode = !bOpenTraceFile;
		}

		if (bBrowserMode && !CheckSessionBrowserSingleInstance())
		{
			return;
		}
	}

	FCoreStyle::ResetToDefault();

	// Crank up a normal Slate application using the platform's standalone renderer.
	FSlateApplication::InitializeAsStandaloneApplication(GetStandardStandaloneRenderer());

	// Load required modules.
	FModuleManager::Get().LoadModuleChecked("EditorStyle");
	FModuleManager::Get().LoadModuleChecked("TraceInsights");

	// Load plug-ins.
	// @todo: allow for better plug-in support in standalone Slate applications
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::PreDefault);
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::Default);

	// Load optional modules.
	if (FModuleManager::Get().ModuleExists(TEXT("SettingsEditor")))
	{
		FModuleManager::Get().LoadModule("SettingsEditor");
	}

	InitializeSlateApplication(bOpenTraceFile, TraceFile);

	delete[] TraceFile;
	TraceFile = nullptr;

	// Initialize source code access.
	// Load the source code access module.
	ISourceCodeAccessModule& SourceCodeAccessModule = FModuleManager::LoadModuleChecked<ISourceCodeAccessModule>(FName("SourceCodeAccess"));

	// Manually load in the source code access plugins, as standalone programs don't currently support plugins.
#if PLATFORM_MAC
	IModuleInterface& XCodeSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>(FName("XCodeSourceCodeAccess"));
	SourceCodeAccessModule.SetAccessor(FName("XCodeSourceCodeAccess"));
#elif PLATFORM_WINDOWS
	IModuleInterface& VisualStudioSourceCodeAccessModule = FModuleManager::LoadModuleChecked<IModuleInterface>(FName("VisualStudioSourceCodeAccess"));
	SourceCodeAccessModule.SetAccessor(FName("VisualStudioSourceCodeAccess"));
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

		FCoreDelegates::OnEndFrame.Broadcast();
		GLog->FlushThreadedLogs(); //im: ???
	}

	//im: ??? FCoreDelegates::OnExit.Broadcast();

	ShutdownSlateApplication();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FUserInterfaceCommand::InitializeSlateApplication(bool bOpenTraceFile, const TCHAR* TraceFile)
{
	//TODO: FSlateApplication::InitHighDPI(true);

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

	uint32 TraceId = 0;
	bool bUseTraceId = FParse::Value(FCommandLine::Get(), TEXT("-OpenTraceId="), TraceId);

	TCHAR* StoreHost = new TCHAR[MaxPath + 1];
	FCString::Strcpy(StoreHost, MaxPath, TEXT("127.0.0.1"));
	uint32 StorePort = 0;
	bool bUseCustomStoreAddress = false;

	if (FParse::Value(FCommandLine::Get(), TEXT("-Store="), StoreHost, MaxPath, true))
	{
		TCHAR* Port = FCString::Strchr(StoreHost, TEXT(':'));
		if (Port)
		{
			*Port = 0;
			Port++;
			StorePort = FCString::Atoi(Port);
		}
		bUseCustomStoreAddress = true;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("-StoreHost="), StoreHost, MaxPath, true))
	{
		bUseCustomStoreAddress = true;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("-StorePort="), StorePort))
	{
		bUseCustomStoreAddress = true;
	}

	TCHAR Cmd[1024];
	bool bExecuteCommand = false;
	if (FParse::Value(FCommandLine::Get(), TEXT("-ExecOnAnalysisCompleteCmd="), Cmd, 1024, false))
	{
		bExecuteCommand = true;
	}

	//This parameter will cause the application to close when analysis fails to start or completes succesfully
	const bool bAutoQuit = FParse::Param(FCommandLine::Get(), TEXT("AutoQuit"));

	const bool bInitializeTesting = FParse::Param(FCommandLine::Get(), TEXT("InsightsTest"));

	if (bUseTraceId)
	{
		if (bInitializeTesting || bAutoQuit)
		{
			TraceInsightsModule.InitializeTesting(bInitializeTesting, bAutoQuit);

			if (bExecuteCommand)
			{
				TraceInsightsModule.ScheduleCommand(Cmd);
			}
		}

		TraceInsightsModule.CreateSessionViewer(bAllowDebugTools);
		TraceInsightsModule.ConnectToStore(StoreHost, StorePort);
		TraceInsightsModule.StartAnalysisForTrace(TraceId, bAutoQuit);
	}
	else
	{
		if (bOpenTraceFile)
		{
			if (bInitializeTesting || bAutoQuit)
			{
				TraceInsightsModule.InitializeTesting(bInitializeTesting, bAutoQuit);

				if (bExecuteCommand)
				{
					TraceInsightsModule.ScheduleCommand(Cmd);
				}
			}

			TraceInsightsModule.CreateSessionViewer(bAllowDebugTools);
			TraceInsightsModule.StartAnalysisForTraceFile(TraceFile, bAutoQuit);
		}
		else
		{
			if (!bUseCustomStoreAddress)
			{
				TraceInsightsModule.CreateDefaultStore();
			}
			else
			{
				TraceInsightsModule.ConnectToStore(StoreHost, StorePort);
			}
			const bool bSingleProcess = FParse::Param(FCommandLine::Get(), TEXT("SingleProcess"));
			TraceInsightsModule.CreateSessionBrowser(bAllowDebugTools, bSingleProcess);
		}
	}

	delete[] StoreHost;
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

bool FUserInterfaceCommand::GetTraceFileFromCmdLine(TCHAR* OutTraceFile, uint32 MaxPath)
{
	// Try getting the trace file from the -OpenTraceFile= paramter first.
	bool bUseTraceFile = FParse::Value(FCommandLine::Get(), TEXT("-OpenTraceFile="), OutTraceFile, MaxPath, true);

	if (bUseTraceFile)
	{
		return true;
	}

	// Support opening a trace file by double clicking a .utrace file.
	// In this case, the app will receive as the first parameter a utrace file path.

	const TCHAR* CmdLine = FCommandLine::Get();
	bool HasToken = FParse::Token(CmdLine, OutTraceFile, MaxPath, false);

	if (HasToken)
	{
		FString Token = OutTraceFile;
		if (Token.EndsWith(TEXT(".utrace")))
		{
			bUseTraceFile = true;
		}
	}

	return bUseTraceFile;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
