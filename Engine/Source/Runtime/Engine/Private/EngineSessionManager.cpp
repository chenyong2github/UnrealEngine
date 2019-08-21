// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EngineSessionManager.h"
#include "Misc/Guid.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProcess.h"
#include "EngineAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "GeneralProjectSettings.h"
#include "UserActivityTracking.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "Misc/EngineBuildSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformOutputDevices.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/EngineVersion.h"
#include "RHI.h"

#if WITH_EDITOR
#include "IVREditorModule.h"
#include "Kismet2/DebuggerCommands.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "SessionManager"

DEFINE_LOG_CATEGORY(LogEngineSessionManager);

namespace SessionManagerDefs
{
	static const FTimespan SessionRecordExpiration = FTimespan::FromDays(30.0);
	static const FTimespan SessionRecordTimeout = FTimespan::FromMinutes(3.0);
	static const FTimespan GlobalLockWaitTimeout = FTimespan::FromSeconds(0.5);
	static const int HeartbeatPeriodSeconds(60);
	static const FString DefaultUserActivity(TEXT("Unknown"));
	static const FString StoreId(TEXT("Epic Games"));
	static const FString RunningSessionToken(TEXT("Running"));
	static const FString ShutdownSessionToken(TEXT("Shutdown"));
	static const FString CrashSessionToken(TEXT("Crashed"));
	static const FString TerminatedSessionToken(TEXT("Terminated"));
	static const FString DebuggerSessionToken(TEXT("Debugger"));
	static const FString AbnormalSessionToken(TEXT("AbnormalShutdown"));
	static const FString PS4SessionToken(TEXT("AbnormalShutdownPS4"));
	static const FString SessionRecordListSection(TEXT("List"));
	static const FString EditorSessionRecordSectionPrefix(TEXT("Unreal Engine/Editor Sessions/"));
	static const FString GameSessionRecordSectionPrefix(TEXT("Unreal Engine/Game Sessions/"));
	static const FString WatchdogRecordSectionPrefix(TEXT("Unreal Engine/Watchdog/"));
	static const FString SessionsVersionString(TEXT("1_3"));
	static const FString WatchdogVersionString(TEXT("1_0"));
	static const FString ModeStoreKey(TEXT("Mode"));
	static const FString ProjectNameStoreKey(TEXT("ProjectName"));
	static const FString CommandLineStoreKey(TEXT("CommandLine"));
	static const FString CrashStoreKey(TEXT("IsCrash"));
	static const FString GPUCrashStoreKey(TEXT("IsGPUCrash"));
	static const FString DeactivatedStoreKey(TEXT("IsDeactivated"));
	static const FString BackgroundStoreKey(TEXT("IsInBackground"));
	static const FString TerminatingKey(TEXT("Terminating"));
	static const FString PlatformProcessIDKey(TEXT("PlatformProcessID"));
	static const FString EngineVersionStoreKey(TEXT("EngineVersion"));
	static const FString TimestampStoreKey(TEXT("Timestamp"));
	static const FString StartupTimestampStoreKey(TEXT("StartupTimestamp"));
	static const FString SessionDurationStoreKey(TEXT("SessionDuration"));
	static const FString Idle1MinStoreKey(TEXT("Idle1Min"));
	static const FString Idle5MinStoreKey(TEXT("Idle5Min"));
	static const FString Idle30MinStoreKey(TEXT("Idle30Min"));
	static const FString SessionIdStoreKey(TEXT("SessionId"));
	static const FString StatusStoreKey(TEXT("LastExecutionState"));
	static const FString DebuggerStoreKey(TEXT("IsDebugger"));
	static const FString WasDebuggerStoreKey(TEXT("WasEverDebugger"));
	static const FString UserActivityStoreKey(TEXT("CurrentUserActivity"));
	static const FString VanillaStoreKey(TEXT("IsVanilla"));
	static const FString GlobalLockName(TEXT("UE4_SessionManager_Lock"));
	static const FString FalseValueString(TEXT("0"));
	static const FString TrueValueString(TEXT("1"));
	static const FString EditorValueString(TEXT("Editor"));
	static const FString GameValueString(TEXT("Game"));
	static const FString UnknownProjectValueString(TEXT("UnknownProject"));
	static const FString PluginsStoreKey(TEXT("Plugins"));
	static const FString WasShutdownStoreKey(TEXT("WasShutdown"));
	static const FString AverageFPSStoreKey(TEXT("AverageFPS"));
	static const FString IsInVRModeStoreKey(TEXT("IsInVRMode"));
	static const FString IsInEnterpriseStoreKey(TEXT("IsInEnterprise"));
	static const FString IsInPIEStoreKey(TEXT("IsInPIE"));
}

namespace
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
		return bInValue ? SessionManagerDefs::TrueValueString : SessionManagerDefs::FalseValueString;
	}

	bool GetStoredBool(const FString& SectionName, const FString& StoredKey)
	{
		FString StoredString = SessionManagerDefs::FalseValueString;
		FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, StoredKey, StoredString);

		return StoredString == SessionManagerDefs::TrueValueString;
	}
}

/* FEngineSessionManager */

