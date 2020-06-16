// Copyright Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"
#include "CrashReportClientDefines.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/QueuedThreadPool.h"
#include "Internationalization/Internationalization.h"
#include "Math/Vector2D.h"
#include "Misc/ConfigCacheIni.h"
#include "GenericPlatform/GenericApplication.h"
#include "Misc/App.h"
#include "Misc/CString.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "CrashReportCoreConfig.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "CrashDescription.h"
#include "CrashReportAnalytics.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformCrashContext.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "IAnalyticsProviderET.h"
#include "XmlParser.h"
#include "Containers/Map.h"

#if !CRASH_REPORT_UNATTENDED_ONLY
	#include "SCrashReportClient.h"
	#include "CrashReportClient.h"
	#include "CrashReportClientStyle.h"
#if !UE_BUILD_SHIPPING
	#include "ISlateReflectorModule.h"
#endif
	#include "Framework/Application/SlateApplication.h"
#endif // !CRASH_REPORT_UNATTENDED_ONLY

#include "CrashReportCoreUnattended.h"
#include "Async/TaskGraphInterfaces.h"
#include "RequiredProgramMainCPPInclude.h"

#include "MainLoopTiming.h"

#include "PlatformErrorReport.h"
#include "XmlFile.h"
#include "RecoveryService.h"

#if CRASH_REPORT_WITH_MTBF
#include "EditorAnalyticsSession.h"
#include "EditorSessionSummarySender.h"
#endif

class FRecoveryService;

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

/**
 * Write a mini log of important events such as the crash GUID(s) to attach to the editor summary analytic event. This is to diagnose abnormal terminations
 * of the Editor or crash in CRC itself. Each log entry is expected to be small and concise. Each instance creates a single log file identified by the CRC
 * process ID. When CRC (compiled with MTBF support) is about to exit, it reloads the existing log files and pass them to EditorSessionSummarySender. When
 * the summary sender is about to send a session, it check the session status, if an error occurred, it tries to match a mini-log and if the corresponding
 * log is found, it is attached to the session summary.
 */
class FDiagnosticLogger : public FOutputDevice
{
public:
	static constexpr int32 MaxLogLen = 8 * 1024;

