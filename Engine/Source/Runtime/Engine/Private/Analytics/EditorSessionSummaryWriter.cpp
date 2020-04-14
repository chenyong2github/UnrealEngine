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

	// The number of seconds required between two user inputs to consider the user as 'inactive'.
	constexpr double UserInactivitySecondsForIdleState = 120;

	// The upper CPU usage % considered as Idle. If the CPU usage goes above this threshold, the Editor is considered 'active'.
	constexpr float IdleCpuUsagePercent = 20;

	// The number of seconds required between Editor activities to consider the Editor as 'inactive' (user input, cpu burst).
	constexpr double EditorInactivitySecondsForIdleState = 120;
}

namespace EditorSessionWriterUtils
{
	bool UpdateIdleTime(const FDateTime& InActivityTimeUtc, TAtomic<uint64>& LastActivityUtcUnixTimestamp, volatile int32& TotalInactivitySecondsCounter, double InactivityThresholdSecondsForIdleState)
	{
		// Atomically load the time of the last activity. (user input, CPU burst/high usage, crash, terminate, shutdown)
		FDateTime LastActivityTime = FDateTime::FromUnixTimestamp(LastActivityUtcUnixTimestamp.Load());
		if (InActivityTimeUtc != LastActivityTime)
		{
			// The thread that exchanges the LastActivityUtcUnixTimestamp successfully is responsible to update the total counter. Other concurrent threads will fail
			// to update and some fractions of seconds can be lost, but that's neglictable in the overall idle computation.
			uint64 LastActivityExpectedTime = LastActivityTime.ToUnixTimestamp();
			if (LastActivityUtcUnixTimestamp.CompareExchange(LastActivityExpectedTime, InActivityTimeUtc.ToUnixTimestamp()))
			{
				// Check if this period of inactivity was long enough to be considered as 'idle'.
				FTimespan InactivityTimespan = InActivityTimeUtc - LastActivityTime;
				if (InactivityTimespan > FTimespan::FromSeconds(InactivityThresholdSecondsForIdleState))
				{
					FPlatformAtomics::InterlockedAdd(&TotalInactivitySecondsCounter, FMath::FloorToInt(static_cast<float>(InactivityTimespan.GetTotalSeconds())));
					return true; //  Total Idle time was updated.
				}
			}
		}

		return false; // Total Idle time wasn't updated.
	}

	bool IsMainLoopStaled(const FDateTime& CurrTime, const FDateTime& LastTickTime)
	{
		return (CurrTime - LastTickTime) > FTimespan::FromSeconds(2);
	}

	bool IsMainLoopStaled(const FDateTime& CurrTime, const TAtomic<uint64>& LastTickTimestamp)
	{
		return IsMainLoopStaled(CurrTime, FDateTime::FromUnixTimestamp(LastTickTimestamp));
	}
}

FEditorSessionSummaryWriter::FEditorSessionSummaryWriter(uint32 InOutOfProcessMonitorProcessId)
	: HeartbeatTimeElapsed(0.0f)
	, bShutdown(false)
	, OutOfProcessMonitorProcessId(InOutOfProcessMonitorProcessId)
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
		CurrentSession = CreateCurrentSession(OutOfProcessMonitorProcessId);
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
		LastSlateInteractionTime = FSlateApplication::Get().GetCurrentTime(); // Use same clock than Slate does.
		FDateTime CurrTimeUtc = FDateTime::UtcNow();
		LastTickUtcTime.Store(CurrTimeUtc.ToUnixTimestamp());
		LastEditorActivityUtcTime.Store(CurrTimeUtc.ToUnixTimestamp());
		LastUserActivityUtcTime.Store(CurrTimeUtc.ToUnixTimestamp());

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

void FEditorSessionSummaryWriter::UpdateTimestamp(const FDateTime& InCurrTimeUtc)
{
	CurrentSession->Timestamp = InCurrTimeUtc;
}