void FEngineSessionManager::Initialize()
{
	// Register for crash and app state callbacks
	FCoreDelegates::OnHandleSystemError.AddRaw(this, &FEngineSessionManager::OnCrashing);
	FCoreDelegates::ApplicationHasReactivatedDelegate.AddRaw(this, &FEngineSessionManager::OnAppReactivate);
	FCoreDelegates::ApplicationWillDeactivateDelegate.AddRaw(this, &FEngineSessionManager::OnAppDeactivate);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FEngineSessionManager::OnAppBackground);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FEngineSessionManager::OnAppForeground);
	FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FEngineSessionManager::OnTerminate);
	FUserActivityTracking::OnActivityChanged.AddRaw(this, &FEngineSessionManager::OnUserActivity);
	FCoreDelegates::IsVanillaProductChanged.AddRaw(this, &FEngineSessionManager::OnVanillaStateChanged);
	FSlateApplication::Get().GetOnModalLoopTickEvent().AddRaw(this, &FEngineSessionManager::Tick);
	
	const bool bFirstInitAttempt = true;
	InitializeRecords(bFirstInitAttempt);
}

void FEngineSessionManager::InitializeRecords(bool bFirstAttempt)
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}

	TArray<FSessionRecord> SessionRecordsToReport;

	{
		// Scoped lock
		FSystemWideCriticalSection StoredValuesLock(SessionManagerDefs::GlobalLockName, bFirstAttempt ? SessionManagerDefs::GlobalLockWaitTimeout : FTimespan::Zero());

		// Get list of sessions in storage
		if (StoredValuesLock.IsValid() && BeginReadWriteRecords())
		{
			UE_LOG(LogEngineSessionManager, Verbose, TEXT("Initializing EngineSessionManager for abnormal shutdown tracking"));

			TArray<FSessionRecord> SessionRecordsToDelete;

			// Attempt check each stored session
			for (FSessionRecord& Record : SessionRecords)
			{
				FTimespan RecordAge = FDateTime::UtcNow() - Record.Timestamp;

				if (Record.bCrashed || Record.bIsTerminating || Record.bWasShutdown)
				{
					// Crashed / terminated sessions
					SessionRecordsToReport.Add(Record);
					SessionRecordsToDelete.Add(Record);
				}
				else if (RecordAge > SessionManagerDefs::SessionRecordExpiration)
				{
					// Delete expired session records
					SessionRecordsToDelete.Add(Record);
				}
				else if (RecordAge > SessionManagerDefs::SessionRecordTimeout)
				{
					// Timed out sessions
					SessionRecordsToReport.Add(Record);
					SessionRecordsToDelete.Add(Record);
				}
			}

			for (FSessionRecord& DeletingRecord : SessionRecordsToDelete)
			{
				DeleteStoredRecord(DeletingRecord);
			}

			// Create a session record for this session
			CreateAndWriteRecordForSession();

			// Update and release list of sessions in storage
			EndReadWriteRecords();

			bInitializedRecords = true;

			UE_LOG(LogEngineSessionManager, Log, TEXT("EngineSessionManager initialized"));
		}
	}

	for (FSessionRecord& SessionRecord : SessionRecordsToReport)
	{
		SendSessionRecordEvent(TEXT("SessionSummary"), SessionRecord, true);

		if (!SessionRecord.bWasShutdown)
		{
			// Send error report for session that timed out or crashed
			SendAbnormalShutdownReport(SessionRecord);
		}
	}
}

void FEngineSessionManager::Tick(float DeltaTime)
{
	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > (float)SessionManagerDefs::HeartbeatPeriodSeconds && !bShutdown)
	{
		HeartbeatTimeElapsed = 0.0f;

		if (!bInitializedRecords)
		{
			// Try late initialization
			const bool bFirstInitAttempt = false;
			InitializeRecords(bFirstInitAttempt);
		}

		// Update timestamp in the session record for this session 
		if (bInitializedRecords)
		{	
			bool bIsDebuggerPresent = FPlatformMisc::IsDebuggerPresent();
			if (CurrentSession.bIsDebugger != bIsDebuggerPresent)
			{
				CurrentSession.bIsDebugger = bIsDebuggerPresent;
			
				FString IsDebuggerString = BoolToStoredString(CurrentSession.bIsDebugger);
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::DebuggerStoreKey, IsDebuggerString);

				if (!CurrentSession.bWasEverDebugger && CurrentSession.bIsDebugger)
				{
					CurrentSession.bWasEverDebugger = true;

					FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::WasDebuggerStoreKey, SessionManagerDefs::TrueValueString);

#if PLATFORM_SUPPORTS_WATCHDOG
					if (!WatchdogSectionName.IsEmpty())
					{
						FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::WasDebuggerStoreKey, SessionManagerDefs::TrueValueString);
					}
#endif
				}
			}

			const FString TimestampString = TimestampToString(FDateTime::UtcNow());
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::TimestampStoreKey, TimestampString);

			const float IdleSeconds = FPlatformTime::Seconds() - FSlateApplication::Get().GetLastUserInteractionTime();

			// 1 + 1 minutes
			if (IdleSeconds > (60 + 60))
			{
				CurrentSession.Idle1Min += 1;
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle1MinStoreKey, FString::FromInt(CurrentSession.Idle1Min));
			}

			// 5 + 1 minutes
			if (IdleSeconds > (5 * 60 + 60))
			{
				CurrentSession.Idle5Min += 1;
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle5MinStoreKey, FString::FromInt(CurrentSession.Idle5Min));
			}

			// 30 + 1 minutes
			if (IdleSeconds > (30 * 60 + 60))
			{
				CurrentSession.Idle30Min += 1;
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle30MinStoreKey, FString::FromInt(CurrentSession.Idle30Min));
			}


#if PLATFORM_SUPPORTS_WATCHDOG
			if (!WatchdogSectionName.IsEmpty())
			{
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, TimestampString);
			}
