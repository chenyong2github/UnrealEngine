// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/QueuedThreadPool.h"
#include "Internationalization/Internationalization.h"
#include "Math/Vector2D.h"
#include "Misc/ConfigCacheIni.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/App.h"
#include "CrashReportCoreConfig.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "CrashDescription.h"
#include "CrashReportAnalytics.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformCrashContext.h"

#if !CRASH_REPORT_UNATTENDED_ONLY
	#include "SCrashReportClient.h"
	#include "CrashReportClient.h"
	#include "CrashReportClientStyle.h"
	#include "ISlateReflectorModule.h"
	#include "Framework/Application/SlateApplication.h"
#endif // !CRASH_REPORT_UNATTENDED_ONLY

#include "CrashReportCoreUnattended.h"
#include "Async/TaskGraphInterfaces.h"
#include "RequiredProgramMainCPPInclude.h"

#include "MainLoopTiming.h"

#include "PlatformErrorReport.h"
#include "XmlFile.h"

/** Default main window size */
const FVector2D InitialWindowDimensions(740, 560);

/** Average tick rate the app aims for */
const float IdealTickRate = 30.f;

/** Set this to true in the code to open the widget reflector to debug the UI */
const bool RunWidgetReflector = false;

IMPLEMENT_APPLICATION(CrashReportClient, "CrashReportClient");
DEFINE_LOG_CATEGORY(CrashReportClientLog);

/** Directory containing the report */
static TArray<FString> FoundReportDirectoryAbsolutePaths;

/** Name of the game passed via the command line. */
static FString GameNameFromCmd;

/** GUID of the crash passed via the command line. */
static FString CrashGUIDFromCmd;

/** If we are implicitly sending its assumed we are also unattended for now */
static bool bImplicitSendFromCmd = false;
/** If we want to enable analytics */
static bool AnalyticsEnabledFromCmd = true;

/** If in monitor mode, watch this pid. */
static uint64 MonitorPid = 0;

/** If in monitor mode, pipe to read data from game. */
static void* MonitorReadPipe = nullptr;

/** If in monitor mode, pipe to write data to game. */
static void* MonitorWritePipe = nullptr;

/**
 * Look for the report to upload, either in the command line or in the platform's report queue
 */
void ParseCommandLine(const TCHAR* CommandLine)
{
	const TCHAR* CommandLineAfterExe = FCommandLine::RemoveExeName(CommandLine);

	FoundReportDirectoryAbsolutePaths.Empty();

	// Use the first argument if present and it's not a flag
	if (*CommandLineAfterExe)
	{
		TArray<FString> Switches;
		TArray<FString> Tokens;
		TMap<FString, FString> Params;
		{
			FString NextToken;
			while (FParse::Token(CommandLineAfterExe, NextToken, false))
			{
				if (**NextToken == TCHAR('-'))
				{
					new(Switches)FString(NextToken.Mid(1));
				}
				else
				{
					new(Tokens)FString(NextToken);
				}
			}

			for (int32 SwitchIdx = Switches.Num() - 1; SwitchIdx >= 0; --SwitchIdx)
			{
				FString& Switch = Switches[SwitchIdx];
				TArray<FString> SplitSwitch;
				if (2 == Switch.ParseIntoArray(SplitSwitch, TEXT("="), true))
				{
					Params.Add(SplitSwitch[0], SplitSwitch[1].TrimQuotes());
					Switches.RemoveAt(SwitchIdx);
				}
			}
		}

		if (Tokens.Num() > 0)
		{
			FoundReportDirectoryAbsolutePaths.Add(Tokens[0]);
		}

		GameNameFromCmd = Params.FindRef(TEXT("AppName"));

		CrashGUIDFromCmd = FString();
		if (Params.Contains(TEXT("CrashGUID")))
		{
			CrashGUIDFromCmd = Params.FindRef(TEXT("CrashGUID"));
		}
 
		if (Switches.Contains(TEXT("ImplicitSend")))
		{
			bImplicitSendFromCmd = true;
		}

		if (Switches.Contains(TEXT("NoAnalytics")))
		{
			AnalyticsEnabledFromCmd = false;
		}

		CrashGUIDFromCmd = Params.FindRef(TEXT("CrashGUID"));
		MonitorPid = FPlatformString::Atoi64(*Params.FindRef(TEXT("MONITOR")));
		MonitorReadPipe = (void*) FPlatformString::Atoi64(*Params.FindRef(TEXT("READ")));
		MonitorWritePipe = (void*) FPlatformString::Atoi64(*Params.FindRef(TEXT("WRITE")));
	}

	if (FoundReportDirectoryAbsolutePaths.Num() == 0)
	{
		FPlatformErrorReport::FindMostRecentErrorReports(FoundReportDirectoryAbsolutePaths, FTimespan::FromDays(30));  //FTimespan::FromMinutes(30));
	}
}

