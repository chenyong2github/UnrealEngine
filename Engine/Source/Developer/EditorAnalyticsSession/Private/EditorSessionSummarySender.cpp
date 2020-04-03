// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorSessionSummarySender.h"

#include "AnalyticsEventAttribute.h"
#include "Algo/Transform.h"
#include "IAnalyticsProviderET.h"
#include "EditorAnalyticsSession.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersion.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorSessionSummary, Verbose, All);

/* FEditorSessionSummarySender */

namespace EditorSessionSenderDefs
{
	static const FTimespan SessionExpiration = FTimespan::FromDays(30.0);
	static const float HeartbeatPeriodSeconds = 60;

	// shutdown types
	static const FString RunningSessionToken(TEXT("Running"));
	static const FString ShutdownSessionToken(TEXT("Shutdown"));
	static const FString CrashSessionToken(TEXT("Crashed"));
	static const FString TerminatedSessionToken(TEXT("Terminated"));
	static const FString DebuggerSessionToken(TEXT("Debugger"));
	static const FString AbnormalSessionToken(TEXT("AbnormalShutdown"));
}

FEditorSessionSummarySender::FEditorSessionSummarySender(IAnalyticsProviderET& InAnalyticsProvider, const FString& InSenderName, const uint32 InCurrentSessionProcessId)
	: HeartbeatTimeElapsed(0.0f)
	, AnalyticsProvider(InAnalyticsProvider)
	, Sender(InSenderName)
	, CurrentSessionProcessId(InCurrentSessionProcessId)
{
}

FEditorSessionSummarySender::~FEditorSessionSummarySender()
{
}

void FEditorSessionSummarySender::Tick(float DeltaTime)
{
	HeartbeatTimeElapsed += DeltaTime;

	if (HeartbeatTimeElapsed > EditorSessionSenderDefs::HeartbeatPeriodSeconds)
	{
		HeartbeatTimeElapsed = 0.0f;

		SendStoredSessions();
	}
}

void FEditorSessionSummarySender::Shutdown()
{
	SendStoredSessions(/*bForceSendCurrentSession*/true);
}

void FEditorSessionSummarySender::SendStoredSessions(const bool bForceSendOwnedSession) const
{
	// Load the list of sessions to process. Expect contention on the analytic session lock between the Editor and CrashReportClientEditor (on windows) or between Editor instances (on mac/linux)
	//   - Try every 'n' seconds if bForceSendCurrentSession is true.
	//   - Don't block and don't loop if bForceSendCurrentSession is false.
	TArray<FEditorAnalyticsSession> SessionsToReport;
	FTimespan Timemout(bForceSendOwnedSession ? FTimespan::FromSeconds(5) : FTimespan::Zero());
	bool bSessionsLoaded = false;
	do
	{
		if (FEditorAnalyticsSession::Lock(Timemout))
		{
			// Get list of sessions in storage
			TArray<FEditorAnalyticsSession> ExistingSessions;
			FEditorAnalyticsSession::LoadAllStoredSessions(ExistingSessions);

			TArray<FEditorAnalyticsSession> SessionsToDelete;

			// Whether this summary sender instance was tasked to send the specified session.
			auto HasOwnershipOf = [this](const FEditorAnalyticsSession& InSession) { return InSession.PlatformProcessID == CurrentSessionProcessId; };

			// Whether the process(es) writing and/or sending the specified session are all dead (and cannot write or send the session anymore).
			auto IsOrphan = [](const FEditorAnalyticsSession& InSession) { return !FPlatformProcess::IsApplicationRunning(InSession.PlatformProcessID) && (InSession.MonitorProcessID == 0 || !FPlatformProcess::IsApplicationRunning(InSession.MonitorProcessID)); };

			// Check each stored session to see if they should be sent or not
			for (FEditorAnalyticsSession& Session : ExistingSessions)
			{
				if (HasOwnershipOf(Session)) // This process was configured to send this session.
				{
					if (!bForceSendOwnedSession) // Don't send until forced to (on shutdown).
					{
						continue;
					}
				}
				else if (!IsOrphan(Session))
				{
					continue; // Skip, another process is in charge of sending this session.
				}

				const FTimespan SessionAge = FDateTime::UtcNow() - Session.Timestamp;
				if (SessionAge < EditorSessionSenderDefs::SessionExpiration)
				{
					SessionsToReport.Add(Session);
				}
				SessionsToDelete.Add(Session);
			}

			// NOTE: The sessions are deleted before sending (while holding the lock) to prevent another process from finding and sending the same orphan sessions twice.
			//       This also ensures a session will never be sent twice in case a crash occurs while sending, but all sessions loaded in memory for sending will also be lost.
			for (const FEditorAnalyticsSession& ToDelete : SessionsToDelete)
			{
				ToDelete.Delete();
				ExistingSessions.RemoveAll([&ToDelete](const FEditorAnalyticsSession& Session)
					{
						return Session.SessionId == ToDelete.SessionId;
					});
			}

			TArray<FString> SessionIDs;
			SessionIDs.Reserve(ExistingSessions.Num());
			Algo::Transform(ExistingSessions, SessionIDs, &FEditorAnalyticsSession::SessionId);

			FEditorAnalyticsSession::SaveStoredSessionIDs(SessionIDs);

			FEditorAnalyticsSession::Unlock();
			bSessionsLoaded = true;
		}
	} while (bForceSendOwnedSession && !bSessionsLoaded); // Retry until session are loaded if the sender is forced to send the current session.

	// Send the sessions (without holding the lock).
	for (const FEditorAnalyticsSession& Session : SessionsToReport)
	{
		SendSessionSummaryEvent(Session);
	}
}

