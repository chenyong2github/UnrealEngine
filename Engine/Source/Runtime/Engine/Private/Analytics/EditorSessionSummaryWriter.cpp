// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorSessionSummaryWriter.h"

#if WITH_EDITOR

#include "EngineAnalytics.h"
#include "EngineGlobals.h"
#include "GeneralProjectSettings.h"
#include "RHI.h"
#include "UserActivityTracking.h"

#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/ExceptionHandling.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "IAnalyticsProviderET.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

#include "EditorAnalyticsSession.h"
#include "IVREditorModule.h"
#include "Kismet2/DebuggerCommands.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "SessionSummary"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSessionSummary, Verbose, All);

namespace EditorSessionWriterDefs
{
	// Number of seconds to wait before checking again if the debugger is connected.
	static const float DebuggerCheckPeriodSeconds = 1;

	// The upper CPU usage % considered as Idle. If the CPU usage goes above this threshold, the Editor is considered 'active'.
	constexpr float IdleCpuUsagePercent = 20;

	// The number of seconds required between Editor activities to consider the Editor as 'inactive' (user input, cpu burst).
	constexpr double EditorInactivitySecondsForIdleState = 5 * 60.0; // To be comparable to the 5-min user inactivity.

	// Returns the default period at which the session is saved.
	constexpr double GetDefaultSavePeriodSecs()
	{
	#if PLATFORM_WINDOWS
		return 30; // Saving to a couple of values to the registry takes about 5ms, so we can save more or less frequently.
	#else
		return 60; // On other platforms, were we must load/parse/update/save a .ini, this is rather slow, so throttle it more.
	#endif
	}
}

FEditorSessionSummaryWriter::FEditorSessionSummaryWriter(uint32 InProcessMonitorProcessId)
	: NextDebuggerCheckSecs(FPlatformTime::Seconds())
	, LastUserActivityTimeSecs(FPlatformTime::Seconds())
	, AccountedUserIdleSecs(0)
	, LastEditorActivityTimeSecs(FPlatformTime::Seconds())
	, SessionStartTimeUtc(FDateTime::UtcNow()) // Reliable only if system date/time doesn't change (like daylight savings or user altering it)
	, SessionStartTimeSecs(FPlatformTime::Seconds()) // Don't rely on system date/time. May suffer from lack of precision over long period of time (few seconds over a day).
	, OutOfProcessMonitorProcessId(InProcessMonitorProcessId)
{
}

FEditorSessionSummaryWriter::~FEditorSessionSummaryWriter()
{
	Shutdown(); // In case it wasn't already called.
}

void FEditorSessionSummaryWriter::Initialize()
{
	if (!FEngineAnalytics::IsAvailable() || CurrentSession != nullptr)
	{
		return;
	}

	if (FEditorAnalyticsSession::TryLock()) // System wide lock to write the session file/registry. Don't block if already taken, delay initialisation to the next Tick().
	{
		UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Initializing EditorSessionSummaryWriter for editor session tracking"));

		// Create a session Session for this session
		CurrentSession = CreateCurrentSession(SessionStartTimeUtc, OutOfProcessMonitorProcessId);

		// Update the idle/inactivity timers. The session start time is taken when the FEditorSessionSummaryWriter is created, but it is possible to have a very long gap of time until the
		// session itself is created if the session lock is contented. In such case, the session is created at the next Tick() and it may come much later if the computer hibernated in-between.
		double CurrTimeSecs = FPlatformTime::Seconds();
		UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
		UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/false);
		UpdateSessionDuration(CurrTimeSecs);

		CurrentSession->Save();
		LastSaveTimeSecs = CurrTimeSecs;

		UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummaryWriter initialized"));

		// Update the session list.
		TArray<FString> StoredSessions;
		FEditorAnalyticsSession::GetStoredSessionIDs(StoredSessions);
		StoredSessions.Add(CurrentSession->SessionId);
		FEditorAnalyticsSession::SaveStoredSessionIDs(StoredSessions);

		FEditorAnalyticsSession::Unlock();

		// Attached debugger was checked during session creation, schedule the next one in n seconds.
		NextDebuggerCheckSecs = CurrTimeSecs + EditorSessionWriterDefs::DebuggerCheckPeriodSeconds;
	}

	if (CurrentSession)
	{
		// Register for crash and app state callbacks
		FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEditorSessionSummaryWriter::OnCrashing); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEditorSessionSummaryWriter::OnTerminate); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnVanillaStateChanged);
		FCoreDelegates::OnUserLoginChangedEvent.AddRaw(this, &FEditorSessionSummaryWriter::OnUserLoginChanged);
		FEditorDelegates::PreBeginPIE.AddRaw(this, &FEditorSessionSummaryWriter::OnEnterPIE);
		FEditorDelegates::EndPIE.AddRaw(this, &FEditorSessionSummaryWriter::OnExitPIE);
		FUserActivityTracking::OnActivityChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnUserActivity);
		FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEditorSessionSummaryWriter::Tick);
		FSlateApplication::Get().GetLastUserInteractionTimeUpdateEvent().AddRaw(this, &FEditorSessionSummaryWriter::OnSlateUserInteraction);
	}
}