#endif

#if WITH_EDITOR
			extern ENGINE_API float GAverageFPS;

			CurrentSession.AverageFPS = GAverageFPS;
			const FString AverageFPSString = FString::SanitizeFloat(GAverageFPS);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::AverageFPSStoreKey, AverageFPSString);

			CurrentSession.bIsInVRMode = IVREditorModule::Get().IsVREditorModeActive();
			const FString IsInVRModeString = BoolToStoredString(CurrentSession.bIsInVRMode);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInVRModeStoreKey, IsInVRModeString);

			CurrentSession.bIsInEnterprise = IProjectManager::Get().IsEnterpriseProject();
			const FString IsInEnterpriseString = BoolToStoredString(CurrentSession.bIsInEnterprise);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInEnterpriseStoreKey, IsInEnterpriseString);

			CurrentSession.bIsInPIE = FPlayWorldCommandCallbacks::IsInPIE();
			const FString IsInPIEString = BoolToStoredString(CurrentSession.bIsInPIE);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInPIEStoreKey, IsInPIEString);
#endif
		}
	}
}

void FEngineSessionManager::Shutdown()
{
	FCoreDelegates::OnHandleSystemError.RemoveAll(this);
	FCoreDelegates::ApplicationHasReactivatedDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);
	FCoreDelegates::IsVanillaProductChanged.RemoveAll(this);

	if (!CurrentSession.bIsTerminating) // Skip Slate if terminating, since we can't guarantee which thread called us.
	{
		FSlateApplication::Get().GetOnModalLoopTickEvent().RemoveAll(this);
	}

	// Clear the session record for this session
	if (bInitializedRecords)
	{
		if (!CurrentSession.bIsTerminating)
		{
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::WasShutdownStoreKey, SessionManagerDefs::TrueValueString);
		}

		if (!CurrentSession.bCrashed)
		{
#if PLATFORM_SUPPORTS_WATCHDOG
			if (!WatchdogSectionName.IsEmpty())
			{
				const FString& ShutdownType = CurrentSession.bIsTerminating ? SessionManagerDefs::TerminatedSessionToken : SessionManagerDefs::ShutdownSessionToken;
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::StatusStoreKey, ShutdownType);
				FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, TimestampToString(FDateTime::UtcNow()));
				WatchdogSectionName.Empty();
			}
#endif
		}

		bInitializedRecords = false;
		bShutdown = true;
	}
}

bool FEngineSessionManager::BeginReadWriteRecords()
{
	SessionRecords.Empty();

	// Lock and read the list of sessions in storage
	FString ListSectionName = GetStoreSectionString(SessionManagerDefs::SessionRecordListSection);

	// Write list to SessionRecords member
	FString SessionListString;
	FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, ListSectionName, TEXT("SessionList"), SessionListString);

	// Parse SessionListString for session ids
	TArray<FString> SessionIds;
	SessionListString.ParseIntoArray(SessionIds, TEXT(","));

	// Retrieve all the sessions in the list from storage
	for (const FString& SessionId : SessionIds)
	{
		FString SectionName = GetStoreSectionString(SessionId);

		FString IsCrashString;
		FString EngineVersionString;
		FString TimestampString;
		FString IsDebuggerString;

		// Read mandatory values
		if (FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::CrashStoreKey, IsCrashString) &&
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::EngineVersionStoreKey, EngineVersionString) &&
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::TimestampStoreKey, TimestampString) &&
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::DebuggerStoreKey, IsDebuggerString))
		{
			// If the process is still running we don't need to report it.
			FString PlatformProcessIDString;
			if (FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::PlatformProcessIDKey, PlatformProcessIDString))
			{
				const uint32 ProcId = (uint32)FCString::Atoi(*PlatformProcessIDString);
				FProcHandle Handle = FPlatformProcess::OpenProcess(ProcId);
				if (Handle.IsValid())
				{
					const bool bIsRunning = FPlatformProcess::IsProcRunning(Handle);
					FPlatformProcess::CloseProc(Handle);
					if (bIsRunning)
					{
						continue;
					}
				}
			}

			// Read optional values
			FString ModeString = SessionManagerDefs::EditorValueString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::ModeStoreKey, ModeString);
			FString ProjectName = SessionManagerDefs::UnknownProjectValueString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::ProjectNameStoreKey, ProjectName);
			FString UserActivityString = SessionManagerDefs::DefaultUserActivity;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::UserActivityStoreKey, UserActivityString);
			FString StartupTimestampString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::StartupTimestampStoreKey, StartupTimestampString);
			FString Idle1MinString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle1MinStoreKey, Idle1MinString);
			FString Idle5MinString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle5MinStoreKey, Idle5MinString);
			FString Idle30MinString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle30MinStoreKey, Idle30MinString);
			FString PluginsString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::PluginsStoreKey, PluginsString);
			FString AverageFPSString;
			FPlatformMisc::GetStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::AverageFPSStoreKey, AverageFPSString);

			// Create new record from the read values
			FSessionRecord NewRecord;
			NewRecord.SessionId = SessionId;
			NewRecord.Mode = ModeString == SessionManagerDefs::EditorValueString ? EEngineSessionManagerMode::Editor : EEngineSessionManagerMode::Game;
			NewRecord.ProjectName = ProjectName;
			NewRecord.EngineVersion = EngineVersionString;
			NewRecord.StartupTimestamp = StringToTimestamp(StartupTimestampString);
			NewRecord.Timestamp = StringToTimestamp(TimestampString);
			NewRecord.Idle1Min = FCString::Atod(*Idle1MinString);
			NewRecord.Idle5Min = FCString::Atod(*Idle5MinString);
			NewRecord.Idle30Min = FCString::Atod(*Idle30MinString);
			NewRecord.AverageFPS = FCString::Atof(*AverageFPSString);
			NewRecord.CurrentUserActivity = UserActivityString;
			NewRecord.bCrashed = IsCrashString == SessionManagerDefs::TrueValueString;
			NewRecord.bGPUCrashed = GetStoredBool(SectionName, SessionManagerDefs::GPUCrashStoreKey);
			NewRecord.bIsDebugger = IsDebuggerString == SessionManagerDefs::TrueValueString;
			NewRecord.bWasEverDebugger = GetStoredBool(SectionName, SessionManagerDefs::WasDebuggerStoreKey);
			NewRecord.bIsDeactivated = GetStoredBool(SectionName, SessionManagerDefs::DeactivatedStoreKey);
			NewRecord.bIsInBackground = GetStoredBool(SectionName, SessionManagerDefs::BackgroundStoreKey);
			NewRecord.bIsVanilla = GetStoredBool(SectionName, SessionManagerDefs::VanillaStoreKey);
			NewRecord.bIsTerminating = GetStoredBool(SectionName, SessionManagerDefs::TerminatingKey);
			NewRecord.bWasShutdown = GetStoredBool(SectionName, SessionManagerDefs::WasShutdownStoreKey);
			NewRecord.bIsInPIE = GetStoredBool(SectionName, SessionManagerDefs::IsInPIEStoreKey);
			NewRecord.bIsInVRMode = GetStoredBool(SectionName, SessionManagerDefs::IsInVRModeStoreKey);
			NewRecord.bIsInEnterprise = GetStoredBool(SectionName, SessionManagerDefs::IsInEnterpriseStoreKey);

			PluginsString.ParseIntoArray(NewRecord.Plugins, TEXT(","));

			SessionRecords.Add(NewRecord);
		}
		else
		{
			// Clean up orphaned values, if there are any
			DeleteStoredRecordValues(SectionName);
		}
	}

	return true;
}

