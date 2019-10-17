// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IDisasterRecoveryClientModule.h"
#include "DisasterRecoverySessionInfo.h"

#include "IConcertSyncClientModule.h"
#include "IConcertModule.h"
#include "IConcertClient.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "ConcertFrontendStyle.h"

#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"

#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "Backends/JsonStructSerializerBackend.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "DisasterRecoveryFSM.h"
#include "DisasterRecoverySettings.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "IPackageAutoSaver.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDisasterRecovery, Log, All);

#define LOCTEXT_NAMESPACE "DisasterRecoveryClient"


/** Implement the Disaster Recovery module */
class FDisasterRecoveryClientModule : public IDisasterRecoveryClientModule
{
public:
	virtual void StartupModule() override
	{
		Role = TEXT("DisasterRecovery");

		// Hook to the PreExit callback, needed to execute UObject related shutdowns
		FCoreDelegates::OnPreExit.AddRaw(this, &FDisasterRecoveryClientModule::HandleAppPreExit);

		// Wait for init to finish before starting the Disaster Recovery service
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDisasterRecoveryClientModule::OnEngineInitComplete);

		// Hook to listen when a new session is created.
		IConcertSyncClientModule::Get().OnClientCreated().AddRaw(this, &FDisasterRecoveryClientModule::HandleConcertSyncClientCreated);
		for (TSharedRef<IConcertSyncClient> Client : IConcertSyncClientModule::Get().GetClients())
		{
			Client->OnSyncSessionStartup().AddRaw(this, &FDisasterRecoveryClientModule::HandleSyncSessionStartup);
			Client->OnSyncSessionShutdown().AddRaw(this, &FDisasterRecoveryClientModule::HandleSyncSessionShutdown);
		}

		// Initialize Style
		FConcertFrontendStyle::Initialize();

		// Register the Disaster Recovery Settings panel.
		RegisterSettings();
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

		// Unhook AppPreExit and call it
		FCoreDelegates::OnPreExit.RemoveAll(this);
		HandleAppPreExit();

		// Unhook this module callback from other clients.
		if (IConcertSyncClientModule::IsAvailable())
		{
			IConcertSyncClientModule::Get().OnClientCreated().RemoveAll(this);
			for (TSharedRef<IConcertSyncClient> Client : IConcertSyncClientModule::Get().GetClients())
			{
				Client->OnSyncSessionStartup().RemoveAll(this);
				Client->OnSyncSessionShutdown().RemoveAll(this);
			}
		}

		// Unregister the Disaster Recovery Settings panel.
		UnregisterSettings();
	}

	virtual TSharedPtr<IConcertSyncClient> GetClient() const override
	{
		return DisasterRecoveryClient;
	}

