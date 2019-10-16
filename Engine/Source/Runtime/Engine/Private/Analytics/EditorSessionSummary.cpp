// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorSessionSummary.h"

#include "AnalyticsEventAttribute.h"
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
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"

#if WITH_EDITOR
#include "IVREditorModule.h"
#include "Kismet2/DebuggerCommands.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "SessionSummary"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSessionSummary, Verbose, All);

namespace SessionSummaryDefs
{
	static const FTimespan SessionRecordExpiration = FTimespan::FromDays(30.0);
	static const FTimespan SessionRecordTimeout = FTimespan::FromMinutes(10.0);
	static const FTimespan GlobalLockWaitTimeout = FTimespan::FromSeconds(0.5);
	static const int HeartbeatPeriodSeconds(60);

	static const FString FalseValueString(TEXT("0"));
	static const FString TrueValueString(TEXT("1"));

	// shutdown types
	static const FString RunningSessionToken(TEXT("Running"));
	static const FString ShutdownSessionToken(TEXT("Shutdown"));
	static const FString CrashSessionToken(TEXT("Crashed"));
	static const FString TerminatedSessionToken(TEXT("Terminated"));
	static const FString DebuggerSessionToken(TEXT("Debugger"));
	static const FString AbnormalSessionToken(TEXT("AbnormalShutdown"));

	static const FString DefaultUserActivity(TEXT("Unknown"));
	static const FString UnknownProjectValueString(TEXT("UnknownProject"));

	// storage location
	static const FString StoreId(TEXT("Epic Games"));
	static const FString SessionSummarySection(TEXT("Unreal Engine/Session Summary/1_0"));
	static const FString GlobalLockName(TEXT("UE4_SessionSummary_Lock"));
	static const FString SessionListStoreKey(TEXT("SessionList"));

	// general values
	static const FString ProjectNameStoreKey(TEXT("ProjectName"));
	static const FString SessionIdStoreKey(TEXT("SessionId"));
	static const FString PlatformProcessIDKey(TEXT("PlatformProcessID"));
	static const FString EngineVersionStoreKey(TEXT("EngineVersion"));
	static const FString StatusStoreKey(TEXT("LastExecutionState"));
	static const FString UserActivityStoreKey(TEXT("CurrentUserActivity"));
	static const FString PluginsStoreKey(TEXT("Plugins"));
	static const FString AverageFPSStoreKey(TEXT("AverageFPS"));

	// timestamps
	static const FString TimestampStoreKey(TEXT("Timestamp"));
	static const FString StartupTimestampStoreKey(TEXT("StartupTimestamp"));
	static const FString SessionDurationStoreKey(TEXT("SessionDuration"));
	static const FString Idle1MinStoreKey(TEXT("Idle1Min"));
	static const FString Idle5MinStoreKey(TEXT("Idle5Min"));
	static const FString Idle30MinStoreKey(TEXT("Idle30Min"));
	
	// boolean flags
	static const FString IsCrashStoreKey(TEXT("IsCrash"));
	static const FString IsGPUCrashStoreKey(TEXT("IsGPUCrash"));
	static const FString IsDebuggerStoreKey(TEXT("IsDebugger"));
	static const FString WasDebuggerStoreKey(TEXT("WasEverDebugger"));
	static const FString IsVanillaStoreKey(TEXT("IsVanilla"));
	static const FString IsTerminatingKey(TEXT("Terminating"));
	static const FString WasShutdownStoreKey(TEXT("WasShutdown"));
	static const FString IsInPIEStoreKey(TEXT("IsInPIE"));
	static const FString IsInEnterpriseStoreKey(TEXT("IsInEnterprise"));
	static const FString IsInVRModeStoreKey(TEXT("IsInVRMode"));
	static const FString IsLowDriveSpaceDetectedStoreKey(TEXT("LowDriveSpaceDetected"));
}