/**
 * Find the error report folder and check it matches the app name if provided
 */
FPlatformErrorReport LoadErrorReport()
{
	if (FoundReportDirectoryAbsolutePaths.Num() == 0)
	{
		UE_LOG(CrashReportClientLog, Warning, TEXT("No error report found"));
		return FPlatformErrorReport();
	}

	for (const FString& ReportDirectoryAbsolutePath : FoundReportDirectoryAbsolutePaths)
	{
		FPlatformErrorReport ErrorReport(ReportDirectoryAbsolutePath);

		FString Filename;
		// CrashContext.runtime-xml has the precedence over the WER
		if (ErrorReport.FindFirstReportFileWithExtension(Filename, *FGenericCrashContext::CrashContextExtension))
		{
			FPrimaryCrashProperties::Set(new FCrashContext(ReportDirectoryAbsolutePath / Filename));
		}
		else if (ErrorReport.FindFirstReportFileWithExtension(Filename, TEXT(".xml")))
		{
			FPrimaryCrashProperties::Set(new FCrashWERContext(ReportDirectoryAbsolutePath / Filename));
		}
		else
		{
			continue;
		}

#if CRASH_REPORT_UNATTENDED_ONLY
		return ErrorReport;
#else
		bool NameMatch = false;
		if (GameNameFromCmd.IsEmpty() || GameNameFromCmd == FPrimaryCrashProperties::Get()->GameName)
		{
			NameMatch = true;
		}

		bool GUIDMatch = false;
		if (CrashGUIDFromCmd.IsEmpty() || CrashGUIDFromCmd == FPrimaryCrashProperties::Get()->CrashGUID)
		{
			GUIDMatch = true;
		}

		if (NameMatch && GUIDMatch)
		{
			FString ConfigFilename;
			if (ErrorReport.FindFirstReportFileWithExtension(ConfigFilename, *FGenericCrashContext::CrashConfigExtension))
			{
				FConfigFile CrashConfigFile;
				CrashConfigFile.Read(ReportDirectoryAbsolutePath / ConfigFilename);
				FCrashReportCoreConfig::Get().SetProjectConfigOverrides(CrashConfigFile);
			}

			return ErrorReport;
		}
#endif
	}

	// Don't display or upload anything if we can't find the report we expected
	return FPlatformErrorReport();
}

static void OnRequestExit()
{
	GIsRequestingExit = true;
}