void FEditorSessionSummaryWriter::UpdateSessionDuration(double CurrTimeSecs)
{
	// NOTE: The code below is to handle a super edge case where a computer would go to sleep and suspend the application while 1 thread was about to update the duration
	//       while another was on the edge of starting updating it. On resume, a race condition between the threads exists and the duration observed by both thread will be
	//       very different. One would include the hibernate time, not the other. Must proceed carefully to ensure the greatest value is written.
	int32 NewSessionDuration = FMath::FloorToInt(static_cast<float>(CurrTimeSecs - SessionStartTimeSecs));
	do
	{
		int32 OldSessionDurationSecs = FPlatformAtomics::AtomicRead(&CurrentSession->SessionDuration);
		if (NewSessionDuration > OldSessionDurationSecs)
		{
			if (FPlatformAtomics::InterlockedCompareExchange(&CurrentSession->SessionDuration, NewSessionDuration, OldSessionDurationSecs) == OldSessionDurationSecs)
			{
				return; // Value was exchanged successfully.
			}
		}
		else
		{
			return; // Another thread updated with a greater duration.
		}
	} while (true);
}

void FEditorSessionSummaryWriter::UpdateSessionTimestamp(const FDateTime& InCurrTimeUtc)
{
	CurrentSession->Timestamp = InCurrTimeUtc;
}

// The editor idle time tries to account for the user inputs as well as CPU usage of the Editor. It is accumulated differently than the user idle
// times. User idle time is incremented after a grace period of N minutes. The Editor idle time is incremented every time a period of fixed idle
// time is completed.
bool FEditorSessionSummaryWriter::UpdateEditorIdleTime(double CurrTimeSecs, bool bReset)
{
	bool bSessionUpdated = false;

	double LastActivityExpectedSecs = LastEditorActivityTimeSecs.Load();
	double InactivitySeconds = CurrTimeSecs - LastActivityExpectedSecs;
	if (InactivitySeconds >= EditorSessionWriterDefs::EditorInactivitySecondsForIdleState) // Was idle long enough to account this span of time as Idle?
	{
		// Ensure only one thread increments the counter.
		if (LastEditorActivityTimeSecs.CompareExchange(LastActivityExpectedSecs, CurrTimeSecs))
		{
			// Add up this span of inactivity and reset the counter to start another span.
			FPlatformAtomics::InterlockedAdd(&CurrentSession->TotalEditorInactivitySeconds, FMath::FloorToInt(static_cast<float>(InactivitySeconds)));
			bSessionUpdated = true;
			bReset = true;
		}
	}

	if (bReset)
	{
		LastEditorActivityTimeSecs.Store(CurrTimeSecs);
	}

	return bSessionUpdated;
}