struct FEditorSessionRecord
{
	FString SessionId;
	FString ProjectName;
	FString EngineVersion;
	int32 PlatformProcessID;
	FDateTime StartupTimestamp;
	FDateTime Timestamp;
	int32 Idle1Min;
	int32 Idle5Min;
	int32 Idle30Min;
	FString CurrentUserActivity;
	TArray<FString> Plugins;
	float AverageFPS;

	bool bCrashed : 1;
	bool bGPUCrashed : 1;
	bool bIsDebugger : 1;
	bool bWasEverDebugger : 1;
	bool bIsVanilla : 1;
	bool bIsTerminating : 1;
	bool bWasShutdown : 1;
	bool bIsInPIE : 1;
	bool bIsInEnterprise : 1;
	bool bIsInVRMode : 1;
	bool bLowDriveSpaceDetected : 1;

	FEditorSessionRecord()
	{
		PlatformProcessID = 0;
		StartupTimestamp = FDateTime::MinValue();
		Timestamp = FDateTime::MinValue();
		Idle1Min = 0;
		Idle5Min = 0;
		Idle30Min = 0;
		AverageFPS = 0;
		bCrashed = false;
		bGPUCrashed = false;
		bIsDebugger = false;
		bWasEverDebugger = false;
		bIsVanilla = false;
		bIsTerminating = false;
		bWasShutdown = false;
		bIsInPIE = false;
		bIsInEnterprise = false;
		bIsInVRMode = false;
		bLowDriveSpaceDetected = false;
	}
};

// Utilities for writing to stored values
namespace EditorSessionSummaryUtils
{
	FString TimestampToString(FDateTime InTimestamp)
	{
		return LexToString(InTimestamp.ToUnixTimestamp());
	}

	FDateTime StringToTimestamp(FString InString)
	{
		int64 TimestampUnix;
		if (LexTryParseString(TimestampUnix, *InString))
		{
			return FDateTime::FromUnixTimestamp(TimestampUnix);
		}
		return FDateTime::MinValue();
	}

	FString BoolToStoredString(bool bInValue)
	{
		return bInValue ? SessionSummaryDefs::TrueValueString : SessionSummaryDefs::FalseValueString;
	}

	bool GetStoredBool(const FString& SectionName, const FString& StoredKey)
	{
		FString StoredString = SessionSummaryDefs::FalseValueString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, StoredKey, StoredString);

		return StoredString == SessionSummaryDefs::TrueValueString;
	}

	FString GetSessionStorageLocation(const FEditorSessionRecord& Record)
	{
		return SessionSummaryDefs::SessionSummarySection + TEXT("/") + Record.SessionId;
	}
}

/* FEditorSessionSummaryWriter */

FEditorSessionSummaryWriter::FEditorSessionSummaryWriter() :
	CurrentSession(nullptr)
	, LastUserInteractionTime(0.0f)
	, HeartbeatTimeElapsed(0.0f)
	, bInitializedRecords(false)
	, bShutdown(false)
{

}

void FEditorSessionSummaryWriter::Initialize()
{
	// Register for crash and app state callbacks
	FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEditorSessionSummaryWriter::OnCrashing);
	FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEditorSessionSummaryWriter::OnTerminate);
	FUserActivityTracking::OnActivityChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnUserActivity);
	FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEditorSessionSummaryWriter::OnVanillaStateChanged);
	FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEditorSessionSummaryWriter::Tick);

	const bool bFirstInitAttempt = true;
	InitializeRecords(bFirstInitAttempt);
}