void FEngineSessionManager::EndReadWriteRecords()
{
	// Update the list of sessions in storage to match SessionRecords
	FString SessionListString;
	if (SessionRecords.Num() > 0)
	{
		for (FSessionRecord& Session : SessionRecords)
		{
			SessionListString.Append(Session.SessionId);
			SessionListString.Append(TEXT(","));
		}
		SessionListString.RemoveAt(SessionListString.Len() - 1);
	}

	FString ListSectionName = GetStoreSectionString(SessionManagerDefs::SessionRecordListSection);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, ListSectionName, TEXT("SessionList"), SessionListString);

	// Clear SessionRecords member
	SessionRecords.Empty();
}

void FEngineSessionManager::DeleteStoredRecord(const FSessionRecord& Record)
{
	// Delete the session record in storage
	FString SessionId = Record.SessionId;
	FString SectionName = GetStoreSectionString(SessionId);

	DeleteStoredRecordValues(SectionName);

	// Remove the session record from SessionRecords list
	SessionRecords.RemoveAll([&SessionId](const FSessionRecord& X){ return X.SessionId == SessionId; });
}

void FEngineSessionManager::DeleteStoredRecordValues(const FString& SectionName) const
{
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::ModeStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::ProjectNameStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::CrashStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::GPUCrashStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::EngineVersionStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::StartupTimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::TimestampStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle1MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle5MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::Idle30MinStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::DebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::WasDebuggerStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::WasShutdownStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::DeactivatedStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::BackgroundStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::UserActivityStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::VanillaStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::TerminatingKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::PlatformProcessIDKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::PluginsStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::AverageFPSStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::IsInPIEStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::IsInEnterpriseStoreKey);
	FPlatformMisc::DeleteStoredValue(SessionManagerDefs::StoreId, SectionName, SessionManagerDefs::IsInVRModeStoreKey);
}

