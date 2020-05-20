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
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IAnalyticsProviderET.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
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
	// Number of seconds to wait between each update of the mutable metrics.
	static const float HeartbeatPeriodSeconds = 60;

	// In the first minutes, update every seconds because lot of crashes occurs in the first minute.
	static const float EarlyHeartbeatPeriodSeconds = 1;

	// The upper CPU usage % considered as Idle. If the CPU usage goes above this threshold, the Editor is considered 'active'.
	constexpr float IdleCpuUsagePercent = 20;

	// The number of seconds required between Editor activities to consider the Editor as 'inactive' (user input, cpu burst).
	constexpr double EditorInactivitySecondsForIdleState = 5 * 60.0; // To be comparable to the 5-min user inactivity.
}

FEditorSessionSummaryWriter::FEditorSessionSummaryWriter(uint32 InProcessMonitorProcessId)
	: HeartbeatTimeElapsed(0.0f)
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

	UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Initializing EditorSessionSummaryWriter for editor session tracking"));

	if (FEditorAnalyticsSession::TryLock()) // System wide lock to write the session file/registry. Don't block if already taken, delay initialisation to the next Tick().
	{
		// Create a session Session for this session
		CurrentSession = CreateCurrentSession(SessionStartTimeUtc, OutOfProcessMonitorProcessId);
		CurrentSession->Save();

		UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummaryWriter initialized"));

		// update session list string
		TArray<FString> StoredSessions;

		FEditorAnalyticsSession::GetStoredSessionIDs(StoredSessions);
		StoredSessions.Add(CurrentSession->SessionId);

		FEditorAnalyticsSession::SaveStoredSessionIDs(StoredSessions);

		FEditorAnalyticsSession::Unlock();
	}

	if (CurrentSession)
	{
		// Reset all 'inactivity' timers to 'now'.
		double CurrTimSecs = FPlatformTime::Seconds();
		LastUserActivityTimeSecs.Store(CurrTimSecs);
		LastEditorActivityTimeSecs.Store(CurrTimSecs);

		// Register for crash and app state callbacks
		FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEditorSessionSummaryWriter::OnCrashing); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEditorSessionSummaryWriter::OnTerminate); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnVanillaStateChanged);
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
		// WARNING: To avoid breaking public API in 4.25.1, TotalUserInactivitySeconds field was repurposed to store the session duration. It should be renamed appropriately in 4.26.
		int32 OldSessionDurationSecs = FPlatformAtomics::AtomicRead(&CurrentSession->TotalUserInactivitySeconds);
		if (NewSessionDuration > OldSessionDurationSecs)
		{
			if (FPlatformAtomics::InterlockedCompareExchange(&CurrentSession->TotalUserInactivitySeconds, NewSessionDuration, OldSessionDurationSecs) == OldSessionDurationSecs)
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

	// If the Editor process CPU usage is high enough, this count as an activity.
	if (FPlatformTime::GetCPUTime().CPUTimePct > EditorSessionWriterDefs::IdleCpuUsagePercent)
	{
		UpdateEditorIdleTime(CurrentTimeSecs, /*bReset*/true);
	}

	// Update other session stats approximatively every minute.
	HeartbeatTimeElapsed += DeltaTime;

	// In the first seconds of the session, be more granular about updating the session (many crashes occurs there), update/save every second or so, then every minutes later on.
	if (HeartbeatTimeElapsed >= EditorSessionWriterDefs::HeartbeatPeriodSeconds || (CurrentTimeSecs - SessionStartTimeSecs <= 30.0 && HeartbeatTimeElapsed >= EditorSessionWriterDefs::EarlyHeartbeatPeriodSeconds))
	{
		HeartbeatTimeElapsed = 0.0f;

		// Check if the out of process monitor is running.
		if (CurrentSession->MonitorProcessID != 0 && !CurrentSession->MonitorExceptCode.IsSet())
		{
			// The out-of-process application reporting our crash shouldn't die before this process.
			if (!FPlatformProcess::IsApplicationRunning(CurrentSession->MonitorProcessID))
			{
				CurrentSession->MonitorExceptCode.Emplace(ECrashExitCodes::OutOfProcessReporterExitedUnexpectedly);
			}
		}

		// check if the debugger is present
		bool bIsDebuggerPresent = FPlatformMisc::IsDebuggerPresent();
		if (CurrentSession->bIsDebugger != bIsDebuggerPresent)
		{
			CurrentSession->bIsDebugger = bIsDebuggerPresent;

			if (!CurrentSession->bWasEverDebugger && CurrentSession->bIsDebugger)
			{
				CurrentSession->bWasEverDebugger = true;
			}
		}

		extern ENGINE_API float GAverageFPS;
		CurrentSession->AverageFPS = GAverageFPS;
		CurrentSession->bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
		CurrentSession->bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();

		TrySaveCurrentSession(FDateTime::UtcNow(), CurrentTimeSecs); // Saving also updates session duration/timestamp/userIdle/editorIdle
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

TUniquePtr<FEditorAnalyticsSession> FEditorSessionSummaryWriter::CreateCurrentSession(const FDateTime& StartupTimeUtc, uint32 OutOfProcessMonitorProcessId)
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

	Session->PlatformProcessID = FPlatformProcess::GetCurrentProcessId();
	Session->MonitorProcessID = OutOfProcessMonitorProcessId;
	Session->ProjectName = ProjectSettings.ProjectName.Len() ? ProjectSettings.ProjectName : FApp::GetProjectName();
	Session->ProjectID = ProjectSettings.ProjectID.ToString(EGuidFormats::DigitsWithHyphens);
	Session->ProjectDescription = ProjectSettings.Description;
	Session->ProjectVersion = ProjectSettings.ProjectVersion;
	Session->EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Changelist);
	Session->StartupTimestamp = StartupTimeUtc;
	Session->Timestamp = FDateTime::UtcNow();
	Session->bIsDebugger = FPlatformMisc::IsDebuggerPresent();
	Session->bWasEverDebugger = FPlatformMisc::IsDebuggerPresent();
	Session->CurrentUserActivity = GetUserActivityString();
	Session->bIsVanilla = GEngine && GEngine->IsVanillaProduct();

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

	// The out-of-process application reporting our crash shouldn't die before this process.
	if (Session->MonitorProcessID != 0 && !FPlatformProcess::IsApplicationRunning(Session->MonitorProcessID))
	{
		Session->MonitorExceptCode.Emplace(ECrashExitCodes::OutOfProcessReporterExitedUnexpectedly);
	}

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
		}

		// At last, try to save the session. It may fail, but the locklessly logged events above will carry the most important information.
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());
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

		// At last, try to save the session. It may fail, but the locklessly logged events above will carry the most important information.
		TrySaveCurrentSession(FDateTime::UtcNow(), FPlatformTime::Seconds());

		// NOTE: Don't explicitely Shutdown(), it is expected to be called on game thread to prevent unregistering delegate from a random thread.
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
			UpdateUserIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateEditorIdleTime(CurrTimeSecs, /*bReset*/false);
			UpdateSessionDuration(CurrTimeSecs);
			UpdateSessionTimestamp(CurrTimeUtc);
			CurrentSession->Save();
			SaveSessionLock.Unlock();
		}
		FEditorAnalyticsSession::Unlock();
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