void FEditorSessionSummaryWriter::InitializeRecords(bool bFirstAttempt)
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}

	if (bInitializedRecords)
	{
		return;
	}

	// Scoped lock
	FSystemWideCriticalSection StoredValuesLock(SessionSummaryDefs::GlobalLockName, bFirstAttempt ? SessionSummaryDefs::GlobalLockWaitTimeout : FTimespan::Zero());

	if (StoredValuesLock.IsValid())
	{
		UE_LOG(LogEditorSessionSummary, Verbose, TEXT("Initializing EditorSessionSummaryWriter for editor session tracking"));

		// Create a session record for this session
		CurrentSession = CreateRecordForCurrentSession();
		CurrentSessionSectionName = EditorSessionSummaryUtils::GetSessionStorageLocation(*CurrentSession);
		WriteStoredRecord(*CurrentSession);

		bInitializedRecords = true;

		UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummaryWriter initialized"));

		// update session list string
		FString SessionListString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SessionSummaryDefs::SessionSummarySection, SessionSummaryDefs::SessionListStoreKey, SessionListString);

		if (!SessionListString.IsEmpty())
		{
			SessionListString.Append(TEXT(","));
		}
		SessionListString.Append(CurrentSession->SessionId);

		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, SessionSummaryDefs::SessionSummarySection, SessionSummaryDefs::SessionListStoreKey, SessionListString);
	}
}

void FEditorSessionSummaryWriter::UpdateTimestamps()
{
	if (!ensure(bInitializedRecords))
	{
		return;
	}

	CurrentSession->Timestamp = FDateTime::UtcNow();
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::TimestampStoreKey, EditorSessionSummaryUtils::TimestampToString(CurrentSession->Timestamp));

	const float IdleSeconds = FPlatformTime::Seconds() - LastUserInteractionTime;

	// 1 + 1 minutes
	if (IdleSeconds > (60 + 60))
	{
		CurrentSession->Idle1Min += 1;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::Idle1MinStoreKey, FString::FromInt(CurrentSession->Idle1Min));
	}

	// 5 + 1 minutes
	if (IdleSeconds > (5 * 60 + 60))
	{
		CurrentSession->Idle5Min += 1;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::Idle5MinStoreKey, FString::FromInt(CurrentSession->Idle5Min));
	}

	// 30 + 1 minutes
	if (IdleSeconds > (30 * 60 + 60))
	{
		CurrentSession->Idle30Min += 1;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::Idle30MinStoreKey, FString::FromInt(CurrentSession->Idle30Min));
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

	if (HeartbeatTimeElapsed > (float) SessionSummaryDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		// Try late initialization
		const bool bFirstInitAttempt = false;
		InitializeRecords(bFirstInitAttempt);

		if (bInitializedRecords)
		{
			// check if the debugger is present
			bool bIsDebuggerPresent = FPlatformMisc::IsDebuggerPresent();
			if (CurrentSession->bIsDebugger != bIsDebuggerPresent)
			{
				CurrentSession->bIsDebugger = bIsDebuggerPresent;
				FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsDebuggerStoreKey, EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bIsDebugger));

				if (!CurrentSession->bWasEverDebugger && CurrentSession->bIsDebugger)
				{
					CurrentSession->bWasEverDebugger = true;
					FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::WasDebuggerStoreKey, SessionSummaryDefs::TrueValueString);
				}
			}

			UpdateTimestamps();

#if WITH_EDITOR
			extern ENGINE_API float GAverageFPS;

			CurrentSession->AverageFPS = GAverageFPS;
			const FString AverageFPSString = FString::SanitizeFloat(GAverageFPS);
			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::AverageFPSStoreKey, AverageFPSString);

			CurrentSession->bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();
			const FString IsInPIEString = EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bIsInPIE);
			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsInPIEStoreKey, IsInPIEString);

			CurrentSession->bIsInEnterprise = IProjectManager::Get().IsEnterpriseProject();
			const FString IsInEnterpriseString = EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bIsInEnterprise);
			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsInEnterpriseStoreKey, IsInEnterpriseString);

			CurrentSession->bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
			const FString IsInVRModeString = EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bIsInVRMode);
			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsInVRModeStoreKey, IsInVRModeString);

#endif
		}
	}
}