void FEngineSessionManager::SendSessionRecordEvent(const FString& EventName, const FSessionRecord& Record, bool bSendHardwareDetails)
{
	FGuid SessionId;
	FString SessionIdString = Record.SessionId;
	if (FGuid::Parse(SessionIdString, SessionId))
	{
		// convert session guid to one with braces for sending to analytics
		SessionIdString = SessionId.ToString(EGuidFormats::DigitsWithHyphensInBraces);
	}

#if !PLATFORM_PS4
	FString ShutdownTypeString = Record.bCrashed ? SessionManagerDefs::CrashSessionToken :
		(Record.bWasEverDebugger ? SessionManagerDefs::DebuggerSessionToken :
		(Record.bIsTerminating ? SessionManagerDefs::TerminatedSessionToken : 
		(Record.bWasShutdown ? SessionManagerDefs::ShutdownSessionToken : SessionManagerDefs::AbnormalSessionToken)));
#else
	// PS4 cannot set the crash flag so report abnormal shutdowns with a specific token meaning "crash or abnormal shutdown".
	FString ShutdownTypeString = Record.bWasEverDebugger ? SessionManagerDefs::DebuggerSessionToken : SessionManagerDefs::PS4SessionToken;
#endif

	const FString& RunTypeString = Record.Mode == EEngineSessionManagerMode::Editor ? SessionManagerDefs::EditorValueString : SessionManagerDefs::GameValueString;
	FString PluginsString = FString::Join(Record.Plugins, TEXT(","));

	TArray< FAnalyticsEventAttribute > AnalyticsAttributes;
	AnalyticsAttributes.Emplace(TEXT("RunType"), RunTypeString);
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
	AnalyticsAttributes.Emplace(SessionManagerDefs::WasShutdownStoreKey, Record.bWasShutdown);
	AnalyticsAttributes.Emplace(SessionManagerDefs::StartupTimestampStoreKey, Record.StartupTimestamp.ToIso8601());
	AnalyticsAttributes.Emplace(SessionManagerDefs::AverageFPSStoreKey, Record.AverageFPS);
	AnalyticsAttributes.Emplace(SessionManagerDefs::IsInPIEStoreKey, Record.bIsInPIE);
	AnalyticsAttributes.Emplace(SessionManagerDefs::IsInEnterpriseStoreKey, Record.bIsInEnterprise);
	AnalyticsAttributes.Emplace(SessionManagerDefs::IsInVRModeStoreKey, Record.bIsInVRMode);

	double SessionDuration = (Record.Timestamp - Record.StartupTimestamp).GetTotalSeconds();
	AnalyticsAttributes.Emplace(SessionManagerDefs::SessionDurationStoreKey, SessionDuration);

	AnalyticsAttributes.Emplace(TEXT("1MinIdle"), Record.Idle1Min);
	AnalyticsAttributes.Emplace(TEXT("5MinIdle"), Record.Idle5Min);
	AnalyticsAttributes.Emplace(TEXT("30MinIdle"), Record.Idle30Min);

	if (bSendHardwareDetails)
	{
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
	}

	FEngineAnalytics::GetProvider().RecordEvent(EventName, AnalyticsAttributes);

	UE_LOG(LogEngineSessionManager, Log, TEXT("EngineSessionManager sent report. Event=%s, Type=%s, SessionId=%s"), *EventName, *ShutdownTypeString, *SessionIdString);
}

/**
 * @EventName Engine.AbnormalShutdown
 *
 * @Trigger Fired only by the engine during startup, once for each "abnormal shutdown" detected that has not already been sent.
 *
 * @Type Client
 * @Owner Chris.Wood
 *
 * @EventParam RunType - Editor or Game
 * @EventParam ProjectName - Project for the session that abnormally terminated. 
 * @EventParam Platform - Windows, Mac, Linux, PS4, XBoxOne or Unknown
 * @EventParam SessionId - Analytics SessionID of the session that abnormally terminated.
 * @EventParam EngineVersion - EngineVersion of the session that abnormally terminated.
 * @EventParam ShutdownType - one of Crashed, Debugger, or AbormalShutdown
 *               * Crashed - we definitely detected a crash (whether or not a debugger was attached)
 *               * Terminated - the application was terminated from within or by the OS.
 *               * Debugger - the session crashed or shutdown abnormally, but we had a debugger attached at startup, so abnormal termination is much more likely because the user was debugging.
 *               * AbnormalShutdown - this happens when we didn't detect a normal shutdown, but none of the above cases is the cause. A session record simply timed-out without being closed.
 * @EventParam Timestamp - the UTC time of the last known time the abnormally terminated session was running, within 5 minutes.
 * @EventParam CurrentUserActivity - If one was set when the session abnormally terminated, this is the activity taken from the FUserActivityTracking API.
 * @EventParam IsVanilla - Value from the engine's IsVanillaProduct() method. Basically if this is a Epic-distributed Editor with zero third party plugins or game code modules.
 * @EventParam WasDebugged - True if this session was attached to debugger at any time.
 * @EventParam GPUCrash - A GPU Hang or Crash was detected before the final assert, fatal log, or other exit.
 *
 * @TODO: Debugger should be a completely separate flag, since it's orthogonal to whether we detect a crash or shutdown.
 *
 * @Comments The engine will only try to check for abnormal terminations if it determines it is a "real" editor or game run (not a commandlet or PIE, or editor -game run), and the user has not disabled sending usage data to Epic via the settings.
 * 
 * The SessionId parameter should be used to find the actual session associated with this crash.
 * 
 * If multiple versions of the editor or launched, this code will properly track each one and its shutdown status. So during startup, an editor instance may need to fire off several events.
 *
 * When attributing abnormal terminations to engine versions, be sure to use the EngineVersion associated with this event, and not the AppVersion. AppVersion is for the session that is currently sending the event, not for the session that crashed. That is why EngineVersion is sent separately.
 *
 * The editor updates Timestamp every 5 minutes, so we should know the time of the crash within 5 minutes. It should technically correlate with the last heartbeat we receive in the data for that session.
 *
 * The main difference between an AbnormalShutdown and a Crash is that we KNOW a crash occurred, so we can send the event right away. If the engine did not shut down correctly, we don't KNOW that, so simply wait up to 30m (the engine updates the timestamp every 5 mins) to be sure that it's probably not running anymore.
 *
 * We have seen data in the wild that indicated editor freezing for up to 8 days but we're assuming that was likely stopped in a debugger. That's also why we added the ShutdownType of Debugger to the event. However, this code does not check IMMEDIATELY on crash if the debugger is present (that might be dangerous in a crash handler perhaps), we only check if a debugger is attached at startup. Then if an A.S. is detected, we just say "Debugger" because it's likely they just stopped the debugger and killed the process.
 */