	static FDiagnosticLogger& Get()
	{
		static FDiagnosticLogger Instance;
		return Instance;
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		// Log the errors, especially the failed 'check()' with the callstack/message.
		if (Verbosity == ELogVerbosity::Error)
		{
			LogEvent(TEXT("CRC/Error"));
			LogEvent(V, /*bForwardToUELog*/false); // This log is already going to UE_LOG, don't need to forward it.
		}
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override
	{
		Serialize(V, Verbosity, Category);
	}

	/** Log a small events to help diagnose abnormal shutdown or bugs in CRC itself. The event text is expected to be short and concise. */
	void LogEvent(const TCHAR* Event, bool bForwardToUELog = true)
	{
		if (IsEnabled())
		{
			FScopeLock ScopedLock(&LoggerLock);

			// Add the separator if some text is already logged.
			if (DiagnosticInfo.Len())
			{
				DiagnosticInfo.Append(TEXT("|"));
			}

			// Rotate the log if it gets too long.
			int32 FreeLen = MaxLogLen - DiagnosticInfo.Len();
			int32 EventLen = FCString::Strlen(Event);
			if (EventLen > FreeLen)
			{
				if (EventLen > MaxLogLen)
				{
					DiagnosticInfo.Reset(MaxLogLen);
					EventLen = MaxLogLen;
				}
				else
				{
					DiagnosticInfo.RemoveAt(0, EventLen - FreeLen, /*bAllowShrinking*/false); // Free space, remove the chars from the oldest events (in front).
				}
			}

			// Append the log entry and dump the log to the file.
			DiagnosticInfo.AppendChars(Event, EventLen);
			FFileHelper::SaveStringToFile(DiagnosticInfo, *GetLogPathname());

			// Prevent error logs coming from the logging system to be duplicated.
			if (bForwardToUELog)
			{
				UE_LOG(CrashReportClientLog, Log, TEXT("%s"), Event);
			}
		}
	}

	void LogEvent(const FString& Event)
	{
		LogEvent(*Event);
	}

	static TMap<uint32, FString> LoadAllLogs()
	{
		TMap<uint32, FString> Logs;

		IFileManager::Get().IterateDirectory(*GetLogDir(), [&Logs](const TCHAR* Pathname, bool bIsDir)
		{
			if (!bIsDir)
			{
				FString Filename = FPaths::GetCleanFilename(Pathname);
				if (Filename.StartsWith(GetBaseFilename()) && Filename.EndsWith(TEXT(".log")))
				{
					uint32 ProcessID = GetLogProcessId(Filename);
					if (ProcessID == FPlatformProcess::GetCurrentProcessId() || !FPlatformProcess::IsApplicationRunning(ProcessID)) // Don't load the log of another running CrashReportClient.
					{
						FString MonitorLog;
						if (FFileHelper::LoadFileToString(MonitorLog, Pathname))
						{
							Logs.Emplace(ProcessID, MoveTemp(MonitorLog));
						}
					}
				}
			}
			return true; // Continue iterating the directory.
		});

		return Logs;
	}

	static void ClearAllLogs()
	{
		IFileManager::Get().IterateDirectory(*GetLogDir(), [](const TCHAR* Pathname, bool bIsDir)
		{
			if (!bIsDir)
			{
				FString Filename = FPaths::GetCleanFilename(Pathname);
				if (Filename.StartsWith(GetBaseFilename()) && Filename.EndsWith(TEXT(".log")))
				{
					uint32 ProcessID = GetLogProcessId(Filename);
					if (ProcessID == FPlatformProcess::GetCurrentProcessId() || !FPlatformProcess::IsApplicationRunning(ProcessID)) // Don't delete the log of another running CrashReportClient.
					{
						IFileManager::Get().Delete(Pathname);
					}
				}
			}
			return true; // Continue iterating the directory.
		});
	}

private:
	FDiagnosticLogger()
	{
		if (IsEnabled())
		{
			// Ensure the Log directory exists.
			IFileManager::Get().MakeDirectory(*GetLogDir(), /*Tree*/true);

			// Delete the previous file (if any was left).
			IFileManager::Get().Delete(*GetLogPathname(), /*bRequireExits*/false);

			// Reserve the memory for the log string.
			DiagnosticInfo.Reset(MaxLogLen);
		}
	}

	static bool IsEnabled()
	{
		// Only log if MTBF is enabled. In this mode, the mini-log created is attached to the Editor session summary to diagnose problems in CRC or help
		// figure out Editor abnormal terminations.
		return CRASH_REPORT_WITH_MTBF != 0;
	}

	static const FString& GetLogDir()
	{
		static FString LogDir(FPlatformProcess::UserTempDir()); // This folder (and API) doesn't rely on the engine being initialized and can be use very early.
		return LogDir;
	}

	static const TCHAR* GetBaseFilename()
	{
		return TEXT("UnrealCrcDiagnosticMiniLog");
	}

	static const FString& GetLogPathname()
	{
		static FString LogPathname(GetLogDir() / FString::Printf(TEXT("%s_%s.log"), GetBaseFilename(), *LexToString(FPlatformProcess::GetCurrentProcessId())));
		return LogPathname;
	}

	static uint32 GetLogProcessId(const FString& Filename)
	{
		// Parse the PID from a filename like: CrcDiagLog_939399.log
		int Start;
		int End;
		if (!Filename.FindChar(TEXT('_'), Start))
		{
			return 0;
		}
		else if (!Filename.FindChar(TEXT('.'), End))
		{
			return 0;
		}

		FString ProcessIdStr = Filename.Mid(Start + 1, End - Start);
		return FCString::Atoi(*ProcessIdStr);
	}

private:
	FString DiagnosticInfo;
	FCriticalSection LoggerLock;
};

// This extra function can be declared external in the platform specific code. (This avoid creating an extra file for just one function).
// It also prevent logging before the engine loop is fully initialized.
void LogCrcEvent(const TCHAR* Event)
{
	FDiagnosticLogger::Get().LogEvent(Event);
}

/** If in monitor mode, pipe to read data from game. */
static void* MonitorReadPipe = nullptr;

/** If in monitor mode, pipe to write data to game. */
static void* MonitorWritePipe = nullptr;

/** Result of submission of report */
enum SubmitCrashReportResult {
	Failed,				// Failed to send report
	SuccessClosed,		// Succeeded sending report, user has not elected to relaunch
	SuccessRestarted,	// Succeeded sending report, user has elected to restart process
	SuccessContinue		// Succeeded sending report, continue running (if monitor mode).
};

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
		if (ErrorReport.FindFirstReportFileWithExtension(Filename, FGenericCrashContext::CrashContextExtension))
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
			if (ErrorReport.FindFirstReportFileWithExtension(ConfigFilename, FGenericCrashContext::CrashConfigExtension))
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
	RequestEngineExit(TEXT("OnRequestExit"));
}

#if !CRASH_REPORT_UNATTENDED_ONLY
SubmitCrashReportResult RunWithUI(FPlatformErrorReport ErrorReport)
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
		return Failed;
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

	// set the normal UE4 IsEngineExitRequested() when outer frame is closed
	FSlateApplication::Get().SetExitRequestedHandler(FSimpleDelegate::CreateStatic(&OnRequestExit));

	// Prepare the custom Slate styles
	FCrashReportClientStyle::Initialize();

	// Create the main implementation object
	TSharedRef<FCrashReportClient> CrashReportClient = MakeShared<FCrashReportClient>(ErrorReport);

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

#if !UE_BUILD_SHIPPING
	// Debugging code
	if (RunWidgetReflector)
	{
		FModuleManager::LoadModuleChecked<ISlateReflectorModule>("SlateReflector").DisplayWidgetReflector();
	}
#endif

	// Bring the window to the foreground as it may be behind the crashed process
	Window->HACK_ForceToFront();
	Window->BringToFront();

	// loop until the app is ready to quit
	while (!(IsEngineExitRequested() || CrashReportClient->IsUploadComplete()))
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

	// Detect if ensure, if user has selected to restart or close.	
	return CrashReportClient->GetIsSuccesfullRestart() ? SuccessRestarted : (FPrimaryCrashProperties::Get()->bIsEnsure ? SuccessContinue : SuccessClosed);
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