void FEditorSessionSummaryWriter::LowDriveSpaceDetected()
{
	if (bInitializedRecords && !CurrentSession->bLowDriveSpaceDetected)
	{
		CurrentSession->bLowDriveSpaceDetected = true;

		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsLowDriveSpaceDetectedStoreKey, SessionSummaryDefs::TrueValueString);
	}
}

void FEditorSessionSummaryWriter::Shutdown()
{
	FCoreDelegates::OnHandleSystemError.RemoveAll(this);
	FCoreDelegates::ApplicationHasReactivatedDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
	FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);

	// Clear the session record for this session
	if (bInitializedRecords)
	{
		if (!CurrentSession->bIsTerminating && !CurrentSession->bCrashed)
		{
			CurrentSession->bWasShutdown = true;
			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::WasShutdownStoreKey, SessionSummaryDefs::TrueValueString);
		}

		delete CurrentSession;
		bInitializedRecords = false;
		bShutdown = true;
	}
}

void FEditorSessionSummaryWriter::WriteStoredRecord(const FEditorSessionRecord& Record) const
{
	FString StorageLocation = EditorSessionSummaryUtils::GetSessionStorageLocation(Record);
	FString PluginsString = FString::Join(Record.Plugins, TEXT(","));

	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::ProjectNameStoreKey, Record.ProjectName);
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsCrashStoreKey, SessionSummaryDefs::FalseValueString);
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::EngineVersionStoreKey, Record.EngineVersion);
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::StartupTimestampStoreKey, EditorSessionSummaryUtils::TimestampToString(Record.StartupTimestamp));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::TimestampStoreKey, EditorSessionSummaryUtils::TimestampToString(Record.Timestamp));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::Idle1MinStoreKey, FString::FromInt(Record.Idle1Min));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::Idle5MinStoreKey, FString::FromInt(Record.Idle5Min));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::Idle30MinStoreKey, FString::FromInt(Record.Idle30Min));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::UserActivityStoreKey, Record.CurrentUserActivity);
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsVanillaStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsVanilla));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsTerminatingKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsTerminating));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::PlatformProcessIDKey, FString::FromInt(Record.PlatformProcessID));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::PluginsStoreKey, PluginsString);
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::AverageFPSStoreKey, FString::SanitizeFloat(Record.AverageFPS));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsDebuggerStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsDebugger));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::WasDebuggerStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bWasEverDebugger));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::WasShutdownStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bWasShutdown));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsInPIEStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsInPIE));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsInEnterpriseStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsInEnterprise));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsInVRModeStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bIsInVRMode));
	FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, StorageLocation, SessionSummaryDefs::IsLowDriveSpaceDetectedStoreKey, EditorSessionSummaryUtils::BoolToStoredString(Record.bLowDriveSpaceDetected));
}

FEditorSessionRecord* FEditorSessionSummaryWriter::CreateRecordForCurrentSession() const
{
	FEditorSessionRecord* Record = new FEditorSessionRecord();

	FGuid SessionId;
	if (FGuid::Parse(FEngineAnalytics::GetProvider().GetSessionID(), SessionId))
	{
		// convert session guid to one without braces or other chars that might not be suitable for storage
		Record->SessionId = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
	}
	else
	{
		Record->SessionId = FEngineAnalytics::GetProvider().GetSessionID();
	}

	const uint32 ProcId = FPlatformProcess::GetCurrentProcessId();
	FString CurrentProcessIDString = FString::FromInt(ProcId);

	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();

	Record->ProjectName = ProjectSettings.ProjectName;
	Record->EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Changelist);
	Record->Timestamp = Record->StartupTimestamp = FDateTime::UtcNow();
	Record->bIsDebugger = FPlatformMisc::IsDebuggerPresent();
	Record->bWasEverDebugger = FPlatformMisc::IsDebuggerPresent();
	Record->CurrentUserActivity = GetUserActivityString();
	Record->bIsVanilla = GEngine && GEngine->IsVanillaProduct();

	TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : Plugins)
	{
		Record->Plugins.Add(Plugin->GetName());
	}

	Record->Plugins.Sort();

	return Record;
}