bool FEditorSessionSummaryWriter::UpdateUserIdleTime(double CurrTimeSecs, bool bReset)
{
	bool bSessionUpdated = false;

	// How much time elapsed since the last activity.
	double TotalIdleSecs = CurrTimeSecs - LastUserActivityTimeSecs.Load();
	if (TotalIdleSecs > 60.0) // Less than a minute is always considered normal interaction delay.
	{
		double LastAccountedIdleSecs = AccountedUserIdleSecs.Load();
		double UnaccountedIdleSecs   = TotalIdleSecs - LastAccountedIdleSecs;

		// If one or more minute is unaccounted
		if (UnaccountedIdleSecs >= 60.0)
		{
			double AccountedIdleMins = FMath::FloorToDouble(LastAccountedIdleSecs/60); // Minutes already accounted for.
			double ToAccountIdleMins = FMath::FloorToDouble(UnaccountedIdleSecs/60);   // New minutes to account for (entire minute only)

			// Delta = LatestAccounted - AlreadyAccounted
			double DeltaIdle1Min  = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 1)  - FMath::Max(0.0, AccountedIdleMins - 1);  // The first minute of this idle sequence is considered 'normal interaction delay' and in not accounted as idle.
			double DeltaIdle5Min  = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 5)  - FMath::Max(0.0, AccountedIdleMins - 5);  // The 5 first minutes of this idle sequence are considered 'normal interaction delay' and are not accounted for the 5-min timer.
			double DeltaIdle30Min = FMath::Max(0.0, AccountedIdleMins + ToAccountIdleMins - 30) - FMath::Max(0.0, AccountedIdleMins - 30); // The 30 first minutes of this idle sequence are considered 'normal interaction delay' and are not accounted for the 30-min timer.

			// Ensure only one thread adds the current delta time.
			if (AccountedUserIdleSecs.CompareExchange(LastAccountedIdleSecs, LastAccountedIdleSecs + ToAccountIdleMins * 60.0)) // Only add the 'accounted' minutes and keep fraction of minutes running.
			{
				FPlatformAtomics::InterlockedAdd(&CurrentSession->Idle1Min, FMath::RoundToInt(static_cast<float>(DeltaIdle1Min)));
				FPlatformAtomics::InterlockedAdd(&CurrentSession->Idle5Min, FMath::RoundToInt(static_cast<float>(DeltaIdle5Min)));
				FPlatformAtomics::InterlockedAdd(&CurrentSession->Idle30Min, FMath::RoundToInt(static_cast<float>(DeltaIdle30Min)));
				bSessionUpdated = true;
			}
		}
	}

	if (bReset)
	{
		AccountedUserIdleSecs.Store(0);
		LastUserActivityTimeSecs.Store(CurrTimeSecs);
	}

    // WARNING: The code is supposed to be concurrent safe, but does't block. Calling UpdateUserIdleTime() and reading the counter back may not read the latest value if another thread concurrently
	//          updated the values. In normal condition, this means +/- a minute on the reader. In case of computer was hibernating with this race condition pending, the error is bigger. Several hours
	//          of idle could be lost, but this is very unlikely (compute goes to hibernation while two threads are about to concurrently update idle time), losing this idle time is not statistically significant.

	return bSessionUpdated; // True if the idle timers were updated.
}

bool FEditorSessionSummaryWriter::UpdateOutOfProcessMonitorState(bool bQuickCheck)
{
	if (CurrentSession->MonitorProcessID == 0)
	{
		return false; // Nothing to update, monitor is not running in background (not supported/not in monitor mode/failed to launch)
	}
	else if (CurrentSession->MonitorExitCode.IsSet() && *CurrentSession->MonitorExitCode != ECrashExitCodes::OutOfProcessReporterExitedUnexpectedly)
	{
		return false; // Already have the real exit code set.
	}
	else if (TOptional<int32> ExitCode = FGenericCrashContext::GetOutOfProcessCrashReporterExitCode())
	{
		CurrentSession->MonitorExitCode = MoveTemp(ExitCode); // Just acquired the real exit code from the engine.
		return true;
	}
	else if (bQuickCheck)
	{
		return false; // All the code above is pretty fast and can run every tick. IsApplicationRunning() is very slow, so exit here.
	}
	else if (!CurrentSession->MonitorExitCode.IsSet() && !FPlatformProcess::IsApplicationRunning(CurrentSession->MonitorProcessID))
	{
		// Set a rather unique, but known exit code as place holder, hoping that next update, the engine will report the real one.
		CurrentSession->MonitorExitCode.Emplace(ECrashExitCodes::OutOfProcessReporterExitedUnexpectedly);
		return true;
	}
	// else -> either CrashReportClientEditor is still running or we already flagged it as dead.
	return false;
}

