// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "Interfaces/IAnalyticsProvider.h"
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
	static const float HeartbeatPeriodSeconds = 60;
}

FEditorSessionSummaryWriter::FEditorSessionSummaryWriter() :
	CurrentSession(nullptr)
	, StartupSeconds(0.0)
	, LastUserInteractionTime(0.0f)
	, HeartbeatTimeElapsed(0.0f)
	, bShutdown(false)
{
}

void FEditorSessionSummaryWriter::Initialize()
{
	// Register for crash and app state callbacks
	FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEditorSessionSummaryWriter::OnCrashing);
	FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEditorSessionSummaryWriter::OnTerminate);
	FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnVanillaStateChanged);
	FUserActivityTracking::OnActivityChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnUserActivity);
	FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEditorSessionSummaryWriter::Tick);

	StartupSeconds = FPlatformTime::Seconds();

	InitializeSessions();
}

void FEditorSessionSummaryWriter::InitializeSessions()
{
	if (!FEngineAnalytics::IsAvailable() || CurrentSession != nullptr)
	{
		return;
	}

	UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Initializing EditorSessionSummaryWriter for editor session tracking"));

	if (FEditorAnalyticsSession::Lock())
	{
		// Create a session Session for this session
		CurrentSession = CreateCurrentSession();
		CurrentSession->Save();

		UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummaryWriter initialized"));

		// update session list string
		TArray<FString> StoredSessions;

		FEditorAnalyticsSession::GetStoredSessionIDs(StoredSessions);
		StoredSessions.Add(CurrentSession->SessionId);

		FEditorAnalyticsSession::SaveStoredSessionIDs(StoredSessions);

		FEditorAnalyticsSession::Unlock();
	}
}

void FEditorSessionSummaryWriter::UpdateTimestamps()
{
	CurrentSession->Timestamp = FDateTime::UtcNow();

	const double CurrentSeconds = FPlatformTime::Seconds();
	CurrentSession->SessionDuration = FMath::FloorToInt(CurrentSeconds - StartupSeconds);

	const double IdleSeconds = CurrentSeconds - LastUserInteractionTime;

	// 1 + 1 minutes
	if (IdleSeconds > (60 + 60))
	{
		CurrentSession->Idle1Min += 1;
	}

	// 5 + 1 minutes
	if (IdleSeconds > (5 * 60 + 60))
	{
		CurrentSession->Idle5Min += 1;
	}

	// 30 + 1 minutes
	if (IdleSeconds > (30 * 60 + 60))
	{
		CurrentSession->Idle30Min += 1;
	}
}

void FEditorSessionSummaryWriter::Tick(float DeltaTime)
{
	if (bShutdown)
	{
		return;
	}

	// cache the last user interaction time so that during a crash we have access to it
	LastUserInteractionTime = FSlateApplication::Get().GetLastUserInteractionTime();

	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > EditorSessionWriterDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		// Try late initialization
		InitializeSessions();

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
	CurrentSession->bIsLowDriveSpace = true;

	TrySaveCurrentSession();
}

void FEditorSessionSummaryWriter::Shutdown()
{
	FCoreDelegates::OnHandleSystemError.RemoveAll(this);
	FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
	FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);

	FUserActivityTracking::OnActivityChanged.RemoveAll(this);

	if (CurrentSession != nullptr)
	{
		if (!CurrentSession->bIsTerminating && !CurrentSession->bCrashed)
		{
			FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);

			CurrentSession->bWasShutdown = true;
		}

		UpdateTimestamps();

		TrySaveCurrentSession();

		delete CurrentSession;
		CurrentSession = nullptr;
		bShutdown = true;
	}
}

FEditorAnalyticsSession* FEditorSessionSummaryWriter::CreateCurrentSession() const
{
	FEditorAnalyticsSession* Session = new FEditorAnalyticsSession();

	FGuid SessionId;
	if (FGuid::Parse(FEngineAnalytics::GetProvider().GetSessionID(), SessionId))
	{
		// convert session GUID to one without braces or other chars that might not be suitable for storage
		Session->SessionId = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
	}
	else
	{
		Session->SessionId = FEngineAnalytics::GetProvider().GetSessionID();
	}

	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();

	Session->PlatformProcessID = FPlatformProcess::GetCurrentProcessId();
	Session->ProjectName = ProjectSettings.ProjectName;
	Session->ProjectID = ProjectSettings.ProjectID.ToString(EGuidFormats::DigitsWithHyphens);
	Session->ProjectDescription = ProjectSettings.Description;
	Session->ProjectVersion = ProjectSettings.ProjectVersion;
	Session->EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Changelist);
	Session->Timestamp = Session->StartupTimestamp = FDateTime::UtcNow();
	Session->SessionDuration = 0;
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
	if (CurrentSession != nullptr)
	{
		UpdateTimestamps();

		CurrentSession->bCrashed = true;
		CurrentSession->bGPUCrashed = GIsGPUCrashed;

		TrySaveCurrentSession();
	}
}

void FEditorSessionSummaryWriter::OnTerminate()
{
	if (CurrentSession != nullptr)
	{
		UpdateTimestamps();

		CurrentSession->bIsTerminating = true;

		TrySaveCurrentSession();

		if (IsEngineExitRequested())
		{
			Shutdown();
		}
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

FString FEditorSessionSummaryWriter::GetUserActivityString() const
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
	if (FEditorAnalyticsSession::Lock())
	{
		CurrentSession->Save();

		FEditorAnalyticsSession::Unlock();
	}
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