void FEngineSessionManager::SendAbnormalShutdownReport(const FSessionRecord& Record)
{
#if PLATFORM_WINDOWS | PLATFORM_MAC | PLATFORM_UNIX
	// do nothing
#elif PLATFORM_PS4
	if (Record.bIsDeactivated && !Record.bCrashed)
	{
		// Shutting down in deactivated state on PS4 is normal - don't report it
		return;
	}
#elif PLATFORM_XBOXONE
	if (Record.bIsInBackground && !Record.bCrashed)
	{
		// Shutting down in background state on XB1 is normal - don't report it
		return;
	}
#else
	return; // TODO: CWood: disabled on other platforms
#endif

	SendSessionRecordEvent(TEXT("Engine.AbnormalShutdown"), Record, false);
}

void FEngineSessionManager::CreateAndWriteRecordForSession()
{
	FGuid SessionId;
	if (FGuid::Parse(FEngineAnalytics::GetProvider().GetSessionID(), SessionId))
	{
		// convert session guid to one without braces or other chars that might not be suitable for storage
		CurrentSession.SessionId = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
	}
	else
	{
		CurrentSession.SessionId = FEngineAnalytics::GetProvider().GetSessionID();
	}

	const uint32 ProcId = FPlatformProcess::GetCurrentProcessId();
	FString CurrentProcessIDString = FString::FromInt(ProcId);

	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();

	CurrentSession.Mode = Mode;
	CurrentSession.ProjectName = ProjectSettings.ProjectName;
	CurrentSession.EngineVersion = FEngineVersion::Current().ToString(EVersionComponent::Changelist);
	CurrentSession.Timestamp = FDateTime::UtcNow();
	CurrentSession.bIsDebugger = CurrentSession.bWasEverDebugger = FPlatformMisc::IsDebuggerPresent();
	CurrentSession.CurrentUserActivity = GetUserActivityString();
	CurrentSession.bIsVanilla = GEngine && GEngine->IsVanillaProduct();

	CurrentSessionSectionName = GetStoreSectionString(CurrentSession.SessionId);

	TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : Plugins)
	{
		CurrentSession.Plugins.Add(Plugin->GetName());
	}

	CurrentSession.Plugins.Sort();

	FString ModeString = CurrentSession.Mode == EEngineSessionManagerMode::Editor ? SessionManagerDefs::EditorValueString : SessionManagerDefs::GameValueString;
	FString IsDebuggerString = BoolToStoredString(CurrentSession.bIsDebugger);
	FString WasDebuggerString = BoolToStoredString(CurrentSession.bWasEverDebugger);
	FString IsDeactivatedString = BoolToStoredString(CurrentSession.bIsDeactivated);
	FString IsInBackgroundString = BoolToStoredString(CurrentSession.bIsInBackground);
	FString IsVanillaString = BoolToStoredString(CurrentSession.bIsVanilla);
	FString IsTerminatingString = BoolToStoredString(CurrentSession.bIsTerminating);
	FString CurrentTimestampString = TimestampToString(CurrentSession.Timestamp);
	FString Idle1MinString = FString::FromInt(CurrentSession.Idle1Min);
	FString Idle5MinString = FString::FromInt(CurrentSession.Idle5Min);
	FString Idle30MinString = FString::FromInt(CurrentSession.Idle30Min);
	FString AverageFPSString = FString::SanitizeFloat(CurrentSession.AverageFPS);
	FString PluginsString = FString::Join(CurrentSession.Plugins, TEXT(","));
	FString WasShutdownString = BoolToStoredString(CurrentSession.bWasShutdown);

	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::ModeStoreKey, ModeString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::ProjectNameStoreKey, CurrentSession.ProjectName);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::CrashStoreKey, SessionManagerDefs::FalseValueString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::EngineVersionStoreKey, CurrentSession.EngineVersion);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::StartupTimestampStoreKey, CurrentTimestampString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::TimestampStoreKey, CurrentTimestampString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle1MinStoreKey, Idle1MinString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle5MinStoreKey, Idle5MinString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::Idle30MinStoreKey, Idle30MinString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::DebuggerStoreKey, IsDebuggerString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::WasDebuggerStoreKey, WasDebuggerString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::DeactivatedStoreKey, IsDeactivatedString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::BackgroundStoreKey, IsInBackgroundString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::UserActivityStoreKey, CurrentSession.CurrentUserActivity);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::VanillaStoreKey, IsVanillaString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::TerminatingKey, IsTerminatingString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::WasShutdownStoreKey, WasShutdownString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::PlatformProcessIDKey, CurrentProcessIDString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::PluginsStoreKey, PluginsString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::AverageFPSStoreKey, AverageFPSString);
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInPIEStoreKey, BoolToStoredString(CurrentSession.bIsInPIE));
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInEnterpriseStoreKey, BoolToStoredString(CurrentSession.bIsInEnterprise));
	FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::IsInVRModeStoreKey, BoolToStoredString(CurrentSession.bIsInVRMode));

	SessionRecords.Add(CurrentSession);

#if PLATFORM_SUPPORTS_WATCHDOG
	bool bUseWatchdog = false;
	GConfig->GetBool(TEXT("EngineSessionManager"), TEXT("UseWatchdogMTBF"), bUseWatchdog, GEngineIni);
	if ((!CurrentSession.bWasEverDebugger && bUseWatchdog && !FParse::Param(FCommandLine::Get(), TEXT("NoWatchdog"))) || FParse::Param(FCommandLine::Get(), TEXT("ForceWatchdog")))
	{
		StartWatchdog(ModeString, CurrentSession.ProjectName, FPlatformProperties::PlatformName(), CurrentSession.SessionId, CurrentSession.EngineVersion);
	}