#if !CRASH_REPORT_UNATTENDED_ONLY
bool RunWithUI(FPlatformErrorReport ErrorReport)
{
	// create the platform slate application (what FSlateApplication::Get() returns)
	TSharedRef<FSlateApplication> Slate = FSlateApplication::Create(MakeShareable(FPlatformApplicationMisc::CreateApplication()));

	// initialize renderer
	TSharedRef<FSlateRenderer> SlateRenderer = GetStandardStandaloneRenderer();

	// Grab renderer initialization retry settings from ini
	int32 SlateRendererInitRetryCount = 10;
	GConfig->GetInt(TEXT("CrashReportClient"), TEXT("UIInitRetryCount"), SlateRendererInitRetryCount, GEngineIni);
	double SlateRendererInitRetryInterval = 2.0;
	GConfig->GetDouble(TEXT("CrashReportClient"), TEXT("UIInitRetryInterval"), SlateRendererInitRetryInterval, GEngineIni);

	// Try to initialize the renderer. It's possible that we launched when the driver crashed so try a few times before giving up.
	bool bRendererInitialized = false;
	bool bRendererFailedToInitializeAtLeastOnce = false;
	do 
	{
		SlateRendererInitRetryCount--;
		bRendererInitialized = FSlateApplication::Get().InitializeRenderer(SlateRenderer, true);
		if (!bRendererInitialized && SlateRendererInitRetryCount > 0)
		{
			bRendererFailedToInitializeAtLeastOnce = true;
			FPlatformProcess::Sleep(SlateRendererInitRetryInterval);
		}
	} while (!bRendererInitialized && SlateRendererInitRetryCount > 0);

	if (!bRendererInitialized)
	{
		// Close down the Slate application
		FSlateApplication::Shutdown();
		return false;
	}
	else if (bRendererFailedToInitializeAtLeastOnce)
	{
		// Wait until the driver is fully restored
		FPlatformProcess::Sleep(2.0f);

		// Update the display metrics
		FDisplayMetrics DisplayMetrics;
		FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);
		FSlateApplication::Get().GetPlatformApplication()->OnDisplayMetricsChanged().Broadcast(DisplayMetrics);
	}

	// Set up the main ticker
	FMainLoopTiming MainLoop(IdealTickRate, EMainLoopOptions::UsingSlate);

	// set the normal UE4 GIsRequestingExit when outer frame is closed
	FSlateApplication::Get().SetExitRequestedHandler(FSimpleDelegate::CreateStatic(&OnRequestExit));

	// Prepare the custom Slate styles
	FCrashReportClientStyle::Initialize();

	// Create the main implementation object
	TSharedRef<FCrashReportClient> CrashReportClient = MakeShareable(new FCrashReportClient(ErrorReport));

	// open up the app window	
	TSharedRef<SCrashReportClient> ClientControl = SNew(SCrashReportClient, CrashReportClient);

	TSharedRef<SWindow> Window = FSlateApplication::Get().AddWindow(
		SNew(SWindow)
		.Title(NSLOCTEXT("CrashReportClient", "CrashReportClientAppName", "Unreal Engine 4 Crash Reporter"))
		.HasCloseButton(FCrashReportCoreConfig::Get().IsAllowedToCloseWithoutSending())
		.ClientSize(InitialWindowDimensions)
		[
			ClientControl
		]);

	Window->SetRequestDestroyWindowOverride(FRequestDestroyWindowOverride::CreateSP(CrashReportClient, &FCrashReportClient::RequestCloseWindow));

	// Setting focus seems to have to happen after the Window has been added
	FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);

	// Debugging code
	if (RunWidgetReflector)
	{
		FModuleManager::LoadModuleChecked<ISlateReflectorModule>("SlateReflector").DisplayWidgetReflector();
	}

	// loop until the app is ready to quit
	while (!GIsRequestingExit)
	{
		MainLoop.Tick();

		if (CrashReportClient->ShouldWindowBeHidden())
		{
			Window->HideWindow();
		}
	}

	// Make sure the window is hidden, because it might take a while for the background thread to finish.
	Window->HideWindow();

	// Stop the background thread
	CrashReportClient->StopBackgroundThread();

	// Clean up the custom styles
	FCrashReportClientStyle::Shutdown();

	// Close down the Slate application
	FSlateApplication::Shutdown();

	return true;
}
#endif // !CRASH_REPORT_UNATTENDED_ONLY

// When we want to implicitly send and use unattended we still want to show a message box of a crash if possible
class FMessageBoxThread : public FRunnable
{
	virtual uint32 Run() override
	{
		// We will not have any GUI for the crash reporter if we are sending implicitly, so pop a message box up at least
		if (FApp::CanEverRender())
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
				*NSLOCTEXT("MessageDialog", "ReportCrash_Body", "The application has crashed and will now close. We apologize for the inconvenience.").ToString(),
				*NSLOCTEXT("MessageDialog", "ReportCrash_Title", "Application Crash Detected").ToString());
		}

		return 0;
	}
};

void RunUnattended(FPlatformErrorReport ErrorReport)
{
	// Set up the main ticker
	FMainLoopTiming MainLoop(IdealTickRate, EMainLoopOptions::CoreTickerOnly);

	// In the unattended mode we don't send any PII.
	FCrashReportCoreUnattended CrashReportClient(ErrorReport);
	ErrorReport.SetUserComment(NSLOCTEXT("CrashReportClient", "UnattendedMode", "Sent in the unattended mode"));

	FMessageBoxThread MessageBox;
	FRunnableThread* MessageBoxThread = nullptr;

	if (bImplicitSendFromCmd)
	{
		MessageBoxThread = FRunnableThread::Create(&MessageBox, TEXT("CrashReporter_MessageBox"));
	}

	// loop until the app is ready to quit
	while (!GIsRequestingExit)
	{
		MainLoop.Tick();
	}

	if (bImplicitSendFromCmd && MessageBoxThread)
	{
		MessageBoxThread->WaitForCompletion();
	}
}