private:
	void RegisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "Disaster Recovery",
				LOCTEXT("DisasterRecoverySettingsName", "Disaster Recovery"),
				LOCTEXT("DisasterRecoverySettingsDescription", "Configure the Disaster Recovery Settings."),
				GetMutableDefault<UDisasterRecoverClientConfig>());

			if (SettingsSection.IsValid())
			{
				SettingsSection->OnModified().BindRaw(this, &FDisasterRecoveryClientModule::HandleSettingsSaved);
			}
		}
	}

	void UnregisterSettings()
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Disaster Recovery");
		}
	}

	bool HandleSettingsSaved()
	{
		if (GetDefault<UDisasterRecoverClientConfig>()->bIsEnabled)
		{
			StartDisasterRecoveryService();
		}
		else
		{
			StopDisasterRecoveryService();
		}
		return true;
	}

	void OnEngineInitComplete()
	{
		StartDisasterRecoveryService();
	}

	// Module shutdown is dependent on the UObject system which is currently shutdown on AppExit
	void HandleAppPreExit()
	{
		// if UObject system isn't initialized, skip shutdown
		if (!UObjectInitialized())
		{
			return;
		}

		StopDisasterRecoveryService();
	}

	void HandleConcertSyncClientCreated(TSharedRef<IConcertSyncClient> Client)
	{
		if (Client->GetConcertClient()->GetRole() != Role) // Exclude disaster recovery own session connection changes.
		{
			Client->OnSyncSessionStartup().AddRaw(this, &FDisasterRecoveryClientModule::HandleSyncSessionStartup);
			Client->OnSyncSessionShutdown().AddRaw(this, &FDisasterRecoveryClientModule::HandleSyncSessionShutdown);
		}
	}

	void HandleSyncSessionStartup(const IConcertSyncClient* SyncClient)
	{
		check(DisasterRecoveryClient.Get() != SyncClient)
		SetIgnoreOnRestoreState(!IsCompatibleWithOtherConcertSessions(SyncClient, /*SyncClientShuttingDownSession*/nullptr));
	}

	void HandleSyncSessionShutdown(const IConcertSyncClient* SyncClient)
	{
		check(DisasterRecoveryClient.Get() != SyncClient);
		SetIgnoreOnRestoreState(!IsCompatibleWithOtherConcertSessions(/*SyncClientStartingSession*/nullptr, SyncClient));
	}

	FGuid GetDisasterRecoverySessionId() const
	{
		if (DisasterRecoveryClient)
		{
			if (TSharedPtr<IConcertClientSession> Session = DisasterRecoveryClient->GetConcertClient()->GetCurrentSession())
			{
				return Session->GetSessionInfo().SessionId;
			}
		}

		return FGuid(); // Invalid.
	}

	/** Returns the folder where the disaster recovery service should keep the live session files (the working directory). */
	FString GetDefaultServerWorkingDir() const
	{
		return FPaths::ProjectIntermediateDir() / Role / TEXT("Service");
	}

	/** Returns the folder where the disaster recovery service should keep the archived session files (the saved directory). */
	FString GetDefaultServerArchiveDir() const
	{
		// Put the session data in the project dir.
		return FPaths::ProjectSavedDir() / Role / TEXT("Service");
	}

	FString GetDisasterRecoverySessionInfoFilename() const
	{
		return FPaths::ProjectSavedDir() / Role / TEXT("SessionInfo.json");
	}

	bool LoadDisasterRecoverySessionInfo(FDisasterRecoverySessionInfo& OutSessionInfo) const
	{
		if (TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*GetDisasterRecoverySessionInfoFilename())))
		{
			FJsonStructDeserializerBackend Backend(*FileReader);
			FStructDeserializer::Deserialize(OutSessionInfo, Backend);

			FileReader->Close();
			return !FileReader->IsError();
		}

		return false;
	}

	bool SaveDisasterRecoverySessionInfo(const FDisasterRecoverySessionInfo& InSessionInfo) const
	{
		if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*GetDisasterRecoverySessionInfoFilename())))
		{
			FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
			FStructSerializer::Serialize(InSessionInfo, Backend);

			FileWriter->Close();
			return !FileWriter->IsError();
		}

		return false;
	}

	static FString GetDisasterRecoveryServicePath()
	{
		auto GetDisasterRecoveryServicePathForBuildConfiguration = [](const EBuildConfiguration InBuildConfiguration) -> FString
		{
			FString ServicePath = FPlatformProcess::GenerateApplicationPath(DisasterRecoveryUtil::GetDisasterRecoveryServiceExeName(), InBuildConfiguration);
			return FPaths::FileExists(ServicePath) ? ServicePath : FString();
		};

		// First try and use our build configuration
		FString ServicePath = GetDisasterRecoveryServicePathForBuildConfiguration(FApp::GetBuildConfiguration());

		// Fall back to Development if the app doesn't exist for our build configuration, as installed builds only build it for Development
		if (ServicePath.IsEmpty() && FApp::GetBuildConfiguration() != EBuildConfiguration::Development)
		{
			ServicePath = GetDisasterRecoveryServicePathForBuildConfiguration(EBuildConfiguration::Development);
		}

		return ServicePath;
	}

	static FString GetSystemMutexName()
	{
		return TEXT("Unreal_DisasterRecovery_4221FF"); // Arbitrary name that is unique among other applications.
	}

	void DisasterRecoverySessionCreated(TSharedRef<IConcertClientSession> InSession)
	{
		// Ensure we get exclusive access to the file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Reload the file.
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		// Is this a new session created by recovering from another one?
		RecoverySessionInfo.Sessions.RemoveAll([](const FDisasterRecoverySession& Session)
		{
			return Session.bAutoRestoreLastSession && Session.HostProcessId == FPlatformProcess::GetCurrentProcessId();
		});

		// Create a new session.
		FDisasterRecoverySession& Session = RecoverySessionInfo.Sessions.AddDefaulted_GetRef();
		Session.LastSessionName = InSession->GetSessionInfo().SessionName;
		Session.HostProcessId = FPlatformProcess::GetCurrentProcessId();

		// Normally, bAutoRestoreLastSession is set true here and overwritten to false when the app exits normally, but when running under the debugger, auto-restore is disabled as
		// programmers kill applications (stop the debugger) and this should not count as a crash (unless you want to simulate crash this way - see below).
		Session.bAutoRestoreLastSession = !FPlatformMisc::IsDebuggerPresent();
		//Session.bAutoRestoreLastSession = true; // <- MUST BE COMMENTED BEFORE SUBMITTING: For debugging purpose only. Simulate a crash by stopping the debugger during a session.

		// Save the file.
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}

	TPair<bool, const FConcertSessionInfo*> PinSessionToRestore(const TArray<FConcertSessionInfo>& ArchivedSessions)
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		// Checks if two running process ID are instances of same executable.
		auto IsSameExecutable = [](int32 LhsProcessId, int32 RhsProcessId)
		{
			return FPaths::GetPathLeaf(FPlatformProcess::GetApplicationName(LhsProcessId)) == FPaths::GetPathLeaf(FPlatformProcess::GetApplicationName(RhsProcessId));
		};

		// Returns true if the process hosting the session crashed. Note that CrashReporterClientEditor will shut down the server, archive the session and relaunch a new editor (and a new recovery client) before the
		// previous editor finished crashing. The process hosting a session may still be alive, but not its server. In such case, the session will be unlocked and available for restoration unless another server instances already exist.
		auto IsHostProcessCrashed = [&IsSameExecutable](const FDisasterRecoverySession& Session)
		{
			return Session.bAutoRestoreLastSession && (!FPlatformProcess::IsApplicationRunning(Session.HostProcessId) || !IsSameExecutable(Session.HostProcessId, FPlatformProcess::GetCurrentProcessId()));
		};

		// Returns true if the session was listed as archived by the server. (Might not be visible to that server if other servers are running and use the same working/saved directories, the files are not sharable)
		auto IsSessionArchived = [](const FDisasterRecoverySession& Session, const TArray<FConcertSessionInfo>& ArchivedSessions)
		{
			return ArchivedSessions.FindByPredicate([&Session](const FConcertSessionInfo& MatchCandidate) { return MatchCandidate.SessionName == Session.LastSessionName; });
		};

		// Check in the client file for session candidate to restoration.
		TPair<bool, const FConcertSessionInfo*> RestoreCandidate = MakeTuple(false, static_cast<const FConcertSessionInfo*>(nullptr));
		for (FDisasterRecoverySession& Session : RecoverySessionInfo.Sessions)
		{
			const FConcertSessionInfo* ArchivedSession = IsSessionArchived(Session, ArchivedSessions);
			if (ArchivedSession || IsHostProcessCrashed(Session))
			{
				// Already pinned a session to restore?
				if (RestoreCandidate.Key && RestoreCandidate.Value)
				{
					// User ran multiple instances of the Editor on the same project and more than one instance crashed. Clear this one and restore the first one found only.
					// The situation is unlikely to happen, but a future task would be to return a list of crashed sessions, pass it to the DisasterRecoveryFSM and prompt the user to select which one to recover.
					Session.bAutoRestoreLastSession = false;
				}
				else
				{
					// A crashed session exist.
					RestoreCandidate.Key = true;

					// Can this session be restored (i.e. the server knowns it)?
					if (ArchivedSession)
					{
						// Pin/take ownership of this session. (Will be cleared on DisasterRecoverySessionCreated() if recovery succeed)
						RestoreCandidate.Value = ArchivedSession;
						Session.HostProcessId = FPlatformProcess::GetCurrentProcessId();
					}
				}
			}
		}

		// Remove completed sessions (in case a session was flagged bAutoRestoreLastSession = false above)
		RecoverySessionInfo.Sessions.RemoveAll([](const FDisasterRecoverySession& Session)
		{
			return !Session.bAutoRestoreLastSession; // Remove if the 'restore' flag is false.
		});

		// Save the recovery session info file.
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);

		return RestoreCandidate;
	}

	bool HasCandidateSessionToRestore() const
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		// If at least one session exist, it may be candidate to restore.
		return RecoverySessionInfo.Sessions.Num() != 0;
	}

	void ClearSessionInfoFile()
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}

	bool SpawnDisasterRecoveryServer(const FString& ServerName) // On Linux and Mac. On Windows, it is embedded in CrashReporterClient.
	{
		// Find the service path that will host the sync server
		const FString DisasterRecoveryServicePath = GetDisasterRecoveryServicePath();
		if (DisasterRecoveryServicePath.IsEmpty())
		{
			UE_LOG(LogDisasterRecovery, Warning, TEXT("Disaster Recovery Service application was not found. Disaster Recovery will be disabled! Please build 'UnrealDisasterRecoveryService'."));
			return false;
		}

		FString DisasterRecoveryServiceCommandLine;
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -ConcertServer=\"%s\""), *ServerName);
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -EditorPID=%d"), FPlatformProcess::GetCurrentProcessId());
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -ConcertWorkingDir=\"%s\""), *GetDefaultServerWorkingDir());
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -ConcertSavedDir=\"%s\""), *GetDefaultServerArchiveDir());

		// Create the service process that will host the sync server
		DisasterRecoveryServiceHandle = FPlatformProcess::CreateProc(*DisasterRecoveryServicePath, *DisasterRecoveryServiceCommandLine, true, true, true, nullptr, 0, nullptr, nullptr, nullptr);
		if (!DisasterRecoveryServiceHandle.IsValid())
		{
			UE_LOG(LogDisasterRecovery, Error, TEXT("Failed to launch Disaster Recovery Service application. Disaster Recovery will be disabled!"));
			return false;
		}

		return true;
	}

	bool StartDisasterRecoveryService()
	{
		if (!GetDefault<UDisasterRecoverClientConfig>()->bIsEnabled)
		{
			return false;
		}

		if (!FApp::HasProjectName())
		{
			return false;
		}

		if (DisasterRecoveryClient)
		{
			DisasterRecoveryClient->Shutdown();
			DisasterRecoveryClient.Reset();
		}

		const FString DisasterRecoveryServerName = RecoveryService::GetRecoveryServerName();
		const FString DisasterRecoverySessionName = FString::Printf(TEXT("%s_%s_%s"), *DisasterRecoveryServerName, FApp::GetProjectName(), *FDateTime::Now().ToString());

		// If crash reporter is running out of process, it also hosts disaster recovery server as the '-ConcertServer' param is set when spawning CrashReporterClient. No need to start the UnrealDisasterRecoveryService executable.
		if (!FGenericCrashContext::IsOutOfProcessCrashReporter() && !SpawnDisasterRecoveryServer(DisasterRecoveryServerName))
		{
			return false; // Failed to spawn the service.
		}

		// It is not allowed to prompt the user for recovery in unattended mode. Forget everything about previous session(s) for this project and
		// don't to keep crashed session(s) on 'hold' to be restored the next time -unattended is not specified, the levels may have greatly changed.
		if (FApp::IsUnattended())
		{
			ClearSessionInfoFile();
		}

		// Skip restoration if they are no candidates to restore.
		bool bCandidateToRestore = HasCandidateSessionToRestore();

		// Create and populate the client config object
		UConcertClientConfig* ClientConfig = NewObject<UConcertClientConfig>();
		ClientConfig->bIsHeadless = true;
		ClientConfig->bInstallEditorToolbarButton = false;
		ClientConfig->bAutoConnect = !bCandidateToRestore; // If there is a possible candidate to restore, don't auto connect yet, run the finite state machine to know more.
		ClientConfig->DefaultServerURL = DisasterRecoveryServerName;
		ClientConfig->DefaultSessionName = DisasterRecoverySessionName;
		ClientConfig->DefaultSaveSessionAs = DisasterRecoverySessionName;
		//ClientConfig->ClientSettings.DiscoveryTimeoutSeconds = 0;
		ClientConfig->EndpointSettings.RemoteEndpointTimeoutSeconds = 0;

		// Create the recovery session and auto-join it if there is nothing to recover.
		DisasterRecoveryClient = IConcertSyncClientModule::Get().CreateClient(Role);
		DisasterRecoveryClient->GetConcertClient()->OnSessionStartup().AddRaw(this, &FDisasterRecoveryClientModule::DisasterRecoverySessionCreated);
		DisasterRecoveryClient->Startup(ClientConfig, EConcertSyncSessionFlags::Default_DisasterRecoverySession);

		// Set all events captured by the disaster recovery service as 'restorable' unless another concert client (assumed Multi-User) has created an incompatible session.
		SetIgnoreOnRestoreState(!IsCompatibleWithOtherConcertSessions(/*SyncClientStartingSession*/nullptr, /*SyncClientShuttingDownSession*/nullptr));

		// If something might be recovered from a crash.
		if (bCandidateToRestore)
		{
			if (GUnrealEd)
			{
				// Prevent the "Auto-Save" system from restoring the packages before Disaster Recovery plugin.
				GUnrealEd->GetPackageAutoSaver().DisableRestorePromptAndDeclinePackageRecovery();
			}

			// The DisasterRecoveryFSM will call this function to select and take ownership of the session to recover.
			auto PinSessionFn = [this](const TArray<FConcertSessionInfo>& ArchivedSessions)
			{
				return PinSessionToRestore(ArchivedSessions);
			};

			DisasterRecoveryUtil::StartRecovery(DisasterRecoveryClient.ToSharedRef(), PinSessionFn, /*bLiveDataOnly*/ false);
		}

		return true;
	}

	void StopDisasterRecoveryService()
	{
		// End the recovery FSM (if running).
		bool bRecoveryAborted = !DisasterRecoveryUtil::EndRecovery();
		if (!bRecoveryAborted) // Can be aborted if the user close the editor before the recovery modal window appears. (Need to be quick, but possible)
		{
			FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

			// Load the sessions file.
			FDisasterRecoverySessionInfo RecoverySessionInfo;
			LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

			// Remove the current session from the list of sessions to track.
			int32 ProcessId = FPlatformProcess::GetCurrentProcessId();
			RecoverySessionInfo.Sessions.RemoveAll([ProcessId](const FDisasterRecoverySession& Session)
			{
				return Session.HostProcessId == ProcessId;
			});

			// Write the file to disk.
			SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
		}

		if (DisasterRecoveryClient)
		{
			DisasterRecoveryClient->Shutdown();
			DisasterRecoveryClient.Reset();
		}

		if (DisasterRecoveryServiceHandle.IsValid())
		{
			FPlatformProcess::TerminateProc(DisasterRecoveryServiceHandle);
			DisasterRecoveryServiceHandle.Reset();
		}
	}

	/** Returns true if disaster recovery Concert session can run concurrently with other Concert sessions (if any). */
	bool IsCompatibleWithOtherConcertSessions(const IConcertSyncClient* SyncClientStartingSession, const IConcertSyncClient* SyncClientShuttingDownSession) const
	{
		// At the moment, we don't expect more than 2 clients. We don't have use cases for a third concurrent concert client.
		checkf(IConcertSyncClientModule::Get().GetClients().Num() <= 2, TEXT("Expected 1 disaster recovery client + 1 multi-user client at max."));

		// Scan all existing clients.
		for (const TSharedRef<IConcertSyncClient>& SyncClient : IConcertSyncClientModule::Get().GetClients())
		{
			if (SyncClient == DisasterRecoveryClient || &SyncClient.Get() == SyncClientShuttingDownSession)
			{
				continue; // Compatible with itself or the sync client is shutting down its sync session, so it cannot interfere anymore.
			}
			else if (&SyncClient.Get() == SyncClientStartingSession)
			{
				if (!IsCompatibleWithConcertClient(&SyncClient.Get()))
				{
					return false; // The sync client starting a session will interfere with disaster recovery client.
				}
			}
			else if (SyncClient->GetWorkspace() && !IsCompatibleWithConcertClient(&SyncClient.Get())) // A valid workspace means the client is joining, in or leaving a session.
			{
				return false; // That existing client is interfering with disaster recovery client.
			}
		}

		return true; // No other sessions exist or it is compatible.
	}

	bool IsCompatibleWithConcertClient(const IConcertSyncClient* SyncClient) const
	{
		check(SyncClient != DisasterRecoveryClient.Get());
		checkf(SyncClient->GetConcertClient()->GetRole() == TEXT("MultiUser"), TEXT("A new role was added, check if this role can run concurrently with disaster recovery."));

		// Multi-User (MU) sessions are not compatible with disaster recovery (DR) session because MU events are performed in a transient sandbox that doesn't exist outside the MU session.
		// If a crash occurs during a MU session, DR must not recover transactions applied to the transient sandbox. DR will will record the MU events, but for crash inspection purpose only.
		return SyncClient->GetConcertClient()->GetRole() != TEXT("MultiUser");
	}

	/** Sets whether further Concert events (transaction/package) emitted by Disaster Recovery have the 'ignore' flag on or off. */
	void SetIgnoreOnRestoreState(bool bIgnore)
	{
		if (TSharedPtr<IConcertClientWorkspace> Workspace = DisasterRecoveryClient ? DisasterRecoveryClient->GetWorkspace() : TSharedPtr<IConcertClientWorkspace>())
		{
			Workspace->SetIgnoreOnRestoreFlagForEmittedActivities(bIgnore);
		}
	}

private:
	/** This client role, a tag given to different types of concert client, i.e. DisasterRecovery for this one. */
	FString Role;

	/** Sync client handling disaster recovery */
	TSharedPtr<IConcertSyncClient> DisasterRecoveryClient;

	/** Handle to the active disaster recovery service app, if any */
	FProcHandle DisasterRecoveryServiceHandle;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDisasterRecoveryClientModule, DisasterRecoveryClient);