#endif
}

extern CORE_API bool GIsGPUCrashed;
void FEngineSessionManager::OnCrashing()
{
	if (!CurrentSession.bCrashed && bInitializedRecords)
	{
		CurrentSession.bCrashed = true;
		CurrentSession.bGPUCrashed = GIsGPUCrashed;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::CrashStoreKey, SessionManagerDefs::TrueValueString);
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::GPUCrashStoreKey, BoolToStoredString(CurrentSession.bGPUCrashed));

#if PLATFORM_SUPPORTS_WATCHDOG
		if (!WatchdogSectionName.IsEmpty())
		{
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::StatusStoreKey, SessionManagerDefs::CrashSessionToken);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, TimestampToString(FDateTime::UtcNow()));
		}
#endif
	}
}

void FEngineSessionManager::OnAppReactivate()
{
	if (CurrentSession.bIsDeactivated && bInitializedRecords)
	{
		CurrentSession.bIsDeactivated = false;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::DeactivatedStoreKey, SessionManagerDefs::FalseValueString);
	}
}

void FEngineSessionManager::OnAppDeactivate()
{
	if (!CurrentSession.bIsDeactivated && bInitializedRecords)
	{
		CurrentSession.bIsDeactivated = true;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::DeactivatedStoreKey, SessionManagerDefs::TrueValueString);
	}
}

void FEngineSessionManager::OnAppBackground()
{
	if (!CurrentSession.bIsInBackground && bInitializedRecords)
	{
		CurrentSession.bIsInBackground = true;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::BackgroundStoreKey, SessionManagerDefs::TrueValueString);
	}
}

void FEngineSessionManager::OnAppForeground()
{
	if (CurrentSession.bIsInBackground && bInitializedRecords)
	{
		CurrentSession.bIsInBackground = false;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::BackgroundStoreKey, SessionManagerDefs::FalseValueString);
	}
}

void FEngineSessionManager::OnTerminate()
{
	if (!CurrentSession.bIsTerminating && bInitializedRecords)
	{
		CurrentSession.bIsTerminating = true;
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::TerminatingKey, SessionManagerDefs::TrueValueString);

		if (GIsRequestingExit)
		{
			// Certain terminations are routine (such as closing a log window to quit the editor).
			// In these cases, shut down the engine session so it won't send an abnormal shutdown report.
			Shutdown();
		}
#if PLATFORM_SUPPORTS_WATCHDOG
		else if (!WatchdogSectionName.IsEmpty())
		{
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::StatusStoreKey, SessionManagerDefs::TerminatedSessionToken);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, TimestampToString(FDateTime::UtcNow()));
		}
#endif
	}
}

FString FEngineSessionManager::GetStoreSectionString(FString InSuffix)
{
	check(Mode == EEngineSessionManagerMode::Editor || Mode == EEngineSessionManagerMode::Game)

	if (Mode == EEngineSessionManagerMode::Editor)
	{
		return FString::Printf(TEXT("%s%s/%s"), *SessionManagerDefs::EditorSessionRecordSectionPrefix, *SessionManagerDefs::SessionsVersionString, *InSuffix);
	}
	else
	{
		const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();
		return FString::Printf(TEXT("%s%s/%s/%s"), *SessionManagerDefs::GameSessionRecordSectionPrefix, *SessionManagerDefs::SessionsVersionString, *ProjectSettings.ProjectName, *InSuffix);
	}
}

void FEngineSessionManager::OnVanillaStateChanged(bool bIsVanilla)
{
	if (CurrentSession.bIsVanilla != bIsVanilla && bInitializedRecords)
	{
		CurrentSession.bIsVanilla = bIsVanilla;
		FString IsVanillaString = BoolToStoredString(CurrentSession.bIsVanilla);
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::VanillaStoreKey, IsVanillaString);
	}
}

void FEngineSessionManager::OnUserActivity(const FUserActivity& UserActivity)
{
	if (!CurrentSession.bCrashed && bInitializedRecords)
	{
		CurrentSession.CurrentUserActivity = GetUserActivityString();
		FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, CurrentSessionSectionName, SessionManagerDefs::UserActivityStoreKey, CurrentSession.CurrentUserActivity);

#if PLATFORM_SUPPORTS_WATCHDOG
		if (!WatchdogSectionName.IsEmpty())
		{
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::UserActivityStoreKey, CurrentSession.CurrentUserActivity);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, TimestampToString(FDateTime::UtcNow()));
		}
#endif
	}
}

FString FEngineSessionManager::GetUserActivityString() const
{
	const FUserActivity& UserActivity = FUserActivityTracking::GetUserActivity();
	
	if (UserActivity.ActionName.IsEmpty())
	{
		return SessionManagerDefs::DefaultUserActivity;
	}

	return UserActivity.ActionName;
}

#if PLATFORM_SUPPORTS_WATCHDOG

/**
 * @EventName Engine.StartWatchdog
 *
 * @Trigger Event raised by EngineSessionManager as part of MTBF tracking. Records an attempt to start the UnrealWatchdog process.
 *
 * @Type Client
 * @Owner Chris.Wood
 *
 * @EventParam RunType - Editor or Game
 * @EventParam ProjectName - Project for the session.
 * @EventParam Platform - Windows, Mac, Linux
 * @EventParam SessionId - Analytics SessionID of the session.
 * @EventParam EngineVersion - EngineVersion of the session.
 * @EventParam IsInternalBuild - internal Epic build environment or not? Calls FEngineBuildSettings::IsInternalBuild(). Value is Yes or No.
 * @EventParam Outcome - Whether the watchdog was started successfully. One of Succeeded, CreateProcFailed or MissingBinaryFailed.
 *
 * @Comments Currently only runs Watchdog when MTBF is enabled, we aren't debugging, we're a DESKTOP platform and watchdog is specifically enabled via config or command line arg.
 */