FPlatformErrorReport CollectErrorReport(uint32 Pid, const FSharedCrashContext& SharedCrashContext, void* WritePipe) 
{
	// @note: This API is only partially implemented on Mac OS and Linux.
	FProcHandle ProcessHandle = FPlatformProcess::OpenProcess(Pid);

	// First init the static crash context state
	FPlatformCrashContext::InitializeFromContext(
		SharedCrashContext.SessionContext,
		SharedCrashContext.EnabledPluginsNum > 0 ? &SharedCrashContext.DynamicData[SharedCrashContext.EnabledPluginsOffset] : nullptr,
		SharedCrashContext.EngineDataNum > 0 ? &SharedCrashContext.DynamicData[SharedCrashContext.EngineDataOffset] : nullptr,
		SharedCrashContext.GameDataNum > 0 ? &SharedCrashContext.DynamicData[SharedCrashContext.GameDataOffset] : nullptr
	);
	// Next create a crash context for the crashed process.
	FPlatformCrashContext CrashContext(SharedCrashContext.CrashType, SharedCrashContext.ErrorMessage);
	CrashContext.SetCrashedProcess(ProcessHandle);
	CrashContext.SetCrashedThreadId(SharedCrashContext.CrashingThreadId);
	CrashContext.SetNumMinidumpFramesToIgnore(SharedCrashContext.NumStackFramesToIgnore);

	// Initialize the stack walking for the monitored process
	FPlatformStackWalk::InitStackWalkingForProcess(ProcessHandle);

	for (uint32 ThreadIdx = 0; ThreadIdx < SharedCrashContext.NumThreads; ThreadIdx++)
	{
		const uint32 ThreadId = SharedCrashContext.ThreadIds[ThreadIdx];
		uint64 StackFrames[CR_MAX_STACK_FRAMES] = {0};
		const uint32 StackFrameCount = FPlatformStackWalk::CaptureThreadStackBackTrace(
			ThreadId, 
			StackFrames,
			CR_MAX_STACK_FRAMES
		);

		CrashContext.AddPortableThreadCallStack(
			SharedCrashContext.ThreadIds[ThreadIdx],
			&SharedCrashContext.ThreadNames[ThreadIdx*CR_MAX_THREAD_NAME_CHARS],
			StackFrames,
			StackFrameCount
		);

		// Add the crashing stack specifically. Is this really needed?
		if (ThreadId == SharedCrashContext.CrashingThreadId)
		{
			CrashContext.SetPortableCallStack(
				StackFrames + SharedCrashContext.NumStackFramesToIgnore,
				StackFrameCount - SharedCrashContext.NumStackFramesToIgnore
			);
		}
	}

	// Setup the FPrimaryCrashProperties singleton. If the path is not set it is most likely
	// that we have crashed during static init, in which case we need to construct a directory
	// ourself.
	CrashContext.SerializeContentToBuffer();

	FString ReportDirectoryAbsolutePath(SharedCrashContext.CrashFilesDirectory);
	bool DirectoryExists = true;
	if (ReportDirectoryAbsolutePath.IsEmpty())
	{
		DirectoryExists = FGenericCrashContext::CreateCrashReportDirectory(
			SharedCrashContext.SessionContext.CrashGUIDRoot,
			SharedCrashContext.SessionContext.GameName,
			0,
			ReportDirectoryAbsolutePath);
	}

	// Copy platform specific files (e.g. minidump) to output directory if it exists
	if (DirectoryExists)
	{
		CrashContext.CopyPlatformSpecificFiles(*ReportDirectoryAbsolutePath, SharedCrashContext.PlatformCrashContext);
	}

	// At this point the game can continue execution. It is important this happens
	// as soon as thread state and minidump has been created, so that ensures cause
	// as little hitch as possible.
	uint8 ResponseCode[] = { 0xd, 0xe, 0xa, 0xd };
	FPlatformProcess::WritePipe(WritePipe, ResponseCode, sizeof(ResponseCode));

	// Write out the XML file.
	const FString CrashContextXMLPath = FPaths::Combine(*ReportDirectoryAbsolutePath, FPlatformCrashContext::CrashContextRuntimeXMLNameW);
	CrashContext.SerializeAsXML(*CrashContextXMLPath);

	const TCHAR* CrachContextBuffer = *CrashContext.GetBuffer();
	FPrimaryCrashProperties::Set(new FCrashContext(ReportDirectoryAbsolutePath / TEXT("CrashContext.runtime-xml"), CrachContextBuffer));

	FPlatformErrorReport ErrorReport(ReportDirectoryAbsolutePath);

#if CRASH_REPORT_UNATTENDED_ONLY
	return ErrorReport;
#else


	FString ConfigFilename;
	if (ErrorReport.FindFirstReportFileWithExtension(ConfigFilename, *FGenericCrashContext::CrashConfigExtension))
	{
		FConfigFile CrashConfigFile;
		CrashConfigFile.Read(ReportDirectoryAbsolutePath / ConfigFilename);
		FCrashReportCoreConfig::Get().SetProjectConfigOverrides(CrashConfigFile);
	}

	return ErrorReport;
#endif
}