void FEditorSessionSummaryWriter::Tick(float DeltaTime)
{
	if (bShutdown)
	{
		return;
	}
	
	// Try late initialization (in case the global lock was already taken during init and the session couldn't be created or the user just toggled 'send data' on).
	if (CurrentSession == nullptr)
	{
		Initialize();
		return;
	}

	const double CurrentTimeSecs = FPlatformTime::Seconds();
	const FDateTime CurrentTimeUtc = FDateTime::UtcNow();

	// In the n first seconds, save more frequently because lot of bad things happen early and we'd like to have the data as accurate as possible.
	constexpr double EarlySessionAgeSecs = 30;
	constexpr double EarlySavePeriodSecs = 1;
	constexpr double DefaultSavePeriodSecs = EditorSessionWriterDefs::GetDefaultSavePeriodSecs();
	const double SessionAgeSecs = CurrentTimeSecs - SessionStartTimeSecs;
	bool bSaveSession = (CurrentTimeSecs - LastSaveTimeSecs >= (SessionAgeSecs <= EarlySessionAgeSecs ? EarlySavePeriodSecs : DefaultSavePeriodSecs));

	// Update all variables that are cheap to update.
	extern ENGINE_API float GAverageFPS;
	CurrentSession->AverageFPS = GAverageFPS;
	CurrentSession->LastTickTimestamp = CurrentTimeUtc;
	CurrentSession->SessionTickCount++;
	CurrentSession->EngineTickCount = GFrameCounter;

	// Detect if the Editor process CPU usage is high, this count as an activity and this resets Editor idle counter.
	if (FPlatformTime::GetCPUTime().CPUTimePct > EditorSessionWriterDefs::IdleCpuUsagePercent)
	{
		UpdateEditorIdleTime(CurrentTimeSecs, /*bReset*/true);
	}

	// Detect if CRC state changed since the last update.
	bSaveSession |= UpdateOutOfProcessMonitorState(/*bQuickCheck*/true);

	// Detect if the VR mode changed since the last update.
	bool bVREditorModeActive = IVREditorModule::Get().IsVREditorModeActive();
	if (bVREditorModeActive != CurrentSession->bIsInVRMode)
	{
		CurrentSession->bIsInVRMode = bVREditorModeActive;
		bSaveSession = true;
	}

	// Detect if the PIE state changed since the last update.
	bool bInPie = FPlayWorldCommandCallbacks::IsInPIE();
	if (bInPie != CurrentSession->bIsInPIE)
	{
		CurrentSession->bIsInPIE = bInPie;
		bSaveSession = true;
	}

	// Periodially check if the debugger is attached. The call might be slightly expensive on some platforms, so throttle it down.
	if (CurrentTimeSecs >= NextDebuggerCheckSecs)
	{
		// Ignoring the debugger changes how IsDebuggerPresent() behave and masks the usage of the debugger if true.
		if (GIgnoreDebugger)
		{
			bSaveSession |= CurrentSession->bIsDebuggerIgnored == false; // Only saved it when it goes from false to true.
			CurrentSession->bIsDebuggerIgnored = true;
		}

		// Check if the debugger is present.
		bool bIsDebuggerPresent = FPlatformMisc::IsDebuggerPresent();
		if (CurrentSession->bIsDebugger != bIsDebuggerPresent)
		{
			CurrentSession->bIsDebugger = bIsDebuggerPresent;
			if (bIsDebuggerPresent && !CurrentSession->bWasEverDebugger)
			{
				CurrentSession->bWasEverDebugger = true;
			}
			bSaveSession = true;
		}
		NextDebuggerCheckSecs = CurrentTimeSecs + EditorSessionWriterDefs::DebuggerCheckPeriodSeconds;
	}

	if (bSaveSession)
	{
		// TODO: Consider cloning the session and saving in another thread. It cost about 1 to 6ms on Windows to save in the main thread.
		TrySaveCurrentSession(CurrentTimeUtc, CurrentTimeSecs); // Saving also updates session duration/timestamp/userIdle/editorIdle
	}
}