void FEngineSessionManager::StartWatchdog(const FString& RunType, const FString& ProjectName, const FString& PlatformName, const FString& SessionId, const FString& EngineVersion)
{
	uint32 ProcessId =  FPlatformProcess::GetCurrentProcessId();
	const int SuccessfulRtnCode = 0;	// hardcode this for now, zero might not always be correct

	FString LogFilePath = FPaths::ConvertRelativePathToFull(FPlatformOutputDevices::GetAbsoluteLogFilename());

	FString WatchdogClientArguments =
		FString::Printf(TEXT(
			"-PID=%u -RunType=%s -ProjectName=\"%s\" -Platform=%s -SessionId=%s -EngineVersion=%s -SuccessfulRtnCode=%d -LogPath=\"%s\""),
			ProcessId, *RunType, *ProjectName, *PlatformName, *SessionId, *EngineVersion, SuccessfulRtnCode, *LogFilePath);

	bool bAllowWatchdogDetectHangs = false;
	GConfig->GetBool(TEXT("EngineSessionManager"), TEXT("AllowWatchdogDetectHangs"), bAllowWatchdogDetectHangs, GEngineIni);

	if (bAllowWatchdogDetectHangs)
	{
		int HangSeconds = 120;
		GConfig->GetInt(TEXT("EngineSessionManager"), TEXT("WatchdogMinimumHangSeconds"), HangSeconds, GEngineIni);

		WatchdogClientArguments.Append(FString::Printf(TEXT(" -DetectHangs -HangSeconds=%d"), HangSeconds));
	}

	if (FEngineBuildSettings::IsInternalBuild())
	{
		// Suppress the watchdog dialogs if this engine session should never show interactive UI
		if (!FApp::IsUnattended() && !IsRunningDedicatedServer() && FApp::CanEverRender())
		{
			// Only show watchdog dialogs if it's set in config
			bool bAllowWatchdogDialogs = false;
			GConfig->GetBool(TEXT("EngineSessionManager"), TEXT("AllowWatchdogDialogs"), bAllowWatchdogDialogs, GEngineIni);

			if (bAllowWatchdogDialogs)
			{
				WatchdogClientArguments.Append(TEXT(" -AllowDialogs"));
			}
		}
	}

	FString WatchdogPath = FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("UnrealWatchdog"), EBuildConfigurations::Development));

	TArray< FAnalyticsEventAttribute > WatchdogStartedAttributes;
	WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("RunType"), RunType));
	WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("ProjectName"), ProjectName));
	WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("Platform"), PlatformName));
	WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("SessionId"), SessionId));
	WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("IsInternalBuild"), FEngineBuildSettings::IsInternalBuild() ? TEXT("Yes") : TEXT("No")));

	if (FPaths::FileExists(WatchdogPath))
	{
		FProcHandle WatchdogProcessHandle = FPlatformProcess::CreateProc(*WatchdogPath, *WatchdogClientArguments, true, true, false, NULL, 0, NULL, NULL);

		if (WatchdogProcessHandle.IsValid())
		{
			FString WatchdogStartTimeString = TimestampToString(FDateTime::UtcNow());
			FString WasDebuggerString = BoolToStoredString(CurrentSession.bWasEverDebugger);

			WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("Outcome"), TEXT("Succeeded")));
			UE_LOG(LogEngineSessionManager, Log, TEXT("Started UnrealWatchdog for process id %u"), ProcessId);

			WatchdogSectionName = GetWatchdogStoreSectionString(ProcessId);

			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::CommandLineStoreKey, FCommandLine::GetOriginalForLogging());
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::StartupTimestampStoreKey, WatchdogStartTimeString);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::TimestampStoreKey, WatchdogStartTimeString);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::StatusStoreKey, SessionManagerDefs::RunningSessionToken);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::UserActivityStoreKey, CurrentSession.CurrentUserActivity);
			FPlatformMisc::SetStoredValue(SessionManagerDefs::StoreId, WatchdogSectionName, SessionManagerDefs::WasDebuggerStoreKey, WasDebuggerString);
		}
		else
		{
			WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("Outcome"), TEXT("CreateProcFailed")));
			UE_LOG(LogEngineSessionManager, Warning, TEXT("Unable to start UnrealWatchdog.exe. CreateProc failed."));
		}
	}
	else
	{
		WatchdogStartedAttributes.Add(FAnalyticsEventAttribute(TEXT("Outcome"), TEXT("MissingBinaryFailed")));
		UE_LOG(LogEngineSessionManager, Log, TEXT("Unable to start UnrealWatchdog.exe. File not found."));
	}

	FEngineAnalytics::GetProvider().RecordEvent(TEXT("Engine.StartWatchdog"), WatchdogStartedAttributes);
}

FString FEngineSessionManager::GetWatchdogStoreSectionString(uint32 InPID)
{
	return FString::Printf(TEXT("%s%s/%u"), *SessionManagerDefs::WatchdogRecordSectionPrefix, *SessionManagerDefs::WatchdogVersionString, InPID);
}

#endif // PLATFORM_SUPPORTS_WATCHDOG

#undef LOCTEXT_NAMESPACE