extern CORE_API bool GIsGPUCrashed;
void FEditorSessionSummaryWriter::OnCrashing()
{
	if (bInitializedRecords && !CurrentSession->bCrashed)
	{
		UpdateTimestamps();

		CurrentSession->bCrashed = true;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsCrashStoreKey, SessionSummaryDefs::TrueValueString);
		CurrentSession->bGPUCrashed = GIsGPUCrashed;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsGPUCrashStoreKey, EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bGPUCrashed));
	}
}

void FEditorSessionSummaryWriter::OnTerminate()
{
	if (bInitializedRecords && !CurrentSession->bIsTerminating)
	{
		UpdateTimestamps();

		CurrentSession->bIsTerminating = true;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsTerminatingKey, SessionSummaryDefs::TrueValueString);

		if (IsEngineExitRequested())
		{
			Shutdown();
		}
	}
}

void FEditorSessionSummaryWriter::OnVanillaStateChanged(bool bIsVanilla)
{
	if (bInitializedRecords && CurrentSession->bIsVanilla != bIsVanilla)
	{
		CurrentSession->bIsVanilla = bIsVanilla;
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::IsVanillaStoreKey, EditorSessionSummaryUtils::BoolToStoredString(CurrentSession->bIsVanilla));
	}
}

void FEditorSessionSummaryWriter::OnUserActivity(const FUserActivity& UserActivity)
{
	if (bInitializedRecords && !CurrentSession->bCrashed)
	{
		CurrentSession->CurrentUserActivity = GetUserActivityString();
		FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, CurrentSessionSectionName, SessionSummaryDefs::UserActivityStoreKey, CurrentSession->CurrentUserActivity);
	}
}

FString FEditorSessionSummaryWriter::GetUserActivityString() const
{
	const FUserActivity& UserActivity = FUserActivityTracking::GetUserActivity();

	if (UserActivity.ActionName.IsEmpty())
	{
		return SessionSummaryDefs::DefaultUserActivity;
	}

	return UserActivity.ActionName;
}

/* FEditorSessionSummarySender */

FEditorSessionSummarySender::FEditorSessionSummarySender() :
	HeartbeatTimeElapsed(0.0f)
{

}

void FEditorSessionSummarySender::Initialize()
{
	SendStoredRecords(SessionSummaryDefs::GlobalLockWaitTimeout);
}

void FEditorSessionSummarySender::Tick(float DeltaTime)
{
	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > (float) SessionSummaryDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		SendStoredRecords(FTimespan::Zero());
	}
}

void FEditorSessionSummarySender::SendStoredRecords(FTimespan Timeout) const
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}

	TArray<FEditorSessionRecord> SessionRecordsToReport;

	{
		// Scoped lock
		FSystemWideCriticalSection StoredValuesLock(SessionSummaryDefs::GlobalLockName, Timeout);

		if (StoredValuesLock.IsValid())
		{
			// Get list of sessions in storage
			TArray<FEditorSessionRecord> ExistingRecords = ReadStoredRecords();

			TArray<FEditorSessionRecord> SessionRecordsToDelete;

			// Check each stored session to see if they should be sent or not 
			for (FEditorSessionRecord& Record : ExistingRecords)
			{
				if (IsSessionProcessRunning(Record))
				{
					// Skip processes that are still running
					continue;
				}

				FTimespan RecordAge = FDateTime::UtcNow() - Record.Timestamp;

				if (RecordAge > SessionSummaryDefs::SessionRecordTimeout)
				{
					if (RecordAge < SessionSummaryDefs::SessionRecordExpiration)
					{
						SessionRecordsToReport.Add(Record);
					}
					SessionRecordsToDelete.Add(Record);
				}
			}

			for (const FEditorSessionRecord& ToDelete : SessionRecordsToDelete)
			{
				DeleteStoredRecord(ToDelete);
				ExistingRecords.RemoveAll([&ToDelete](const FEditorSessionRecord& Record)
					{
						return Record.SessionId == ToDelete.SessionId;
					});
			}

			// build up a new SessionList string
			FString SessionListString;

			for (const FEditorSessionRecord& Remaining : ExistingRecords)
			{
				if (!SessionListString.IsEmpty())
				{
					SessionListString.Append(TEXT(","));
				}

				SessionListString.Append(Remaining.SessionId);
			}

			FPlatformMisc::SetStoredValue(SessionSummaryDefs::StoreId, SessionSummaryDefs::SessionSummarySection, SessionSummaryDefs::SessionListStoreKey, SessionListString);
		}
	}

	for (const FEditorSessionRecord& Record : SessionRecordsToReport)
	{
		SendSessionSummaryEvent(Record);
	}
}