void SendErrorReport(FPlatformErrorReport& ErrorReport)
{
	if (!GIsRequestingExit && ErrorReport.HasFilesToUpload() && FPrimaryCrashProperties::Get() != nullptr)
	{
		const bool bUnattended =
#if CRASH_REPORT_UNATTENDED_ONLY
			true;
#else
			FApp::IsUnattended();
#endif // CRASH_REPORT_UNATTENDED_ONLY

		ErrorReport.SetCrashReportClientVersion(FCrashReportCoreConfig::Get().GetVersion());

		if (bUnattended)
		{
			RunUnattended(ErrorReport);
		}
#if !CRASH_REPORT_UNATTENDED_ONLY
		else
		{
			if (!RunWithUI(ErrorReport))
			{
				// UI failed to initialize, probably due to driver crash. Send in unattended mode if allowed.
				bool bCanSendWhenUIFailedToInitialize = true;
				GConfig->GetBool(TEXT("CrashReportClient"), TEXT("CanSendWhenUIFailedToInitialize"), bCanSendWhenUIFailedToInitialize, GEngineIni);
				if (bCanSendWhenUIFailedToInitialize && !FCrashReportCoreConfig::Get().IsAllowedToCloseWithoutSending())
				{
					RunUnattended(ErrorReport);
				}
			}
		}
#endif // !CRASH_REPORT_UNATTENDED_ONLY

	}
}

bool WaitForCrash(uint32 WatchedProcess, FSharedCrashContext& CrashContext, void* ReadPipe)
{
	TArray<uint8> Buffer;

	while (FPlatformProcess::IsApplicationAlive(WatchedProcess))
	{
		if (FPlatformProcess::ReadPipeToArray(ReadPipe, Buffer))
		{
			FPlatformMemory::Memcpy(&CrashContext, Buffer.GetData(), Buffer.Num());
			return true;
		}
		
		FPlatformProcess::Sleep(1.0f);
	}

	return false;
}

void RunCrashReportClient(const TCHAR* CommandLine)
{
	// Override the stack size for the thread pool.
	FQueuedThreadPool::OverrideStackSize = 256 * 1024;

	// Increase the HttpSendTimeout to 5 minutes
	GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpSendTimeout"), 5 * 60.0f, GEngineIni);

	// Set up the main loop
	GEngineLoop.PreInit(CommandLine);

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();

	// Tell the module manager is may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	// Initialize config.
	FCrashReportCoreConfig::Get();

	// Find the report to upload in the command line arguments
	ParseCommandLine(CommandLine);

	FPlatformErrorReport::Init();
	if (AnalyticsEnabledFromCmd)
	{
		FCrashReportAnalytics::Initialize();
	}

	if (MonitorPid && MonitorWritePipe && MonitorReadPipe) 
	{
		while (FPlatformProcess::IsApplicationAlive(MonitorPid))
		{
			// Wait for parent process to signal crash
			FSharedCrashContext CrashContext;
			if (WaitForCrash(MonitorPid, CrashContext, MonitorReadPipe))
			{
				// Build error report in memory
				FPlatformErrorReport ErrorReport = CollectErrorReport(MonitorPid, CrashContext, MonitorWritePipe);
				SendErrorReport(ErrorReport);
			}
		}
	}
	else
	{
		// Load error report generated by the process from disk
		FPlatformErrorReport ErrorReport = LoadErrorReport();
		SendErrorReport(ErrorReport);
	}

	if (AnalyticsEnabledFromCmd)
	{
		FCrashReportAnalytics::Shutdown();
	}

	FPrimaryCrashProperties::Shutdown();
	FPlatformErrorReport::ShutDown();

	GIsRequestingExit = true;

	FEngineLoop::AppPreExit();
	FModuleManager::Get().UnloadModulesAtShutdown();
	FTaskGraphInterface::Shutdown();

	FEngineLoop::AppExit();
}