SubmitCrashReportResult RunUnattended(FPlatformErrorReport ErrorReport, bool bImplicitSend)
{
	// Set up the main ticker
	FMainLoopTiming MainLoop(IdealTickRate, EMainLoopOptions::CoreTickerOnly);

	// In the unattended mode we don't send any PII.
	FCrashReportCoreUnattended CrashReportClient(ErrorReport);
	ErrorReport.SetUserComment(NSLOCTEXT("CrashReportClient", "UnattendedMode", "Sent in the unattended mode"));

	FMessageBoxThread MessageBox;
	FRunnableThread* MessageBoxThread = nullptr;

	if (bImplicitSend)
	{
		MessageBoxThread = FRunnableThread::Create(&MessageBox, TEXT("CrashReporter_MessageBox"));
	}

	// loop until the app is ready to quit
	while (!(IsEngineExitRequested() || CrashReportClient.IsUploadComplete()))
	{
		MainLoop.Tick();
	}

	if (bImplicitSend && MessageBoxThread)
	{
		MessageBoxThread->WaitForCompletion();
	}

	// Continue running in case of ensures, otherwise close
	return FPrimaryCrashProperties::Get()->bIsEnsure ? SuccessContinue : SuccessClosed;
}

FPlatformErrorReport CollectErrorReport(FRecoveryService* RecoveryService, uint32 Pid, const FSharedCrashContext& SharedCrashContext, void* WritePipe)
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
				StackFrames,
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

#if CRASH_REPORT_WITH_RECOVERY
	if (RecoveryService && 
		DirectoryExists && 
		SharedCrashContext.UserSettings.bSendUsageData && 
		SharedCrashContext.CrashType != ECrashContextType::Ensure)
	{
		RecoveryService->CollectFiles(ReportDirectoryAbsolutePath);
	}
#endif

	const TCHAR* CrachContextBuffer = *CrashContext.GetBuffer();
	FPrimaryCrashProperties::Set(new FCrashContext(ReportDirectoryAbsolutePath / TEXT("CrashContext.runtime-xml"), CrachContextBuffer));

	FPlatformErrorReport ErrorReport(ReportDirectoryAbsolutePath);

	// Link the crash to the Editor summary event to help diagnose the abnormal termination quickly.
	FDiagnosticLogger::Get().LogEvent(*FPrimaryCrashProperties::Get()->CrashGUID);

#if CRASH_REPORT_UNATTENDED_ONLY
	return ErrorReport;
#else

	FString ConfigFilename;
	if (ErrorReport.FindFirstReportFileWithExtension(ConfigFilename, FGenericCrashContext::CrashConfigExtension))
	{
		FConfigFile CrashConfigFile;
		CrashConfigFile.Read(ReportDirectoryAbsolutePath / ConfigFilename);
		FCrashReportCoreConfig::Get().SetProjectConfigOverrides(CrashConfigFile);
	}

	return ErrorReport;
#endif
}

SubmitCrashReportResult SendErrorReport(FPlatformErrorReport& ErrorReport, 
	TOptional<bool> bNoDialogOpt = TOptional<bool>(), 
	TOptional<bool> bImplicitSendOpt = TOptional<bool>())
{
	if (!IsEngineExitRequested() && ErrorReport.HasFilesToUpload() && FPrimaryCrashProperties::Get() != nullptr)
	{
		const bool bImplicitSend = bImplicitSendOpt.Get(false);
		const bool bUnattended =
#if CRASH_REPORT_UNATTENDED_ONLY
			true;
#else
			bNoDialogOpt.Get(FApp::IsUnattended()) || bImplicitSend;
#endif // CRASH_REPORT_UNATTENDED_ONLY

		ErrorReport.SetCrashReportClientVersion(FCrashReportCoreConfig::Get().GetVersion());

		if (bUnattended)
		{
			return RunUnattended(ErrorReport, bImplicitSend);
		}
#if !CRASH_REPORT_UNATTENDED_ONLY
		else
		{
			const SubmitCrashReportResult Result = RunWithUI(ErrorReport);
			if (Result == Failed)
			{
				// UI failed to initialize, probably due to driver crash. Send in unattended mode if allowed.
				bool bCanSendWhenUIFailedToInitialize = true;
				GConfig->GetBool(TEXT("CrashReportClient"), TEXT("CanSendWhenUIFailedToInitialize"), bCanSendWhenUIFailedToInitialize, GEngineIni);
				if (bCanSendWhenUIFailedToInitialize && !FCrashReportCoreConfig::Get().IsAllowedToCloseWithoutSending())
				{
					return RunUnattended(ErrorReport, bImplicitSend);
				}
			}
			return Result;
		}
#endif // !CRASH_REPORT_UNATTENDED_ONLY

	}
	return Failed;
}