void FEditorSessionSummarySender::DeleteStoredRecord(const FEditorSessionRecord& Record) const
{
	// Delete the session record in storage
	FString SectionName = EditorSessionSummaryUtils::GetSessionStorageLocation(Record);

	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::ProjectNameStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsGPUCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::EngineVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::StartupTimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::TimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle1MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle5MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle30MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::WasDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::WasShutdownStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::UserActivityStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsVanillaStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsTerminatingKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::PlatformProcessIDKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::PluginsStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::AverageFPSStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsInPIEStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsInEnterpriseStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsInVRModeStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::IsLowDriveSpaceDetectedStoreKey);
}

bool FEditorSessionSummarySender::IsSessionProcessRunning(const FEditorSessionRecord& Record) const
{
	FProcHandle Handle = FPlatformProcess::OpenProcess((uint32) Record.PlatformProcessID);
	if (Handle.IsValid())
	{
		const bool bIsRunning = FPlatformProcess::IsProcRunning(Handle);
		FPlatformProcess::CloseProc(Handle);
		if (bIsRunning)
		{
			return true;
		}
	}

	return false;
}

TArray<FEditorSessionRecord> FEditorSessionSummarySender::ReadStoredRecords() const
{
	TArray<FEditorSessionRecord> Records;

	FString SessionListString;
	FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SessionSummaryDefs::SessionSummarySection, SessionSummaryDefs::SessionListStoreKey, SessionListString);

	TArray<FString> SessionIDs;
	SessionListString.ParseIntoArray(SessionIDs, TEXT(","));

	// Retrieve all the sessions in the list from storage
	for (const FString& SessionId : SessionIDs)
	{
		FEditorSessionRecord NewRecord;
		NewRecord.SessionId = SessionId;

		FString SectionName = EditorSessionSummaryUtils::GetSessionStorageLocation(NewRecord);

		// Read values
		FString PlatformProcessIDString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::PlatformProcessIDKey, PlatformProcessIDString);
		FString EngineVersionString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::EngineVersionStoreKey, EngineVersionString);
		FString TimestampString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::TimestampStoreKey, TimestampString);
		FString ProjectName = SessionSummaryDefs::UnknownProjectValueString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::ProjectNameStoreKey, ProjectName);
		FString UserActivityString = SessionSummaryDefs::DefaultUserActivity;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::UserActivityStoreKey, UserActivityString);
		FString StartupTimestampString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::StartupTimestampStoreKey, StartupTimestampString);
		FString Idle1MinString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle1MinStoreKey, Idle1MinString);
		FString Idle5MinString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle5MinStoreKey, Idle5MinString);
		FString Idle30MinString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::Idle30MinStoreKey, Idle30MinString);
		FString PluginsString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::PluginsStoreKey, PluginsString);
		FString AverageFPSString;
		FPlatformMisc::GetStoredValue(SessionSummaryDefs::StoreId, SectionName, SessionSummaryDefs::AverageFPSStoreKey, AverageFPSString);

		// Create new record from the read values
		NewRecord.PlatformProcessID = FCString::Atoi(*PlatformProcessIDString);
		NewRecord.ProjectName = ProjectName;
		NewRecord.EngineVersion = EngineVersionString;
		NewRecord.StartupTimestamp = EditorSessionSummaryUtils::StringToTimestamp(StartupTimestampString);
		NewRecord.Timestamp = EditorSessionSummaryUtils::StringToTimestamp(TimestampString);
		NewRecord.Idle1Min = FCString::Atod(*Idle1MinString);
		NewRecord.Idle5Min = FCString::Atod(*Idle5MinString);
		NewRecord.Idle30Min = FCString::Atod(*Idle30MinString);
		NewRecord.AverageFPS = FCString::Atof(*AverageFPSString);
		NewRecord.CurrentUserActivity = UserActivityString;
		NewRecord.bCrashed = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsCrashStoreKey);
		NewRecord.bGPUCrashed = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsGPUCrashStoreKey);
		NewRecord.bIsDebugger = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsDebuggerStoreKey);
		NewRecord.bWasEverDebugger = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::WasDebuggerStoreKey);
		NewRecord.bIsVanilla = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsVanillaStoreKey);
		NewRecord.bIsTerminating = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsTerminatingKey);
		NewRecord.bWasShutdown = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::WasShutdownStoreKey);
		NewRecord.bIsInPIE = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsInPIEStoreKey);
		NewRecord.bIsInEnterprise = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsInEnterpriseStoreKey);
		NewRecord.bIsInVRMode = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsInVRModeStoreKey);
		NewRecord.bLowDriveSpaceDetected = EditorSessionSummaryUtils::GetStoredBool(SectionName, SessionSummaryDefs::IsLowDriveSpaceDetectedStoreKey);

		PluginsString.ParseIntoArray(NewRecord.Plugins, TEXT(","));

		Records.Add(NewRecord);
	}

	return MoveTemp(Records);
}