void FEditorSessionSummaryWriter::UpdateEditorIdleTime(const FDateTime& InActivityTimeUtc, bool bSaveSession)
{
	if (EditorSessionWriterUtils::UpdateIdleTime(InActivityTimeUtc, LastEditorActivityUtcTime, CurrentSession->TotalEditorInactivitySeconds, EditorSessionWriterDefs::EditorInactivitySecondsForIdleState) && bSaveSession)
	{
		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::UpdateUserIdleTime(const FDateTime& InActivityTimeUtc, bool bSaveSession)
{
	if (EditorSessionWriterUtils::UpdateIdleTime(InActivityTimeUtc, LastUserActivityUtcTime, CurrentSession->TotalUserInactivitySeconds, EditorSessionWriterDefs::UserInactivitySecondsForIdleState) && bSaveSession)
	{
		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::UpdateLegacyIdleTimes()
{
	int32 IdleSecondsTmp = FPlatformAtomics::AtomicRead(&CurrentSession->IdleSeconds); // Atomically load only once.

	// 1 + 1 minutes
	if (IdleSecondsTmp > (60 + 60))
	{
		FPlatformAtomics::InterlockedIncrement(&CurrentSession->Idle1Min); // User spent one more minute as idle > 1 min
	}

	// 5 + 1 minutes
	if (IdleSecondsTmp > (5 * 60 + 60))
	{
		FPlatformAtomics::InterlockedIncrement(&CurrentSession->Idle5Min); // User spent one more minute as idle > 5 min
	}

	// 30 + 1 minutes
	if (IdleSecondsTmp > (30 * 60 + 60))
	{
		FPlatformAtomics::InterlockedIncrement(&CurrentSession->Idle30Min); // User spent one more minute as idle > 30 min
	}
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
	}
	else
	{
		// How much time the Editor ran without user input. Slate must be access from main thread.
		FPlatformAtomics::InterlockedExchange(&CurrentSession->IdleSeconds, FMath::FloorToInt(static_cast<float>(FSlateApplication::Get().GetCurrentTime() - LastSlateInteractionTime)));

		FDateTime CurrTickTime = FDateTime::UtcNow();
		FDateTime LastTickTime = FDateTime::FromUnixTimestamp(LastTickUtcTime);

		// Did not 'Tick' for a long time? The main thread was busy (or blocked). Consider the Editor as active for that period of time.
		if (EditorSessionWriterUtils::IsMainLoopStaled(CurrTickTime, LastTickTime))
		{
			//UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Main loop staled"));
			LastEditorActivityUtcTime.Store(CurrTickTime.ToUnixTimestamp()); // Reset the Editor activity timer.
		}
		// The Editor process CPU usage is high enough? Consider this as an Editor activity.
		else if (FPlatformTime::GetCPUTime().CPUTimePct > EditorSessionWriterDefs::IdleCpuUsagePercent)
		{
			//UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Editor Usage: %s"), *LexToString(FPlatformTime::GetCPUTime().CPUTimePct));
			UpdateEditorIdleTime(CurrTickTime, /*bSaveSession*/true); // Only save if the Editor did not register any activities in the last n seconds.
		}
		else if ((CurrTickTime - FDateTime::FromUnixTimestamp(LastEditorActivityUtcTime)).GetTotalSeconds() > EditorSessionWriterDefs::EditorInactivitySecondsForIdleState)
		{
			UpdateEditorIdleTime(CurrTickTime, /*bSaveSession*/true); // This period of time is considered idle, record it now.
		}

		if ((CurrTickTime - FDateTime::FromUnixTimestamp(LastUserActivityUtcTime)).GetTotalSeconds() > EditorSessionWriterDefs::UserInactivitySecondsForIdleState)
		{
			UpdateUserIdleTime(CurrTickTime, /*bSaveSession*/true); // This period of time is considered idle, record it now.
		}

		LastTickUtcTime.Store(CurrTickTime.ToUnixTimestamp());

		// Update other session stats approximatively every minute.
		HeartbeatTimeElapsed += DeltaTime;

		// In the first minute, be more granular about updating session timestamp and save every second or so (Lot of abnormal terminations occur before the first minute).
		FTimespan SessionAge = CurrTickTime - CurrentSession->StartupTimestamp;
		if (SessionAge.GetTotalSeconds() < EditorSessionWriterDefs::HeartbeatPeriodSeconds && HeartbeatTimeElapsed >= EditorSessionWriterDefs::EarlyHeartbeatPeriodSeconds)
		{
			HeartbeatTimeElapsed = 0.0f;
			UpdateTimestamp(CurrTickTime);
			TrySaveCurrentSession();
		}
		// After the first minute, update the session every minute or so.
		else if (HeartbeatTimeElapsed > EditorSessionWriterDefs::HeartbeatPeriodSeconds)
		{
			HeartbeatTimeElapsed = 0.0f;

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

			UpdateTimestamp(CurrTickTime);
			UpdateLegacyIdleTimes();

			extern ENGINE_API float GAverageFPS;
			CurrentSession->AverageFPS = GAverageFPS;
			CurrentSession->bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
			CurrentSession->bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();

			TrySaveCurrentSession();
		}
	}
}

void FEditorSessionSummaryWriter::LowDriveSpaceDetected()
{
	if (CurrentSession)
	{
		CurrentSession->bIsLowDriveSpace = true;

		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::Shutdown()
{
	// NOTE: Initialize(), Shutdown() and ~FEditorSessionSummaryWriter() are expected to be called from the game thread only.
	if (CurrentSession && !bShutdown)
	{
		// NOTE: Shutdown() may crash if a delegate is broadcasted from another thread at the same time (that's a bug in 4.25) the delegate are modified.
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
		FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
		FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);
		FUserActivityTracking::OnActivityChanged.RemoveAll(this);
		FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
		FSlateApplication::Get().GetLastUserInteractionTimeUpdateEvent().RemoveAll(this);
		FCoreDelegates::OnHandleSystemError.RemoveAll(this);

		CurrentSession->bWasShutdown = true;
		FDateTime CurrTimeUtc = FDateTime::UtcNow();
		UpdateTimestamp(CurrTimeUtc);
		UpdateEditorIdleTime(CurrTimeUtc, /*bSaveSession*/false); // Saved below, don't save twice.
		UpdateUserIdleTime(CurrTimeUtc, /*bSaveSession*/false); // Saved below, don't save twice.
		UpdateLegacyIdleTimes();
		TrySaveCurrentSession();

		CurrentSession.Reset();
	}

	bShutdown = true;
}

TUniquePtr<FEditorAnalyticsSession> FEditorSessionSummaryWriter::CreateCurrentSession(uint32 OutOfProcessMonitorProcessId)
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
	Session->Timestamp = Session->StartupTimestamp = FDateTime::UtcNow();
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

	return Session;
}

extern CORE_API bool GIsGPUCrashed;
void FEditorSessionSummaryWriter::OnCrashing()
{
	// NOTE: This function is called from the crashing thread or the a crash processing thread and is concurrent with other functions such as Tick(), Initialize() or Shutdown() running on the game thread.
	if (CurrentSession != nullptr)
	{
		FDateTime CurrTime = FDateTime::UtcNow();

		// If the main loop was not ticking, consider the Editor as 'active'. It was doing something or was waiting for something.
		if (!EditorSessionWriterUtils::IsMainLoopStaled(CurrTime, LastTickUtcTime))
		{
			UpdateEditorIdleTime(CurrTime, /*bSaveSession*/false);
		}
		UpdateUserIdleTime(CurrTime, /*bSaveSession*/false);
		UpdateLegacyIdleTimes();
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Crashed, CurrTime);

		if (GIsGPUCrashed)
		{
			CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::GpuCrashed, CurrTime);
		}

		// NOTE: Don't try to save the session, we don't know if the lock used to save the key-store is corrupted (or held by the crashing thread) when OnCrashing() is called from the crash handler thread.
	}
}

void FEditorSessionSummaryWriter::OnTerminate()
{
	// NOTE: This function can be called from any thread (from the crashing thread too) and is likely concurrent with other functions such as Tick(), Initiallize() or Shutdown() running on the game thread.
	if (CurrentSession != nullptr)
	{
		FDateTime CurrTime = FDateTime::UtcNow();
		
		// If the main loop was not ticking, consider the Editor as 'active'. It was doing something or was waiting for something.
		if (!EditorSessionWriterUtils::IsMainLoopStaled(CurrTime, LastTickUtcTime))
		{
			UpdateEditorIdleTime(CurrTime, /*bSaveSession*/false);
		}
		UpdateUserIdleTime(CurrTime, /*bSaveSession*/false);
		UpdateLegacyIdleTimes();
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Terminated, CurrTime);

		// NOTE: Don't try to save the session, we don't know if this is called from a crash handler (and if the crashing thread corrupted (or held) the lock to save the key-store.)
		// NOTE: Don't explicitely Shutdown(), it is expected to be called on game thread to prevent unregistered delegate from a random thread. Just let the normal flow call Shutdown() or not. Destructor will do in last resort.
	}
}