bool IsCrashReportAvailable(uint32 WatchedProcess, FSharedCrashContext& CrashContext, void* ReadPipe)
{
	TArray<uint8> Buffer;

	// Is data available on the pipe.
	if (FPlatformProcess::ReadPipeToArray(ReadPipe, Buffer))
	{
		FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Read"));

		// This is to ensure the FSharedCrashContext compiled in the monitored process and this process has the same size.
		int32 TotalRead = Buffer.Num();

		// Utility function to copy bytes from a source to a destination buffer.
		auto CopyFn = [](const TArray<uint8>& SrcData, uint8* DstIt, uint8* DstEndIt)
		{
			int32 CopyCount = FMath::Min(SrcData.Num(), static_cast<int32>(DstEndIt - DstIt)); // Limit the number of byte to copy to avoid writing passed the end of the destination.
			FPlatformMemory::Memcpy(DstIt, SrcData.GetData(), CopyCount);
			return DstIt + CopyCount; // Returns the updated position.
		};

		// Iterators to defines the boundaries of the destination buffer in memory.
		uint8* SharedCtxIt = reinterpret_cast<uint8*>(&CrashContext);
		uint8* SharedCtxEndIt = SharedCtxIt + sizeof(FSharedCrashContext);

		// Copy the data already read and update the destination iterator.
		SharedCtxIt = CopyFn(Buffer, SharedCtxIt, SharedCtxEndIt);

		// Try to consume all the expected data within a defined period of time.
		FDateTime WaitEndTime = FDateTime::UtcNow() + FTimespan(0, 0, 2);
		while (SharedCtxIt != SharedCtxEndIt && FDateTime::UtcNow() <= WaitEndTime)
		{
			if (FPlatformProcess::ReadPipeToArray(ReadPipe, Buffer)) // This is false if no data is available, but the writer may be still be writing.
			{
				TotalRead += Buffer.Num();
				SharedCtxIt = CopyFn(Buffer, SharedCtxIt, SharedCtxEndIt); // Copy the data read.
			}
			else
			{
				FPlatformProcess::Sleep(0.1); // Give the writer some time.
			}
		}

		if (TotalRead < sizeof(FSharedCrashContext))
		{
			FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/NotEnoughData"));
		}
		else if (TotalRead > sizeof(FSharedCrashContext))
		{
			FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/TooMuchData"));
		}
		else
		{
			// Record the history of events sent by the Editor to help diagnose abnormal terminations.
			switch (CrashContext.CrashType)
			{
				case ECrashContextType::Assert:           FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Assert"));          break;
				case ECrashContextType::Ensure:           FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Ensure"));          break;
				case ECrashContextType::Crash:            FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Crash"));           break;
				case ECrashContextType::GPUCrash:         FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/GPUCrash"));        break;
				case ECrashContextType::Hang:             FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Hang"));            break;
				case ECrashContextType::OutOfMemory:      FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/OOM"));             break;
				case ECrashContextType::AbnormalShutdown: FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/AbnormalShutdown"));break;
				default:                                  FDiagnosticLogger::Get().LogEvent(TEXT("Pipe/Unknown"));         break;
			}
		}

		return SharedCtxIt == SharedCtxEndIt;
	}

	return false;
}

static void DeleteTempCrashContextFile(uint64 ProcessID)
{
	const FString SessionContextFile = FGenericCrashContext::GetTempSessionContextFilePath(ProcessID);
	FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*SessionContextFile);
}

#if CRASH_REPORT_WITH_MTBF

template <typename Type>
bool FindAndParseValue(const TMap<FString, FString>& Map, const FString& Key, Type& OutValue)
{
	const FString* ValueString = Map.Find(Key);
	if (ValueString != nullptr)
	{
		TTypeFromString<Type>::FromString(OutValue, **ValueString);
		return true;
	}

	return false;
}

template <size_t Size>
bool FindAndCopyValue(const TMap<FString, FString>& Map, const FString& Key, TCHAR (&OutValue)[Size])
{
	const FString* ValueString = Map.Find(Key);
	if (ValueString != nullptr)
	{
		FCString::Strncpy(OutValue, **ValueString, Size);
		return true;
	}

	return false;
}