void FEditorSessionSummarySender::SendSessionSummaryEvent(const FEditorSessionRecord& Record) const
{
	FGuid SessionId;
	FString SessionIdString = Record.SessionId;
	if (FGuid::Parse(SessionIdString, SessionId))
	{
		// convert session guid to one with braces for sending to analytics
		SessionIdString = SessionId.ToString(EGuidFormats::DigitsWithHyphensInBraces);
	}

	FString ShutdownTypeString = Record.bCrashed ? SessionSummaryDefs::CrashSessionToken :
		(Record.bWasEverDebugger ? SessionSummaryDefs::DebuggerSessionToken :
		(Record.bIsTerminating ? SessionSummaryDefs::TerminatedSessionToken :
		(Record.bWasShutdown ? SessionSummaryDefs::ShutdownSessionToken : SessionSummaryDefs::AbnormalSessionToken)));

	FString PluginsString = FString::Join(Record.Plugins, TEXT(","));

	TArray< FAnalyticsEventAttribute > AnalyticsAttributes;
	AnalyticsAttributes.Emplace(TEXT("ProjectName"), Record.ProjectName);
	AnalyticsAttributes.Emplace(TEXT("Platform"), FPlatformProperties::PlatformName());
	AnalyticsAttributes.Emplace(TEXT("SessionId"), SessionIdString);
	AnalyticsAttributes.Emplace(TEXT("EngineVersion"), Record.EngineVersion);
	AnalyticsAttributes.Emplace(TEXT("ShutdownType"), ShutdownTypeString);
	AnalyticsAttributes.Emplace(TEXT("Timestamp"), Record.Timestamp.ToIso8601());
	AnalyticsAttributes.Emplace(TEXT("CurrentUserActivity"), Record.CurrentUserActivity);
	AnalyticsAttributes.Emplace(TEXT("IsVanilla"), Record.bIsVanilla);
	AnalyticsAttributes.Emplace(TEXT("WasDebugged"), Record.bWasEverDebugger);
	AnalyticsAttributes.Emplace(TEXT("GPUCrash"), Record.bGPUCrashed);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::WasShutdownStoreKey, Record.bWasShutdown);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::StartupTimestampStoreKey, Record.StartupTimestamp.ToIso8601());
	AnalyticsAttributes.Emplace(SessionSummaryDefs::AverageFPSStoreKey, Record.AverageFPS);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::IsInPIEStoreKey, Record.bIsInPIE);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::IsInEnterpriseStoreKey, Record.bIsInEnterprise);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::IsInVRModeStoreKey, Record.bIsInVRMode);
	AnalyticsAttributes.Emplace(SessionSummaryDefs::IsLowDriveSpaceDetectedStoreKey, Record.bLowDriveSpaceDetected);

	double SessionDuration = (Record.Timestamp - Record.StartupTimestamp).GetTotalSeconds();
	AnalyticsAttributes.Emplace(SessionSummaryDefs::SessionDurationStoreKey, SessionDuration);

	AnalyticsAttributes.Emplace(TEXT("1MinIdle"), Record.Idle1Min);
	AnalyticsAttributes.Emplace(TEXT("5MinIdle"), Record.Idle5Min);
	AnalyticsAttributes.Emplace(TEXT("30MinIdle"), Record.Idle30Min);

	// Add project info whether we are in editor or game.
	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();
	FString OSMajor;
	FString OSMinor;
	FPlatformMisc::GetOSVersions(/*out*/ OSMajor, /*out*/ OSMinor);

	FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	AnalyticsAttributes.Emplace(TEXT("ProjectName"), ProjectSettings.ProjectName);
	AnalyticsAttributes.Emplace(TEXT("ProjectID"), ProjectSettings.ProjectID);
	AnalyticsAttributes.Emplace(TEXT("ProjectDescription"), ProjectSettings.Description);
	AnalyticsAttributes.Emplace(TEXT("ProjectVersion"), ProjectSettings.ProjectVersion);
	AnalyticsAttributes.Emplace(TEXT("GPUVendorID"), GRHIVendorId);
	AnalyticsAttributes.Emplace(TEXT("GPUDeviceID"), GRHIDeviceId);
	AnalyticsAttributes.Emplace(TEXT("GRHIDeviceRevision"), GRHIDeviceRevision);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterInternalDriverVersion"), GRHIAdapterInternalDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterUserDriverVersion"), GRHIAdapterUserDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("TotalPhysicalRAM"), Stats.TotalPhysical);
	AnalyticsAttributes.Emplace(TEXT("CPUPhysicalCores"), FPlatformMisc::NumberOfCores());
	AnalyticsAttributes.Emplace(TEXT("CPULogicalCores"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	AnalyticsAttributes.Emplace(TEXT("DesktopGPUAdapter"), FPlatformMisc::GetPrimaryGPUBrand());
	AnalyticsAttributes.Emplace(TEXT("RenderingGPUAdapter"), GRHIAdapterName);
	AnalyticsAttributes.Emplace(TEXT("CPUVendor"), FPlatformMisc::GetCPUVendor());
	AnalyticsAttributes.Emplace(TEXT("CPUBrand"), FPlatformMisc::GetCPUBrand());
	AnalyticsAttributes.Emplace(TEXT("OSMajor"), OSMajor);
	AnalyticsAttributes.Emplace(TEXT("OSMinor"), OSMinor);
	AnalyticsAttributes.Emplace(TEXT("OSVersion"), FPlatformMisc::GetOSVersion());
	AnalyticsAttributes.Emplace(TEXT("Is64BitOS"), FPlatformMisc::Is64bitOperatingSystem());

	FEngineAnalytics::GetProvider().RecordEvent(TEXT("SessionSummary"), AnalyticsAttributes);

	UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummary sent report. Type=%s, SessionId=%s"), *ShutdownTypeString, *SessionIdString);
}

#undef LOCTEXT_NAMESPACE
