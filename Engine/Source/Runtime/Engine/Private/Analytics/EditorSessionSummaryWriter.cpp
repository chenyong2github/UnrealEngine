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

#define LOCTEXT_NAMESPACE "SessionSummary"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSessionSummary, Verbose, All);

namespace EditorSessionWriterDefs
{
	// Number of seconds to wait between each update of the mutable metrics.
	static const float HeartbeatPeriodSeconds = 60;
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
		// Register for crash and app state callbacks
		FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEditorSessionSummaryWriter::OnCrashing); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEditorSessionSummaryWriter::OnTerminate); // WARNING: Don't assume this function is only called from game thread.
		FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnVanillaStateChanged);
		FUserActivityTracking::OnActivityChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnUserActivity);
		FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEditorSessionSummaryWriter::Tick);
	}
}

void FEditorSessionSummaryWriter::UpdateTimestamps()
{
	CurrentSession->Timestamp = FDateTime::UtcNow();
	UpdateIdleTimes();
}

void FEditorSessionSummaryWriter::UpdateIdleTimes()
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

	if (CurrentSession != nullptr)
	{
		// Note: Update idle time in Tick() because Slate cannot be invoked from any thread and UpdateTimeStamps() can be called from a crashing thread.
		//       Compute the idle time from Slate point-of view. Note that some tasks blocking the UI (such as importing large assets) may be considered idle time.
		CurrentSession->IdleSeconds = FSlateApplication::Get().GetLastUserInteractionTime() != 0 ? // In case Slate did not register any interaction yet (ex the user just launches the Editor and goes away)
			FMath::FloorToInt(static_cast<float>(FSlateApplication::Get().GetCurrentTime() - FSlateApplication::Get().GetLastUserInteractionTime())) :
			FMath::FloorToInt(static_cast<float>((FDateTime::UtcNow() - CurrentSession->StartupTimestamp).GetTotalSeconds()));
	}

	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > EditorSessionWriterDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		// Try late initialization (in case the global lock was already taken during init and the session couldn't be created or the user just toggled 'send data' on).
		if (CurrentSession == nullptr)
		{
			Initialize();
		}

		if (CurrentSession != nullptr)
		{
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

			UpdateTimestamps();

#if WITH_EDITOR
			extern ENGINE_API float GAverageFPS;

			CurrentSession->AverageFPS = GAverageFPS;
			CurrentSession->bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
			CurrentSession->bIsInEnterprise = IProjectManager::Get().IsEnterpriseProject();
			CurrentSession->bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();
#endif

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
		FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
		FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);
		FUserActivityTracking::OnActivityChanged.RemoveAll(this);
		FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
		FCoreDelegates::OnHandleSystemError.RemoveAll(this);

		CurrentSession->bWasShutdown = true;
		UpdateTimestamps();
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
		UpdateIdleTimes();
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Crashed, FDateTime::UtcNow());

		if (GIsGPUCrashed)
		{
			CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::GpuCrashed, FDateTime::UtcNow());
		}

		// NOTE: Don't try to save the session, we don't know if the lock used to save the key-store is corrupted (or held by the crashing thread) when OnCrashing() is called from the crash handler thread.
	}
}

void FEditorSessionSummaryWriter::OnTerminate()
{
	// NOTE: This function can be called from any thread (from the crashing thread too) and is likely concurrent with other functions such as Tick(), Initiallize() or Shutdown() running on the game thread.
	if (CurrentSession != nullptr)
	{
		UpdateIdleTimes();
		CurrentSession->LogEvent(FEditorAnalyticsSession::EEventType::Terminated, FDateTime::UtcNow());

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