static bool LoadTempCrashContextFromFile(FSharedCrashContext& CrashContext, uint64 ProcessID)
{
	const FString TempContextFilePath = FGenericCrashContext::GetTempSessionContextFilePath(ProcessID);

	FXmlFile File;
	if (!File.LoadFile(TempContextFilePath))
	{
		return false;
	}

	TMap<FString, FString> ContextProperties;
	for (FXmlNode* Node : File.GetRootNode()->GetChildrenNodes())
	{
		ContextProperties.Add(Node->GetTag(), Node->GetContent());
	}

	FSessionContext& SessionContext = CrashContext.SessionContext;

	FindAndParseValue(ContextProperties, TEXT("SecondsSinceStart"), SessionContext.SecondsSinceStart);
	FindAndParseValue(ContextProperties, TEXT("IsInternalBuild"), SessionContext.bIsInternalBuild);
	FindAndParseValue(ContextProperties, TEXT("IsPerforceBuild"), SessionContext.bIsPerforceBuild);
	FindAndParseValue(ContextProperties, TEXT("IsSourceDistribution"), SessionContext.bIsSourceDistribution);
	FindAndCopyValue(ContextProperties, TEXT("GameName"), SessionContext.GameName);
	FindAndCopyValue(ContextProperties, TEXT("ExecutableName"), SessionContext.ExecutableName);
	FindAndCopyValue(ContextProperties, TEXT("GameSessionID"), SessionContext.GameSessionID);
	FindAndCopyValue(ContextProperties, TEXT("EngineMode"), SessionContext.EngineMode);
	FindAndCopyValue(ContextProperties, TEXT("EngineModeEx"), SessionContext.EngineModeEx);
	FindAndCopyValue(ContextProperties, TEXT("DeploymentName"), SessionContext.DeploymentName);
	FindAndCopyValue(ContextProperties, TEXT("CommandLine"), SessionContext.CommandLine);
	FindAndParseValue(ContextProperties, TEXT("LanguageLCID"), SessionContext.LanguageLCID);
	FindAndCopyValue(ContextProperties, TEXT("AppDefaultLocale"), SessionContext.DefaultLocale);
	FindAndParseValue(ContextProperties, TEXT("IsUE4Release"), SessionContext.bIsUE4Release);
	FindAndCopyValue(ContextProperties, TEXT("UserName"), SessionContext.UserName);
	FindAndCopyValue(ContextProperties, TEXT("BaseDir"), SessionContext.BaseDir);
	FindAndCopyValue(ContextProperties, TEXT("RootDir"), SessionContext.RootDir);
	FindAndCopyValue(ContextProperties, TEXT("LoginId"), SessionContext.LoginIdStr);
	FindAndCopyValue(ContextProperties, TEXT("EpicAccountId"), SessionContext.EpicAccountId);
	FindAndCopyValue(ContextProperties, TEXT("UserActivityHint"), SessionContext.UserActivityHint);
	FindAndParseValue(ContextProperties, TEXT("CrashDumpMode"), SessionContext.CrashDumpMode);
	FindAndCopyValue(ContextProperties, TEXT("GameStateName"), SessionContext.GameStateName);
	FindAndParseValue(ContextProperties, TEXT("Misc.NumberOfCores"), SessionContext.NumberOfCores);
	FindAndParseValue(ContextProperties, TEXT("Misc.NumberOfCoresIncludingHyperthreads"), SessionContext.NumberOfCoresIncludingHyperthreads);
	FindAndCopyValue(ContextProperties, TEXT("Misc.CPUVendor"), SessionContext.CPUVendor);
	FindAndCopyValue(ContextProperties, TEXT("Misc.CPUBrand"), SessionContext.CPUBrand);
	FindAndCopyValue(ContextProperties, TEXT("Misc.PrimaryGPUBrand"), SessionContext.PrimaryGPUBrand);
	FindAndCopyValue(ContextProperties, TEXT("Misc.OSVersionMajor"), SessionContext.OsVersion);
	FindAndCopyValue(ContextProperties, TEXT("Misc.OSVersionMinor"), SessionContext.OsSubVersion);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.AvailablePhysical"), SessionContext.MemoryStats.AvailablePhysical);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.AvailableVirtual"), SessionContext.MemoryStats.AvailableVirtual);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.UsedPhysical"), SessionContext.MemoryStats.UsedPhysical);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.PeakUsedPhysical"), SessionContext.MemoryStats.PeakUsedPhysical);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.UsedVirtual"), SessionContext.MemoryStats.UsedVirtual);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.PeakUsedVirtual"), SessionContext.MemoryStats.PeakUsedVirtual);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.bIsOOM"), SessionContext.bIsOOM);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.OOMAllocationSize"), SessionContext.OOMAllocationSize);
	FindAndParseValue(ContextProperties, TEXT("MemoryStats.OOMAllocationAlignment"), SessionContext.OOMAllocationAlignment);

	// user settings
	FUserSettingsContext& UserSettings = CrashContext.UserSettings;

	FindAndParseValue(ContextProperties, TEXT("NoDialog"), UserSettings.bNoDialog);
	FindAndParseValue(ContextProperties, TEXT("SendUnattendedBugReports"), UserSettings.bSendUnattendedBugReports);
	FindAndParseValue(ContextProperties, TEXT("SendUsageData"), UserSettings.bSendUsageData);
	FindAndCopyValue(ContextProperties, TEXT("LogFilePath"), UserSettings.LogFilePath);

	return true;
}

static void HandleAbnormalShutdown(FSharedCrashContext& CrashContext, uint64 ProcessID, void* WritePipe, const TSharedPtr<FRecoveryService>& RecoveryService)
{
	CrashContext.CrashType = ECrashContextType::AbnormalShutdown;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// create a temporary crash directory
	const FString TempCrashDirectory = FPlatformProcess::UserTempDir() / FString::Printf(TEXT("UECrashContext-%d"), ProcessID);
	FCString::Strcpy(CrashContext.CrashFilesDirectory, *TempCrashDirectory);

	if (PlatformFile.CreateDirectory(CrashContext.CrashFilesDirectory))
	{
		// copy the log file to the temporary directory
		const FString LogDestination = TempCrashDirectory / FPaths::GetCleanFilename(CrashContext.UserSettings.LogFilePath);
		PlatformFile.CopyFile(*LogDestination, CrashContext.UserSettings.LogFilePath);

		FPlatformErrorReport ErrorReport = CollectErrorReport(RecoveryService.Get(), ProcessID, CrashContext, WritePipe);
		SubmitCrashReportResult Result = SendErrorReport(ErrorReport, /*bNoDialog*/ true);

		// delete the temporary directory
		PlatformFile.DeleteDirectoryRecursively(*TempCrashDirectory);

		if (CrashContext.UserSettings.bSendUsageData)
		{
			// If analytics is enabled make sure they are submitted now.
			FCrashReportAnalytics::GetProvider().BlockUntilFlushed(5.0f);
		}
	}
}

