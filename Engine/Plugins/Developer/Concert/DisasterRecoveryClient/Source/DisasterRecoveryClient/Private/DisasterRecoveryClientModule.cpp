// Copyright Epic Games, Inc. All Rights Reserved.

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
class FDisasterRecoveryClientModule : public IDisasterRecoveryClientModule, public IDisasterRecoverySessionManager
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

	FString GetDisasterRecoverySessionInfoFilename() const
	{
		return FPaths::ProjectSavedDir() / Role / TEXT("Sessions.json");
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

		auto SetAutoRestoreFlag = [](FDisasterRecoverySession& RecoverySession)
		{
			// Normally, bAutoRestoreLastSession is set true here and overwritten to false when the app exits normally, but when running under the debugger, auto-restore is disabled as
			// programmers kill applications (stop the debugger) and this should not count as a crash (unless you want to simulate crash this way - see below).
			RecoverySession.bAutoRestoreLastSession = !FPlatformMisc::IsDebuggerPresent();
			//Session.bAutoRestoreLastSession = true; // <- MUST BE COMMENTED BEFORE SUBMITTING: For debugging purpose only. Simulate a crash by stopping the debugger during a session.
		};

		// Is this a new session created by recovering from another one?
		if (FDisasterRecoverySession* RestoredSession = RecoverySessionInfo.Sessions.FindByPredicate(
			[&InSession, &SetAutoRestoreFlag](const FDisasterRecoverySession& Session) { return Session.bAutoRestoreLastSession && Session.HostProcessId == FPlatformProcess::GetCurrentProcessId(); })) // See TakeRecoverySessionOwnership().
		{
			RestoredSession->LastSessionName = InSession->GetSessionInfo().SessionName;
			SetAutoRestoreFlag(*RestoredSession);
		}
		else // Create a new session.
		{
			FDisasterRecoverySession& RecoverySession = RecoverySessionInfo.Sessions.AddDefaulted_GetRef();
			RecoverySession.RepositoryRootDir = GetSessionRepositoryRootDir();
			RecoverySession.LastSessionName = InSession->GetSessionInfo().SessionName;
			RecoverySession.HostProcessId = FPlatformProcess::GetCurrentProcessId();
			RecoverySession.RepositoryId = GetSessionRepositoryId();
			SetAutoRestoreFlag(RecoverySession);
		}

		// Save the file.
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}
	
	/** Returns the number of session to keep around in the history for a given project. */
	int32 GetMaxSessionHistorySize() const
	{
		return FMath::Max(0, GetDefault<UDisasterRecoverClientConfig>()->SessionHistorySize);
	}

	/** Returns this client repository database root dir. */
	virtual FString GetSessionRepositoryRootDir() const override
	{
		const FString& RootDir = GetDefault<UDisasterRecoverClientConfig>()->RecoverySessionDir.Path;
		if (!RootDir.IsEmpty() && (IFileManager::Get().DirectoryExists(*RootDir) || IFileManager::Get().MakeDirectory(*RootDir, /*Tree*/true)))
		{
			return RootDir;
		}

		return FPaths::ProjectSavedDir() / Role / TEXT("Sessions"); // Returns the default.
	}

	/** Return the repository ID to use if a new session is created rather than recovered. */
	virtual FGuid GetSessionRepositoryId() const override
	{ 
		static FGuid RepositoryId = FGuid::NewGuid();
		return RepositoryId;
	}

	/** Among the tracked sessions, select which one is the best candidate for recovery. Concurrent Editors might be running/crashing/restoring at the same time. */
	virtual TOptional<FDisasterRecoverySession> FindRecoverySessionCandidate(const TArray<FConcertSessionRepositoryInfo>& Repositories) override
	{
		// +------------------------+-----------------+-------------------+---------------+
		// | AutoRestoreLastSession | HostProcessDead | RepositoryMounted | Deduced State |
		// +------------------------+-----------------+-------------------+---------------+
		// |          no            |        Any      |        Any        |  Normal Exit  | -> The session has exited properly (according to Disaster Recovery)
		// |          yes           |        yes      |        no         |  Crashed      | -> The session is cold dead.
		// |          yes           |        no       |        no         |  Crashing     | -> The session is crashing, but CrashReporterClientEditor detected it and shutted down DR service before the editor finished crashing.
		// |          yes           |        Any      |        yes        |  Running      | -> The session is presumably running. Might be crashing or restoring, but as long as the repository is mounted, it is assumed running.
		// +------------------------+-----------------+-------------------+---------------+
	
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		// Checks if two running process IDs are instances of the same executable.
		auto IsSameExecutable = [](int32 LhsProcessId, int32 RhsProcessId)
		{
			return FPaths::GetPathLeaf(FPlatformProcess::GetApplicationName(LhsProcessId)) == FPaths::GetPathLeaf(FPlatformProcess::GetApplicationName(RhsProcessId));
		};

		// Returns true if the process hosting the session crashed. Note that CrashReporterClientEditor will shut down the server, archive the session and relaunch a new editor (and a new recovery client) before the
		// previous editor finished crashing. The process hosting a session may still be alive, but not its server. In such case, the session repository will be unmounted and available for restoration unless another
		// server instances is already restoring the session.
		auto IsHostProcessDead = [&IsSameExecutable](const FDisasterRecoverySession& Session)
		{
			return Session.bAutoRestoreLastSession && (Session.HostProcessId == 0 || !FPlatformProcess::IsApplicationRunning(Session.HostProcessId) || !IsSameExecutable(Session.HostProcessId, FPlatformProcess::GetCurrentProcessId()));
		};

		// Returns true if the session repository is mounted by another client/server pair preventing this client/server instance from loading it.
		auto IsRepositoryMounted = [](const TArray<FConcertSessionRepositoryInfo>& Repositories, const FGuid& SessionRepositoryId)
		{
			const FConcertSessionRepositoryInfo* Repository = Repositories.FindByPredicate([&SessionRepositoryId](const FConcertSessionRepositoryInfo& CandidateRepos) { return CandidateRepos.RepositoryId == SessionRepositoryId; });
			return Repository ? Repository->bMounted : false; // Not found means not mounted -> this will likely prevent the session from being restored, but this will be gracefully handled by the FSM.
		};

		// Sort the candidates by 'hotness'.
		TArray<FDisasterRecoverySession*> SortedCandidates;
		for (FDisasterRecoverySession& Session : RecoverySessionInfo.Sessions)
		{
			if (Session.bAutoRestoreLastSession)
			{
				if (IsRepositoryMounted(Repositories, Session.RepositoryId))
				{
					continue; // Two editors on the same project. This client will not be able to mount the repository, it is already mounted by another instance (which might be crashing or running, but no way to know).
				}
				else if (IsHostProcessDead(Session))
				{
					SortedCandidates.Add(&Session); // The session is cold dead.
				}
				else // The session host process is likely crashing, but this was detected by the CRC and it unmounted the session repository.
				{
					SortedCandidates.Insert(&Session, 0); // Keep most recent session crash in front.
				}

				Session.HostProcessId = 0; // Host is dead or dying, clear it.
			}
		}

		// If user ran concurrent instances of the Editor on the same project and more than one instance crashed, keep only one in front and discard the other(s). (Eventually... add code to ask the user to pick one)
		while (SortedCandidates.Num() > 1)
		{
			SortedCandidates.Last()->bAutoRestoreLastSession = false;
			SortedCandidates.Pop(/*bAllowShrinking*/false);
		}

		// Remove completed sessions (in case a session was flagged bAutoRestoreLastSession = false above)
		RecoverySessionInfo.Sessions.RemoveAll([&RecoverySessionInfo](const FDisasterRecoverySession& Session)
		{
			if (!Session.bAutoRestoreLastSession) // Remove if the 'restore' flag is false.
			{
				RecoverySessionInfo.SessionHistory.Add(Session);
				return true;
			}
			return false;
		});

		// Found a suitable candidate to restore?
		TOptional<FDisasterRecoverySession> RestoreCandidate;
		if (SortedCandidates.Num() == 1)
		{
			RestoreCandidate = *SortedCandidates[0];
		}

		// Save the recovery session info file.
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);

		return RestoreCandidate;
	}

	/** Mark this process as responsible to restore the specified session. Can take the session ownership only once the client workspace has been mounted on the server. */
	virtual void TakeRecoverySessionOwnership(const FDisasterRecoverySession& TargetSession) override
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		if (FDisasterRecoverySession* Session = RecoverySessionInfo.Sessions.FindByPredicate([&TargetSession](const FDisasterRecoverySession& Candidate) { return Candidate.RepositoryId == TargetSession.RepositoryId; }))
		{
			Session->HostProcessId = FPlatformProcess::GetCurrentProcessId(); // Mark this process as owner.

			// Save the recovery session info file.
			SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
		}
	}

	virtual void DiscardRecoverySession(const FDisasterRecoverySession& Session) override
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		RecoverySessionInfo.Sessions.RemoveAll([Session](const FDisasterRecoverySession& Candidate) { return Session.RepositoryId == Candidate.RepositoryId; });

		// Save the recovery session info file.
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}

	bool HasCandidateSessionToRestore() const
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);

		// If at least one session exist with the restore flag true, it may be candidate to restore.
		return RecoverySessionInfo.Sessions.ContainsByPredicate([](const FDisasterRecoverySession& Session) { return Session.bAutoRestoreLastSession; });
	}

	void ClearSessionInfoFile()
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);
		RecoverySessionInfo.SessionHistory.Append(RecoverySessionInfo.Sessions);
		RecoverySessionInfo.Sessions.Empty();
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}

	/** Return the list of expired client workspaces that can be deleted from the server. */
	virtual TArray<FGuid> GetExpiredSessionRepositoryIds() const override
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);
		TArray<FGuid> ExpiredWorkspaceIds;
		int32 ExpiredCount = RecoverySessionInfo.SessionHistory.Num() - GetMaxSessionHistorySize();
		for (int32 i = 0; i < ExpiredCount; ++i)
		{
			ExpiredWorkspaceIds.Add(RecoverySessionInfo.SessionHistory[i].RepositoryId);
		}

		return ExpiredWorkspaceIds;
	}

	/** Invoked when client workspace were purged from the server. */
	virtual void OnSessionRepositoryDropped(const TArray<FGuid>& PurgedWorkspaceIds) override
	{
		// Ensure we get exclusive access to the recovery session info file.
		FSystemWideCriticalSection SystemWideMutex(GetSystemMutexName());

		// Load the session info file (if it exist)
		FDisasterRecoverySessionInfo RecoverySessionInfo;
		LoadDisasterRecoverySessionInfo(RecoverySessionInfo);
		for (const FGuid& PurgedSessionWorkspaceId : PurgedWorkspaceIds)
		{
			RecoverySessionInfo.SessionHistory.RemoveAll([&PurgedSessionWorkspaceId](const FDisasterRecoverySession& Session){ return PurgedSessionWorkspaceId == Session.RepositoryId; });
		}
		SaveDisasterRecoverySessionInfo(RecoverySessionInfo);
	}

	bool SpawnDisasterRecoveryServer(const FString& ServerName) // For Linux and Mac. On Windows, it is embedded in CrashReporterClient.
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
			StopDisasterRecoveryService();
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

		// Create and populate the client config object
		UConcertClientConfig* ClientConfig = NewObject<UConcertClientConfig>();
		ClientConfig->bIsHeadless = true;
		ClientConfig->bInstallEditorToolbarButton = false;
		ClientConfig->bAutoConnect = false;
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
		if (HasCandidateSessionToRestore() && GUnrealEd)
		{
			// Prevent the "Auto-Save" system from restoring the packages before Disaster Recovery plugin.
			GUnrealEd->GetPackageAutoSaver().DisableRestorePromptAndDeclinePackageRecovery();
		}

		// The FSM will try to pin a session for recovery (and may fail with a toast), if no session is found, it will create a new one.
		DisasterRecoveryUtil::StartRecovery(DisasterRecoveryClient.ToSharedRef(), *this, /*bLiveDataOnly*/ false);

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
			RecoverySessionInfo.Sessions.RemoveAll([ProcessId, &RecoverySessionInfo](const FDisasterRecoverySession& Session)
			{
				if (Session.HostProcessId == ProcessId)
				{
					RecoverySessionInfo.SessionHistory.Add_GetRef(Session).bAutoRestoreLastSession = false; // Push back the session in the history list.
					return true;
				}
				return false;
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