void FEditorSessionSummaryWriter::OnVanillaStateChanged(bool bIsVanilla)
{
	if (CurrentSession != nullptr && CurrentSession->bIsVanilla != bIsVanilla)
	{
		CurrentSession->bIsVanilla = bIsVanilla;

		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::OnUserActivity(const FUserActivity& UserActivity)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->CurrentUserActivity = GetUserActivityString();

		TrySaveCurrentSession();
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
	LastSlateInteractionTime = CurrSlateInteractionTime;
	UpdateLegacyIdleTimes();

	FDateTime CurrTimeUtc = FDateTime::UtcNow();
	UpdateUserIdleTime(CurrTimeUtc, /*bSaveSession*/true);
	UpdateEditorIdleTime(CurrTimeUtc, /*bSaveSession*/true);
}

void FEditorSessionSummaryWriter::OnEnterPIE(const bool /*bIsSimulating*/)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->bIsInPIE = true;
		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::OnExitPIE(const bool /*bIsSimulating*/)
{
	if (CurrentSession != nullptr)
	{
		CurrentSession->bIsInPIE = false;
		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::TrySaveCurrentSession()
{
	if (FEditorAnalyticsSession::TryLock()) // Inter-process lock to grant this process exclusive access to the key-store file/registry.
	{
		FScopeLock ScopedLock(&SaveSessionLock); // Intra-process lock to grant the calling thread exclusive access to the key-store file/registry.
		CurrentSession->Save();
		FEditorAnalyticsSession::Unlock();
	}
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
