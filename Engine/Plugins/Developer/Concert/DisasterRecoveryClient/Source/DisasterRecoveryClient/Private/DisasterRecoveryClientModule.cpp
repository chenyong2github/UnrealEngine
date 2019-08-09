// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IDisasterRecoveryClientModule.h"
#include "DisasterRecoverySessionInfo.h"

#include "IConcertSyncClientModule.h"
#include "IConcertModule.h"
#include "IConcertClient.h"
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
		// Hook to the PreExit callback, needed to execute UObject related shutdowns
		FCoreDelegates::OnPreExit.AddRaw(this, &FDisasterRecoveryClientModule::HandleAppPreExit);

		// Wait for init to finish before starting the Disaster Recovery service
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDisasterRecoveryClientModule::OnEngineInitComplete);

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
		const UDisasterRecoverClientConfig* Config = GetDefault<UDisasterRecoverClientConfig>();
		if (!Config->bIsEnabled)
		{
			DisasterRecoveryUtil::EndRecovery(); // Abort recovery.
			HandleAppPreExit(); // Leave the session and shutdown Concert.
			DeleteDisasterRecoverySessionInfo(); // Erase the witness file.
			FPlatformProcess::TerminateProc(DisasterRecoveryServiceHandle); // Required to support restarting. (2 service on the same project/dir fails to share DB files).
		}
		else
		{
			OnEngineInitComplete(); // Restart the service/recreate the session.
		}
		return true;
	}

	// Module shutdown is dependent on the UObject system which is currently shutdown on AppExit
	void HandleAppPreExit()
	{
		// if UObject system isn't initialized, skip shutdown
		if (!UObjectInitialized())
		{
			return;
		}

		// Shutdown cleanly - don't auto-restore the active session
		if (GetDefault<UDisasterRecoverClientConfig>()->bIsEnabled)
		{
			FDisasterRecoverySessionInfo SessionInfoToSave;
			if (LoadDisasterRecoverySessionInfo(SessionInfoToSave))
			{
				SessionInfoToSave.bAutoRestoreLastSession = false;
				SaveDisasterRecoverySessionInfo(SessionInfoToSave);
			}
		}

		if (CheckDisasterRecoveryServiceHealthTickHandle.IsValid())
		{
			FTicker::GetCoreTicker().RemoveTicker(CheckDisasterRecoveryServiceHealthTickHandle);
			CheckDisasterRecoveryServiceHealthTickHandle.Reset();
		}

		if (DisasterRecoveryClient)
		{
			DisasterRecoveryClient->Shutdown();
			DisasterRecoveryClient.Reset();
		}
	}

	void OnEngineInitComplete()
	{
		const UDisasterRecoverClientConfig* Config = GetDefault<UDisasterRecoverClientConfig>();
		if (!Config->bIsEnabled)
		{
			return;
		}

		{
			FDisasterRecoverySessionInfo SessionInfoToRestore;
			if (LoadDisasterRecoverySessionInfo(SessionInfoToRestore))
			{
				StartDisasterRecoveryService(&SessionInfoToRestore);
			}
			else
			{
				StartDisasterRecoveryService();
			}
		}

		CheckDisasterRecoveryServiceHealthTickHandle = FTicker::GetCoreTicker().AddTicker(TEXT("CheckDisasterRecoveryServiceHealth"), 1.0f, [this](float)
		{
			CheckDisasterRecoveryServiceHealth();
			return true;
		});
	}

	/** Returns the folder where the disaster recovery service should keep the live session files (the working directory). */
	static FString GetDefaultServerWorkingDir()
	{
		return FPaths::ProjectIntermediateDir() / TEXT("Concert") / TEXT("Server");
	}

	/** Returns the folder where the disaster recovery service should keep the archived session files (the saved directory). */
	static FString GetDefaultServerArchiveDir()
	{
		// Put the session data in the project dir.
		return FPaths::ProjectSavedDir() / TEXT("Concert") / TEXT("Server");
	}

	static FString GetDisasterRecoverySessionInfoFilename()
	{
		return FPaths::ProjectSavedDir() / TEXT("Concert") / TEXT("DisasterRecovery") / TEXT("SessionInfo.json");
	}

	static bool LoadDisasterRecoverySessionInfo(FDisasterRecoverySessionInfo& OutSessionInfo)
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

	static bool SaveDisasterRecoverySessionInfo(const FDisasterRecoverySessionInfo& InSessionInfo)
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

	static bool DeleteDisasterRecoverySessionInfo()
	{
		return IFileManager::Get().Delete(*GetDisasterRecoverySessionInfoFilename());
	}

	static FString GetDisasterRecoveryServicePath()
	{
		auto GetDisasterRecoveryServicePathForBuildConfiguration = [](const EBuildConfiguration InBuildConfiguration) -> FString
		{
			FString ServicePath = FPlatformProcess::GenerateApplicationPath(TEXT("UnrealDisasterRecoveryService"), InBuildConfiguration);
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

	void DisasterRecoverySessionCreated(TSharedRef<IConcertClientSession> InSession)
	{
		FDisasterRecoverySessionInfo SessionInfoToSave;
		SessionInfoToSave.LastSessionName = InSession->GetSessionInfo().SessionName;

		// Normally, bAutoRestoreLastSession is set true here and overwritten to false when the app exits normally, but when running under the debugger, auto-restore is disabled as
		// programmers kill applications (stop the debugger) and this should not count as a crash (unless you want to simulate crash this way - see below).
		SessionInfoToSave.bAutoRestoreLastSession = !FPlatformMisc::IsDebuggerPresent();
		//SessionInfoToSave.bAutoRestoreLastSession = true; // <- THIS CODE MUST BE COMMENTED BEFORE SUBMITTING: Uncomment for debugging purpose only. Simulate a crash by stopping the debugger during a session.
		SaveDisasterRecoverySessionInfo(SessionInfoToSave);
	}

	bool StartDisasterRecoveryService(const FDisasterRecoverySessionInfo* InSessionInfoToRestore = nullptr)
	{
		if (DisasterRecoveryClient)
		{
			DisasterRecoveryClient->Shutdown();
			DisasterRecoveryClient.Reset();
		}

		if (!FApp::HasProjectName())
		{
			return false;
		}

		// Find the service path that will host the sync server
		const FString DisasterRecoveryServicePath = GetDisasterRecoveryServicePath();
		if (DisasterRecoveryServicePath.IsEmpty())
		{
			UE_LOG(LogDisasterRecovery, Warning, TEXT("Disaster Recovery Service application was not found. Disaster Recovery will be disabled! Please build 'UnrealDisasterRecoveryService'."));
			return false;
		}

		const FString DisasterRecoveryServerName = FString::Printf(TEXT("%s_%d"), *FApp::GetInstanceId().ToString(), DisasterRecoveryServiceInstanceCount++);
		const FString DisasterRecoverySessionName = FString::Printf(TEXT("%s_%s_%s"), FApp::GetProjectName(), *FDateTime::Now().ToString(), *DisasterRecoveryServerName);

		FString DisasterRecoveryServiceCommandLine;
		DisasterRecoveryServiceCommandLine += FString::Printf(TEXT(" -ConcertServer=\"%s\""), *DisasterRecoveryServerName);
		DisasterRecoveryServiceCommandLine += TEXT(" -ConcertIgnore");
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

		bool bRecoveringFromCrash = InSessionInfoToRestore && InSessionInfoToRestore->bAutoRestoreLastSession;

		// Create and populate the client config object
		UConcertClientConfig* ClientConfig = NewObject<UConcertClientConfig>();
		ClientConfig->bIsHeadless = true;
		ClientConfig->bInstallEditorToolbarButton = false;
		ClientConfig->bAutoConnect = !bRecoveringFromCrash; // If recovering from a crash, don't auto connect -> Present UI to let user decide what to recover first.
		ClientConfig->DefaultServerURL = DisasterRecoveryServerName;
		ClientConfig->DefaultSessionName = DisasterRecoverySessionName;
		ClientConfig->DefaultSaveSessionAs = DisasterRecoverySessionName;
		//ClientConfig->ClientSettings.DiscoveryTimeoutSeconds = 0;
		ClientConfig->EndpointSettings.RemoteEndpointTimeoutSeconds = 0;

		// Create and boot the client instance, auto-joining the configured session if launching normally (not in recovery mode).
		DisasterRecoveryClient = IConcertSyncClientModule::Get().CreateClient(TEXT("DisasterRecovery"));
		DisasterRecoveryClient->GetConcertClient()->OnSessionStartup().AddRaw(this, &FDisasterRecoveryClientModule::DisasterRecoverySessionCreated);
		DisasterRecoveryClient->Startup(ClientConfig, EConcertSyncSessionFlags::Default_DisasterRecoverySession);

		// Starts the recovery process.
		if (bRecoveringFromCrash)
		{
			if (GUnrealEd)
			{
				// Prevent the "Auto-Save" system from restoring the packages before Disaster Recovery plugin.
				GUnrealEd->GetPackageAutoSaver().DisableRestorePromptAndDeclinePackageRecovery();
			}

			DisasterRecoveryUtil::StartRecovery(DisasterRecoveryClient.ToSharedRef(), InSessionInfoToRestore->LastSessionName, /*bLiveDataOnly*/ false);
		}

		return true;
	}

	void CheckDisasterRecoveryServiceHealth()
	{
		if (!DisasterRecoveryServiceHandle.IsValid())
		{
			// Disaster Recovery is not available - nothing to check the health of
			return;
		}

		// If the Disaster Recovery Service stopped, attempt to restart it
		if (!FPlatformProcess::IsProcRunning(DisasterRecoveryServiceHandle))
		{
			DisasterRecoveryServiceHandle.Reset();
			StartDisasterRecoveryService();
		}
	}

	/** Sync client handling disaster recovery */
	TSharedPtr<IConcertSyncClient> DisasterRecoveryClient;

	/** Handle to the active disaster recovery service app, if any */
	FProcHandle DisasterRecoveryServiceHandle;

	/** Ticker for ensuring that the disaster recovery service app that was started is still running */
	FDelegateHandle CheckDisasterRecoveryServiceHealthTickHandle;

	/** Count of the number of times the disaster recovery service app has been started by this client */
	int32 DisasterRecoveryServiceInstanceCount = 0;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDisasterRecoveryClientModule, DisasterRecoveryClient);