void FEditorSessionSummaryWriter::LowDriveSpaceDetected()
{
	if (CurrentSession)
	{
		CurrentSession->bIsLowDriveSpace = true;
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
	}
}

void FEditorSessionSummaryWriter::Shutdown()
{
	// NOTE: Initialize(), Shutdown() and ~FEditorSessionSummaryWriter() are expected to be called from the game thread only.
	if (CurrentSession && !bShutdown)
	{
		// NOTE: Shutdown() may crash if a delegate is broadcasted from another thread at the same time (that's a bug in 4.24.x, 4.25.x) the delegate are modified.
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
		FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
		FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);
		FUserActivityTracking::OnActivityChanged.RemoveAll(this);
		FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
		FSlateApplication::Get().GetLastUserInteractionTimeUpdateEvent().RemoveAll(this);
		FCoreDelegates::OnUserLoginChangedEvent.RemoveAll(this);
		FCoreDelegates::OnHandleSystemError.RemoveAll(this);

		CurrentSession->bWasShutdown = true;
		double CurrTimeSecs = FPlatformTime::Seconds();
		FDateTime CurrTimeUtc = FDateTime::UtcNow();

		if (!TrySaveCurrentSession(CurrTimeUtc, CurrTimeSecs)) // If the save fails (because the lock was already taken)
		{
			UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateSessionDuration(CurrTimeSecs);
			CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Shutdown, CurrTimeUtc); // Use the lockless mechanism. It doesn't save everything, but it carries the critical information.
		}

		CurrentSession.Reset();
	}

	bShutdown = true;
}

TUniquePtr<FEditorAnalyticsSession> FEditorSessionSummaryWriter::CreateCurrentSession(const FDateTime& StartupTimeUtc, uint32 CrashReportClientProcessId)
{
	check(FEngineAnalytics::IsAvailable()); // The function assumes the caller checked it before calling.

	TUniquePtr<FEditorAnalyticsSession> Session = MakeUnique<FEditorAnalyticsSession>();
	IAnalyticsProviderET& AnalyticProvider = FEngineAnalytics::GetProvider();

	FGuid SessionId;
	if (FGuid::Parse(AnalyticProvider.GetSessionID(), SessionId))
	{
		// convert session GUID to one without braces or other chars that might not be suitable for storage
		Session->SessionId = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
	}
	else
	{
		Session->SessionId = AnalyticProvider.GetSessionID();
	}

	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();

	// Remember the AppId/AppVersion/UserId used during this session. They will be used if the summary is sent from another process/instance.
	Session->AppId = AnalyticProvider.GetAppID();
	Session->AppVersion = AnalyticProvider.GetAppVersion();
	Session->UserId = AnalyticProvider.GetUserID();

	FString ProjectName = FApp::GetProjectName();
	if (ProjectName.Len() && ProjectSettings.ProjectName.Len())
	{
		if (ProjectSettings.ProjectName != ProjectName)
		{
			ProjectName = ProjectName / ProjectSettings.ProjectName; // The project names don't match, report both.
		}
	}
	else if (ProjectName.IsEmpty())
	{
		ProjectName = ProjectSettings.ProjectName;
	}

	Session->PlatformProcessID = FPlatformProcess::GetCurrentProcessId();
	Session->MonitorProcessID = CrashReportClientProcessId;
	Session->ProjectName = ProjectName;
	Session->ProjectID = ProjectSettings.ProjectID.ToString(EGuidFormats::DigitsWithHyphens);
	Session->ProjectDescription = ProjectSettings.Description;
	Session->ProjectVersion = ProjectSettings.ProjectVersion;
	Session->EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Changelist);
	Session->StartupTimestamp = StartupTimeUtc;
	Session->LastTickTimestamp = FDateTime::UtcNow();
	Session->Timestamp = FDateTime::UtcNow();
	Session->bIsDebugger = FPlatformMisc::IsDebuggerPresent();
	Session->bWasEverDebugger = FPlatformMisc::IsDebuggerPresent();
	Session->CurrentUserActivity = GetUserActivityString();
	Session->bIsVanilla = GEngine && GEngine->IsVanillaProduct();
	Session->CommandLine = FCommandLine::GetForLogging();
	Session->EngineTickCount = GFrameCounter;

	// TODO: Add a function later on to PlatfomCrashContext to check existance of CRC. This is only used on windows at the moment to categorize cases where CRC fails to report the exit code (only supported on Windows).