static bool WasAbnormalShutdown(const FEditorAnalyticsSession& AnalyticSession)
{
	// Check if this was an abnormal shutdown (aka. none of the known shutdown types, and not debugged)
	return AnalyticSession.bCrashed == false &&
		AnalyticSession.bGPUCrashed == false &&
		AnalyticSession.bWasShutdown == false &&
		AnalyticSession.bIsTerminating == false &&
		AnalyticSession.bWasEverDebugger == false;
}

#endif

void RunCrashReportClient(const TCHAR* CommandLine)
{
	FDiagnosticLogger::Get().LogEvent(TEXT("CRC/Init"));

	// Override the stack size for the thread pool.
	FQueuedThreadPool::OverrideStackSize = 256 * 1024;

	// Increase the HttpSendTimeout to 5 minutes
	GConfig->SetFloat(TEXT("HTTP"), TEXT("HttpSendTimeout"), 5 * 60.0f, GEngineIni);

	// Initialize the engine. -Messaging enables MessageBus transports required by Concert (Recovery Service).
	FString FinalCommandLine(CommandLine);
#if CRASH_REPORT_WITH_RECOVERY
	FinalCommandLine += TEXT(" -Messaging -EnablePlugins=\"UdpMessaging,ConcertSyncServer\"");
#endif
	GEngineLoop.PreInit(*FinalCommandLine);
	check(GConfig && GConfig->IsReadyForUse());

	GLog->AddOutputDevice(&FDiagnosticLogger::Get());
	FDiagnosticLogger::Get().LogEvent(TEXT("CRC/Load"));

	// Make sure all UObject classes are registered and default properties have been initialized
	ProcessNewlyLoadedUObjects();

	// Tell the module manager is may now process newly-loaded UObjects when new C++ modules are loaded
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	// Load internal Concert plugins in the pre-default phase
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::PreDefault);

	// Load Concert Sync plugins in default phase
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::Default);

	FDiagnosticLogger::Get().LogEvent(TEXT("CRC/Config"));

	// Initialize config.
	FCrashReportCoreConfig::Get();

	// Find the report to upload in the command line arguments
	ParseCommandLine(CommandLine);
	FPlatformErrorReport::Init();

	if (MonitorPid == 0) // Does not monitor any process.
	{
		FDiagnosticLogger::Get().LogEvent(TEXT("NoMonitor/Start"));

		if (AnalyticsEnabledFromCmd)
		{
			FCrashReportAnalytics::Initialize();
		}

		// Load error report generated by the process from disk
		FPlatformErrorReport ErrorReport = LoadErrorReport();
		const SubmitCrashReportResult Result = SendErrorReport(ErrorReport, FApp::IsUnattended(), bImplicitSendFromCmd);
		// We are not interested in the result of this

		if (AnalyticsEnabledFromCmd)
		{
			FCrashReportAnalytics::Shutdown();
		}
	}
	else // Launched in 'service mode - watches/serves a process'
	{
		FDiagnosticLogger::Get().LogEvent(FString::Printf(TEXT("Monitor/Start:%d"), FPlatformProcess::GetCurrentProcessId()));

		const int32 IdealFramerate = 30;
		double LastTime = FPlatformTime::Seconds();
		const float IdealFrameTime = 1.0f / IdealFramerate;

		TSharedPtr<FRecoveryService> RecoveryServicePtr; // Note: Shared rather than Unique due to FRecoveryService only being a forward declaration in some builds

#if CRASH_REPORT_WITH_RECOVERY
		// Starts the disaster recovery service. This records transactions and allows users to recover from previous crashes.
		RecoveryServicePtr = MakeShared<FRecoveryService>(MonitorPid);
		FDiagnosticLogger::Get().LogEvent(TEXT("Recovery/Started"));
#endif

		// Try to open the process. The analytics shows that CRC sometime exits before the Editor. This can happen if OpenProcess() fails, maybe because
		// this process was spawned in the Editor Pre-init phase and the OS did not make it available yet. In case of failure, give it a few trials.
		FProcHandle MonitoredProcess;
		for (int32 Trial = 0; Trial < 5; ++Trial)
		{
#if PLATFORM_WINDOWS
			// We do not need to open a full access 
			MonitoredProcess = FProcHandle(::OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, 0, MonitorPid));
#else
			MonitoredProcess = FPlatformProcess::OpenProcess(MonitorPid);
#endif
			if (MonitoredProcess.IsValid())
			{
				break;
			}

			FPlatformProcess::Sleep(1); // Wait a seconds before retrying. Give time to the Editor to initialize.
		}

		FDiagnosticLogger::Get().LogEvent(MonitoredProcess.IsValid() ? TEXT("OpenProcess/Done") : TEXT("OpenProcess/Failed"));

		auto GetProcessStatus = [](FProcHandle& ProcessHandle) -> TTuple<bool/*Running*/, TOptional<int32>/*ReturnCode*/>
		{
			bool bRunning = true;
			TOptional<int32> ProcessReturnCodeOpt; // Unknown by default.
			if (!ProcessHandle.IsValid())
			{
				bRunning = false;
			}
			else if (!FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				bRunning = false;
				int32 ProcessReturnCode = 0;
				if (FPlatformProcess::GetProcReturnCode(ProcessHandle, &ProcessReturnCode)) // Is the return code available? (Not supported on all platforms)
				{
					FDiagnosticLogger::Get().LogEvent(FString::Printf(TEXT("Editor/ExitCode:%d"), ProcessReturnCode));
					ProcessReturnCodeOpt.Emplace(ProcessReturnCode);
				}
				else
				{
					FDiagnosticLogger::Get().LogEvent(TEXT("Editor/ExitCode:N/A"));
				}
			}

			return MakeTuple(bRunning, ProcessReturnCodeOpt);
		};

		// This GetProcessStatus() call is expensive, perform it at low frequency.
		TTuple<bool/*bRunning*/, TOptional<int32>/*ExitCode*/> ProcessStatus = GetProcessStatus(MonitoredProcess);
		while (ProcessStatus.Get<0>() && !IsEngineExitRequested())
		{
			const double CurrentTime = FPlatformTime::Seconds();

			// If 'out-of-process' crash reporting was enabled.
			if (MonitorWritePipe && MonitorReadPipe)
			{
				// Check if the monitored process signaled a crash or an ensure.
				FSharedCrashContext CrashContext;
				if (IsCrashReportAvailable(MonitorPid, CrashContext, MonitorReadPipe))
				{
					FDiagnosticLogger::Get().LogEvent(TEXT("Report/Start"));

					const bool bReportCrashAnalyticInfo = CrashContext.UserSettings.bSendUsageData;
					if (bReportCrashAnalyticInfo)
					{
						FCrashReportAnalytics::Initialize();
					}

					FDiagnosticLogger::Get().LogEvent(TEXT("Report/Collect"));

					// Build error report in memory.
					FPlatformErrorReport ErrorReport = CollectErrorReport(RecoveryServicePtr.Get(), MonitorPid, CrashContext, MonitorWritePipe);

#if CRASH_REPORT_WITH_RECOVERY
					if (RecoveryServicePtr && !FPrimaryCrashProperties::Get()->bIsEnsure)
					{
						// Shutdown the recovery service. This will releases the recovery database file lock (not sharable) and let a new instance take it and offer the user to recover.
						FDiagnosticLogger::Get().LogEvent(TEXT("Recovery/Shutdown"));
						RecoveryServicePtr.Reset();
					}
#endif
					FDiagnosticLogger::Get().LogEvent(TEXT("Report/Send"));

					const bool bNoDialog = (CrashContext.UserSettings.bNoDialog || CrashContext.UserSettings.bImplicitSend) && CrashContext.UserSettings.bSendUnattendedBugReports;
					const SubmitCrashReportResult Result = SendErrorReport(ErrorReport, bNoDialog, CrashContext.UserSettings.bImplicitSend);

					FDiagnosticLogger::Get().LogEvent(TEXT("Report/Analytic"));
					if (bReportCrashAnalyticInfo)
					{
						if (FCrashReportAnalytics::IsAvailable())
						{
							// If analytics is enabled make sure they are submitted now.
							FCrashReportAnalytics::GetProvider().BlockUntilFlushed(5.0f);
						}
						FCrashReportAnalytics::Shutdown();
					}

					FDiagnosticLogger::Get().LogEvent(TEXT("Report/Done"));
				}
			}

			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

			// Pump & Tick objects
			const double DeltaTime = CurrentTime - LastTime;
			FTicker::GetCoreTicker().Tick(DeltaTime);

			GFrameCounter++;
			FStats::AdvanceFrame(false);
			GLog->FlushThreadedLogs();

			// Run garbage collection for the UObjects for the rest of the frame or at least to 2 ms
			IncrementalPurgeGarbage(true, FMath::Max<float>(0.002f, IdealFrameTime - (FPlatformTime::Seconds() - LastTime)));

			// Throttle main thread main fps by sleeping if we still have time
			FPlatformProcess::Sleep(FMath::Max<float>(0.0f, IdealFrameTime - (FPlatformTime::Seconds() - LastTime)));

			// Check if the application is alive about every second. (This is an expensive call)
			if (GFrameCounter % IdealFramerate == 0)
			{
				ProcessStatus = GetProcessStatus(MonitoredProcess);
			}

			LastTime = CurrentTime;
		}