void FEditorSessionSummarySender::SendSessionSummaryEvent(const FEditorAnalyticsSession& Session) const
{
	FGuid SessionId;
	FString SessionIdString = Session.SessionId;
	if (FGuid::Parse(SessionIdString, SessionId))
	{
		// convert session guid to one with braces for sending to analytics
		SessionIdString = SessionId.ToString(EGuidFormats::DigitsWithHyphensInBraces);
	}

	FString ShutdownTypeString = Session.bCrashed ? EditorSessionSenderDefs::CrashSessionToken :
		(Session.bWasEverDebugger ? EditorSessionSenderDefs::DebuggerSessionToken :
		(Session.bIsTerminating ? EditorSessionSenderDefs::TerminatedSessionToken :
		(Session.bWasShutdown ? EditorSessionSenderDefs::ShutdownSessionToken : EditorSessionSenderDefs::AbnormalSessionToken)));

	FString PluginsString = FString::Join(Session.Plugins, TEXT(","));

	TArray<FAnalyticsEventAttribute> AnalyticsAttributes;

	// Track which app/user is sending the summary (summary can be sent by another process (CrashReportClient) or another instance in case of crash/abnormal terminaison.
	AnalyticsAttributes.Emplace(TEXT("SummarySenderAppId"), AnalyticsProvider.GetAppID());
	AnalyticsAttributes.Emplace(TEXT("SummarySenderAppVersion"), AnalyticsProvider.GetAppVersion());
	AnalyticsAttributes.Emplace(TEXT("SummarySenderEngineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Changelist)); // Same as in EditorSessionSummaryWriter.cpp
	AnalyticsAttributes.Emplace(TEXT("SummarySenderUserId"), AnalyticsProvider.GetUserID());
	AnalyticsAttributes.Emplace(TEXT("SummarySenderSessionId"), AnalyticsProvider.GetSessionID()); // Not stripping the {} around the GUID like EditorSessionSummaryWriter does with SessionId.

	AnalyticsAttributes.Emplace(TEXT("ProjectName"), Session.ProjectName);
	AnalyticsAttributes.Emplace(TEXT("ProjectID"), Session.ProjectID);
	AnalyticsAttributes.Emplace(TEXT("ProjectDescription"), Session.ProjectDescription);
	AnalyticsAttributes.Emplace(TEXT("ProjectVersion"), Session.ProjectVersion);
	AnalyticsAttributes.Emplace(TEXT("Platform"), FPlatformProperties::IniPlatformName());
	AnalyticsAttributes.Emplace(TEXT("SessionId"), SessionIdString); // The provider is expected to add it as "SessionID" param in the HTTP request, but keep it for completness, because the formats are slightly different.
	AnalyticsAttributes.Emplace(TEXT("EngineVersion"), Session.EngineVersion); // The provider is expected to add it as "AppVersion" param in the HTTP request, but keep it for completness, because the formats are slightly different.
	AnalyticsAttributes.Emplace(TEXT("ShutdownType"), ShutdownTypeString);
	AnalyticsAttributes.Emplace(TEXT("StartupTimestamp"), Session.StartupTimestamp.ToIso8601());
	AnalyticsAttributes.Emplace(TEXT("Timestamp"), Session.Timestamp.ToIso8601());
	AnalyticsAttributes.Emplace(TEXT("SessionDuration"), FMath::FloorToInt(static_cast<float>((Session.Timestamp - Session.StartupTimestamp).GetTotalSeconds())));
	AnalyticsAttributes.Emplace(TEXT("1MinIdle"), Session.Idle1Min);
	AnalyticsAttributes.Emplace(TEXT("5MinIdle"), Session.Idle5Min);
	AnalyticsAttributes.Emplace(TEXT("30MinIdle"), Session.Idle30Min);
	AnalyticsAttributes.Emplace(TEXT("CurrentUserActivity"), Session.CurrentUserActivity);
	AnalyticsAttributes.Emplace(TEXT("AverageFPS"), Session.AverageFPS);
	AnalyticsAttributes.Emplace(TEXT("Plugins"), PluginsString);
	AnalyticsAttributes.Emplace(TEXT("DesktopGPUAdapter"), Session.DesktopGPUAdapter);
	AnalyticsAttributes.Emplace(TEXT("RenderingGPUAdapter"), Session.RenderingGPUAdapter);
	AnalyticsAttributes.Emplace(TEXT("GPUVendorID"), Session.GPUVendorID);
	AnalyticsAttributes.Emplace(TEXT("GPUDeviceID"), Session.GPUDeviceID);
	AnalyticsAttributes.Emplace(TEXT("GRHIDeviceRevision"), Session.GRHIDeviceRevision);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterInternalDriverVersion"), Session.GRHIAdapterInternalDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("GRHIAdapterUserDriverVersion"), Session.GRHIAdapterUserDriverVersion);
	AnalyticsAttributes.Emplace(TEXT("TotalPhysicalRAM"), Session.TotalPhysicalRAM);
	AnalyticsAttributes.Emplace(TEXT("CPUPhysicalCores"), Session.CPUPhysicalCores);
	AnalyticsAttributes.Emplace(TEXT("CPULogicalCores"), Session.CPULogicalCores);
	AnalyticsAttributes.Emplace(TEXT("CPUVendor"), Session.CPUVendor);
	AnalyticsAttributes.Emplace(TEXT("CPUBrand"), Session.CPUBrand);
	AnalyticsAttributes.Emplace(TEXT("OSMajor"), Session.OSMajor);
	AnalyticsAttributes.Emplace(TEXT("OSMinor"), Session.OSMinor);
	AnalyticsAttributes.Emplace(TEXT("OSVersion"), Session.OSVersion);
	AnalyticsAttributes.Emplace(TEXT("Is64BitOS"), Session.bIs64BitOS);
	AnalyticsAttributes.Emplace(TEXT("GPUCrash"), Session.bGPUCrashed);
	AnalyticsAttributes.Emplace(TEXT("WasDebugged"), Session.bWasEverDebugger);
	AnalyticsAttributes.Emplace(TEXT("IsVanilla"), Session.bIsVanilla);
	AnalyticsAttributes.Emplace(TEXT("WasShutdown"), Session.bWasShutdown);
	AnalyticsAttributes.Emplace(TEXT("IsInPIE"), Session.bIsInPIE);
	AnalyticsAttributes.Emplace(TEXT("IsInEnterprise"), Session.bIsInEnterprise);
	AnalyticsAttributes.Emplace(TEXT("IsInVRMode"), Session.bIsInVRMode);
	AnalyticsAttributes.Emplace(TEXT("IsLowDriveSpace"), Session.bIsLowDriveSpace);
	AnalyticsAttributes.Emplace(TEXT("SentFrom"), Sender);

	// Add the exit code to the report if it was set by out-of-process monitor (CrashReportClientEditor).
	if (Session.ExitCode.IsSet())
	{
		AnalyticsAttributes.Emplace(TEXT("ExitCode"), Session.ExitCode.GetValue());
	}

	// Was this summary produced by another process than itself or the out-of-process monitor for that run?
	AnalyticsAttributes.Emplace(TEXT("DelayedSend"), Session.PlatformProcessID != CurrentSessionProcessId);

	// Sending the summary event of the current process analytic session?
	if (AnalyticsProvider.GetSessionID().Contains(Session.SessionId)) // The string (GUID) returned by GetSessionID() is surrounded with braces like "{3FEA3232-...}" while Session.SessionId is not -> "3FEA3232-..."
	{
		AnalyticsProvider.RecordEvent(TEXT("SessionSummary"), AnalyticsAttributes);
	}
	else // The summary was created by another process/instance in a different session. (Ex: Editor sending a summary a prevoulsy crashed instance or CrashReportClientEditor sending it on behalf of the Editor)
	{
		// The provider sending a 'summary event' created by another instance/process must parametrize its post request 'as if' it was sent from the instance/session that created it (backend expectation).
		// Create a new provider to avoid interfering with the current session events. (ex. if another thread sends telemetry at the same time, don't accidently tag it with the wrong SessionID, AppID, etc.).
		TSharedPtr<IAnalyticsProviderET> TempSummaryProvider = FAnalyticsET::Get().CreateAnalyticsProvider(AnalyticsProvider.GetConfig());
		
		// Reconfigure the analytics provider to sent the summary event 'as if' it was sent by the process that created it. This is required by the analytics backend.
		FGuid SessionGuid;
		FGuid::Parse(Session.SessionId, SessionGuid);
		TempSummaryProvider->SetSessionID(SessionGuid.ToString(EGuidFormats::DigitsWithHyphensInBraces)); // Ensure to put back the {} around the GUID.
		TempSummaryProvider->SetAppID(CopyTemp(Session.AppId));
		TempSummaryProvider->SetAppVersion(CopyTemp(Session.AppVersion));
		TempSummaryProvider->SetUserID(CopyTemp(Session.UserId));

		// Send the summary.
		TempSummaryProvider->RecordEvent(TEXT("SessionSummary"), AnalyticsAttributes);

		// The temporary provider is about to be deleted (going out of scope), ensure it sents its report.
		TempSummaryProvider->BlockUntilFlushed(2.0f);
	}

	UE_LOG(LogEditorSessionSummary, Log, TEXT("EditorSessionSummary sent report. Type=%s, SessionId=%s"), *ShutdownTypeString, *SessionIdString);
}