#if PLATFORM_WINDOWS
	// If the monitor process (CRC) did not launch, check if the executable is present. (Few people seems to delete it or not build it)
	if (Session->MonitorProcessID == 0)
	{
		const FString EngineDir = FPlatformMisc::EngineDir();

		// Find the path to crash reporter binary. Avoid creating FStrings.
		FString CrcPathRel = EngineDir / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory() / TEXT("CrashReportClientEditor.exe");
		FString CrcPathDev = EngineDir / TEXT("Binaries") / FPlatformProcess::GetBinariesSubdirectory() / TEXT("CrashReportClientEditor-Win64-Development.exe");

		Session->bIsCrcExeMissing = !IFileManager::Get().FileExists(*CrcPathRel) && !IFileManager::Get().FileExists(*CrcPathDev);
	}
#endif

	FString OSMajor;
	FString OSMinor;
	FPlatformMisc::GetOSVersions(/*out*/ OSMajor, /*out*/ OSMinor);
	FPlatformMemoryStats Stats = FPlatformMemory::GetStats();

	Session->DesktopGPUAdapter = FPlatformMisc::GetPrimaryGPUBrand();
	Session->RenderingGPUAdapter = GRHIAdapterName;
	Session->GPUVendorID = GRHIVendorId;
	Session->GPUDeviceID = GRHIDeviceId;
	Session->GRHIDeviceRevision = GRHIDeviceRevision;
	Session->GRHIAdapterInternalDriverVersion = GRHIAdapterInternalDriverVersion;
	Session->GRHIAdapterUserDriverVersion = GRHIAdapterUserDriverVersion;
	Session->GRHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("");
	Session->TotalPhysicalRAM = Stats.TotalPhysical;
	Session->CPUPhysicalCores = FPlatformMisc::NumberOfCores();
	Session->CPULogicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	Session->CPUVendor = FPlatformMisc::GetCPUVendor();
	Session->CPUBrand = FPlatformMisc::GetCPUBrand();
	Session->OSMajor = OSMajor;
	Session->OSMinor = OSMinor;
	Session->OSVersion = FPlatformMisc::GetOSVersion();
	Session->bIs64BitOS = FPlatformMisc::Is64bitOperatingSystem();

	extern ENGINE_API float GAverageFPS;
	Session->AverageFPS = GAverageFPS;
	Session->bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
	Session->bIsInEnterprise = IProjectManager::Get().IsEnterpriseProject();
	Session->bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();

	TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : Plugins)
	{
		Session->Plugins.Add(Plugin->GetName());
	}

	Session->Plugins.Sort();

	return Session;
}

extern CORE_API bool GIsGPUCrashed;
void FEditorSessionSummaryWriter::OnCrashing()
{
	// NOTE: This function is called from the crashing thread or a crash processing thread and is concurrent with other functions such as Tick(), Initialize() or Shutdown() running on the game thread.
	if (CurrentSession != nullptr)
	{
		double CurrTimeSecs = FPlatformTime::Seconds();
		UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
		UpdateEditorIdleTime(CurrTimeSecs, /*bSaveSession*/false);
		UpdateSessionDuration(CurrTimeSecs);
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Crashed, FDateTime::UtcNow());

		if (GIsGPUCrashed)
		{
			CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::GpuCrashed, FDateTime::UtcNow());
			CurrentSession->bGPUCrashed = true; // Not atomic and not strictly required, the logged event will cover for it, but for debugging this is easier when looking in the registry directly.
		}

		// NOTE: Don't explicitely Shutdown(), it is expected to be called on game thread to prevent unregistering delegate from a random thread.
		// NOTE: Don't call TrySaveCurrentSession(), not all fields are atomic and saving could write a corrupted version if the field is written at the same time.
	}
}