#if CRASH_REPORT_WITH_MTBF // Expected to be 1 when compiling CrashReportClientEditor.
		{
			FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/Start"));

			// The loop above can exit before the Editor (monitored process) exits (because of IsEngineExitRequested()) if the user clicks 'Close Without Sending' very quickly, but for MTBF,
			// it is desirable to have the Editor process return code. Give some extra time to the Editor to exit. If it doesn't exit within x seconds the next CRC instance will sent the
			// current analytic report delayed, not ideal, but supported.
			FDateTime WaitEndTime = FDateTime::UtcNow() + FTimespan::FromMinutes(3);
			while (ProcessStatus.Get<0>() && FDateTime::UtcNow() <= WaitEndTime)
			{
				FPlatformProcess::Sleep(0.1f); // In seconds
				ProcessStatus = GetProcessStatus(MonitoredProcess);
			}

			// Check the status of the Editor process after waiting n seconds.
			TOptional<int32> MonitoredProcessExitCode = ProcessStatus.Get<1>();
			bool bMonitoredProcessExited = !ProcessStatus.Get<0>();
			bool bMonitoredSessionLoaded = false;

			FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/LoadSession"));

			// Try to persist an exit code in session summary (even if the Editor is still running)
			FEditorAnalyticsSession MonitoredSession;
			FTimespan Timeout = FTimespan::FromMinutes(2);
			if (FEditorAnalyticsSession::Lock(Timeout))
			{
				if (FEditorAnalyticsSession::FindSession(MonitorPid, MonitoredSession))
				{
					bMonitoredSessionLoaded = true; // Were able to acquire lock and load the session.

					if (MonitoredProcessExitCode.IsSet())
					{
						// Persist the real Editor exit code.
						MonitoredSession.SaveExitCode(MonitoredProcessExitCode.GetValue());
					}
					else if (!bMonitoredProcessExited)
					{
						// Persist a custom exit code - Editor is still running, but CRC was requested to exit. (ex. The user clicked the 'Close Without Sending' but the Editor is not exited yet)
						MonitoredSession.SaveExitCode(ECrashExitCodes::MonitoredApplicationStillRunning);
					}
					else
					{
						// Persist the custom exit code - Editor is not running anymore, but CRC could not read it.
						MonitoredSession.SaveExitCode(ECrashExitCodes::MonitoredApplicationExitCodeNotAvailable);
					}
				}
				else
				{
					FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/NoSessionFound"));
				}
				FEditorAnalyticsSession::Unlock();
			}
			else
			{
				FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/LockSessionFail"));
			}

			if (bMonitoredProcessExited)
			{
				// Load the temporary crash context file.
				FSharedCrashContext TempCrashContext;
				FMemory::Memzero(TempCrashContext);
				if (LoadTempCrashContextFromFile(TempCrashContext, MonitorPid) && TempCrashContext.UserSettings.bSendUsageData)
				{
					FCrashReportAnalytics::Initialize();
					if (FCrashReportAnalytics::IsAvailable())
					{
						FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/Done"));
						// Send this session summary event (and the orphan ones if any).
						FEditorSessionSummarySender EditorSessionSummarySender(FCrashReportAnalytics::GetProvider(), TEXT("CrashReportClient"), MonitorPid);
						EditorSessionSummarySender.SetMonitorDiagnosticLogs(FDiagnosticLogger::LoadAllLogs());
						EditorSessionSummarySender.Shutdown();
						FDiagnosticLogger::ClearAllLogs(); // Logs (if any) were attached and sent if required and are not longer required.

						// If the Editor thinks the session ended up abnormally, generate a crash report (to get the Editor logs and figure out why this happened).
						if (bMonitoredSessionLoaded && TempCrashContext.UserSettings.bSendUnattendedBugReports)
						{
							// The exit code used by Windows when the Editor process is killed.
							constexpr int32 ProcessKilledExitCode = 1;

							// Check what the Editor knows about the exit. Was the proper handlers called and the flag(s) set in the summary event?
							if (WasAbnormalShutdown(MonitoredSession) && (!MonitoredProcessExitCode.IsSet() || MonitoredProcessExitCode.GetValue() != ProcessKilledExitCode))
							{
								// Send a spoofed crash report in the case that we detect an abnormal shutdown has occurred
								HandleAbnormalShutdown(TempCrashContext, MonitorPid, MonitorWritePipe, RecoveryServicePtr);
							}
						}
					}
					FCrashReportAnalytics::Shutdown();
				}
				else
				{
					FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/NoTempCrash"));
				}
			}
			else
			{
				FDiagnosticLogger::Get().LogEvent(TEXT("MTBF/StillRunning"));
			}
		}
#endif
		// Clean up the context file
		DeleteTempCrashContextFile(MonitorPid);

		FPlatformProcess::CloseProc(MonitoredProcess);
	}

	GLog->RemoveOutputDevice(&FDiagnosticLogger::Get());

	FPrimaryCrashProperties::Shutdown();
	FPlatformErrorReport::ShutDown();

	RequestEngineExit(TEXT("CrashReportClientApp RequestExit"));

	// Allow the game thread to finish processing any latent tasks.
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

	FEngineLoop::AppPreExit();
	FModuleManager::Get().UnloadModulesAtShutdown();
	FTaskGraphInterface::Shutdown();

	FEngineLoop::AppExit();
}