void FEditorSessionSummaryWriter::OnTerminate()
{
	// NOTE: This function can be called from any thread (from the crashing thread too) and is likely concurrent with other functions such as Tick(), Initialize() or Shutdown() running on the game thread.
	if (CurrentSession != nullptr)
	{
		double CurrTimeSecs = FPlatformTime::Seconds();
		UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
		UpdateEditorIdleTime(CurrTimeSecs, /*bSaveSession*/false);
		UpdateSessionDuration(CurrTimeSecs);
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Terminated, FDateTime::UtcNow());

		// NOTE: Don't explicitely Shutdown(), it is expected to be called on game thread to prevent unregistering delegate from a random thread.
		// NOTE: Don't call TrySaveCurrentSession(), not all fields are atomic and saving could write a corrupted version if the field is written at the same time.
	}
}

void FEditorSessionSummaryWriter::OnVanillaStateChanged(bool bIsVanilla)
{
	if (CurrentSession != nullptr && CurrentSession->bIsVanilla != bIsVanilla)
	{
		CurrentSession->bIsVanilla = bIsVanilla;
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
	}
}

void FEditorSessionSummaryWriter::OnUserActivity(const FUserActivity& UserActivity)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->CurrentUserActivity = GetUserActivityString();
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
	}
}

void FEditorSessionSummaryWriter::OnUserLoginChanged(bool bLoggingIn, int32, int32)
{
	if (!bLoggingIn)
	{
		CurrentSession->bIsUserLoggingOut = true;

		double CurrTimeSecs = FPlatformTime::Seconds();
		FDateTime CurrTimeUtc = FDateTime::UtcNow();
		if (!TrySaveCurrentSession(CurrTimeUtc, CurrTimeSecs)) // If the save fails (because the lock was already taken)
		{
			UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateSessionDuration(CurrTimeSecs);
			CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::LogOut, FDateTime::UtcNow()); // Use the lockless mechanism. It doesn't save everything, but it carries the critical information.
		}
	}
}

FString FEditorSessionSummaryWriter::GetUserActivityString()
{
	const FUserActivity& UserActivity = FUserActivityTracking::GetUserActivity();

	if (UserActivity.ActionName.IsEmpty())
	{
		return TEXT("Unknown");
	}

	return UserActivity.ActionName;
}

void FEditorSessionSummaryWriter::OnSlateUserInteraction(double CurrSlateInteractionTime)
{
	CurrentSession->UserInteractionCount++;

	// User input 'reset' the idle timers.
	double CurrTimeSecs = FPlatformTime::Seconds();
	bool bSave = UpdateUserIdleTime(CurrTimeSecs, /*bReset*/true);
	bSave |= UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/true);
	if (bSave)
	{
		TrySaveCurrentSession(FDateTime::UtcNow(), CurrTimeSecs);
	}
}

void FEditorSessionSummaryWriter::OnEnterPIE(const bool /*bIsSimulating*/)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->bIsInPIE = true;
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
	}
}

void FEditorSessionSummaryWriter::OnExitPIE(const bool /*bIsSimulating*/)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->bIsInPIE = false;
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
	}
}

bool FEditorSessionSummaryWriter::TrySaveCurrentSession(const FDateTime& CurrTimeUtc, double CurrTimeSecs)
{
	if (FEditorAnalyticsSession::TryLock()) // Inter-process lock to grant this process exclusive access to the key-store file/registry.
	{
		if (SaveSessionLock.TryLock()) // Intra-process lock to grant the calling thread exclusive access to the key-store file/registry.
		{
			UpdateOutOfProcessMonitorState(/*bQuickCheck*/true);
			UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateSessionDuration(CurrTimeSecs);
			UpdateSessionTimestamp(CurrTimeUtc);
			CurrentSession->Save();
			LastSaveTimeSecs = CurrTimeSecs;
			SaveSessionLock.Unlock();
		}
		FEditorAnalyticsSession::Unlock();
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
