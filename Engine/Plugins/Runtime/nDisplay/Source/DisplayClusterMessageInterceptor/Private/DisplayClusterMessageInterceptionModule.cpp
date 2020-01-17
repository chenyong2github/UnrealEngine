// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Cluster/IDisplayClusterClusterSyncObject.h"
#include "DisplayClusterGameEngine.h"
#include "DisplayClusterMessageInterceptor.h"
#include "DisplayClusterMessageInterceptionSettings.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "IDisplayCluster.h"
#include "IMessageBus.h"
#include "IMessagingModule.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif 

#define LOCTEXT_NAMESPACE "DisplayClusterInterception"


namespace DisplayClusterInterceptionModuleUtils
{
	static const FString MessageInterceptionSetupEventCategory = TEXT("nDCISetup");
	static const FString MessageInterceptionSetupEventParameterSettings = TEXT("Settings");
}

/**
 * Display Cluster Message Interceptor module
 * Intercept a specified set of message bus messages that are received across all the display nodes
 * to process them in sync across the cluster.
 */
class FDisplayClusterMessageInterceptionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Register for Cluster StartSession callback so everything is setup before launching interception
		IDisplayCluster::Get().OnDisplayClusterStartSession().AddRaw(this, &FDisplayClusterMessageInterceptionModule::OnDisplayClusterStartSession);

		// Setup console command to start/stop interception
		StartMessageSyncCommand = MakeUnique<FAutoConsoleCommand>(
			TEXT("nDisplay.MessageBusSync.Start"),
			TEXT("Start MessageBus syncing"),
			FConsoleCommandDelegate::CreateRaw(this, &FDisplayClusterMessageInterceptionModule::StartInterception)
			);
		StopMessageSyncCommand = MakeUnique<FAutoConsoleCommand>(
			TEXT("nDisplay.MessageBusSync.Stop"),
			TEXT("Stop MessageBus syncing"),
			FConsoleCommandDelegate::CreateRaw(this, &FDisplayClusterMessageInterceptionModule::StopInterception)
			);

#if WITH_EDITOR
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings(
				"Project", "Plugins", "nDisplay Message Interception",
				LOCTEXT("InterceptionSettingsName", "nDisplay Message Interception"),
				LOCTEXT("InterceptionSettingsDescription", "Configure nDisplay Message Interception."),
				GetMutableDefault<UDisplayClusterMessageInterceptionSettings>()
			);
		}
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "nDisplay Message Interception");
		}
#endif

		if (IDisplayCluster::IsAvailable())
		{
			// Unregister cluster event listening
			IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
			if (ClusterManager && ListenerDelegate.IsBound())
			{
				ClusterManager->RemoveClusterEventListener(ListenerDelegate);
				ListenerDelegate.Unbind();
			}

			// Unregister cluster session events
			IDisplayCluster::Get().OnDisplayClusterStartSession().RemoveAll(this);
			IDisplayCluster::Get().OnDisplayClusterEndSession().RemoveAll(this);
			IDisplayCluster::Get().OnDisplayClusterPreTick().RemoveAll(this);
		}
		Interceptor.Reset();
		StartMessageSyncCommand.Reset();
		StopMessageSyncCommand.Reset();
	}

private:
	void OnDisplayClusterStartSession()
	{
		if (IDisplayCluster::IsAvailable() && IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster)
		{
			// Create the message interceptor only when we're in cluster mode
			Interceptor = MakeShared<FDisplayClusterMessageInterceptor, ESPMode::ThreadSafe>();

			// Register cluster event listeners
			IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
			if (ClusterManager && !ListenerDelegate.IsBound())
			{
				ListenerDelegate = FOnClusterEventListener::CreateRaw(this, &FDisplayClusterMessageInterceptionModule::HandleClusterEvent);
				ClusterManager->AddClusterEventListener(ListenerDelegate);

				//Master will send out its interceptor settings to the cluster so everyone uses the same things
				if (ClusterManager->IsMaster())
				{
					FString ExportedSettings;
					const UDisplayClusterMessageInterceptionSettings* CurrentSettings = GetDefault<UDisplayClusterMessageInterceptionSettings>();
					FMessageInterceptionSettings::StaticStruct()->ExportText(ExportedSettings, &CurrentSettings->InterceptionSettings, nullptr, nullptr, PPF_None, nullptr);
					
					FDisplayClusterClusterEvent SettingsEvent;
					SettingsEvent.Category = DisplayClusterInterceptionModuleUtils::MessageInterceptionSetupEventCategory;
					SettingsEvent.Name = ClusterManager->GetNodeId();
					SettingsEvent.Parameters.FindOrAdd(DisplayClusterInterceptionModuleUtils::MessageInterceptionSetupEventParameterSettings) = MoveTemp(ExportedSettings);

					const bool bMasterOnly = true; 
					ClusterManager->EmitClusterEvent(SettingsEvent, bMasterOnly);
				}
			}

			//Start with interception enabled
			bStartInterceptionRequested = true;

			// Register cluster session events
			IDisplayCluster::Get().OnDisplayClusterEndSession().AddRaw(this, &FDisplayClusterMessageInterceptionModule::StopInterception);
			IDisplayCluster::Get().OnDisplayClusterPreTick().AddRaw(this, &FDisplayClusterMessageInterceptionModule::HandleClusterPreTick);
		}
	}

	void HandleClusterEvent(const FDisplayClusterClusterEvent& InEvent)
	{
		if (InEvent.Category == DisplayClusterInterceptionModuleUtils::MessageInterceptionSetupEventCategory)
		{
			HandleMessageInterceptorSetupEvent(InEvent);
		}
		else
		{
			//All events except our settings synchronization one are passed to the interceptor
			if (Interceptor)
			{
				Interceptor->HandleClusterEvent(InEvent);
			}
		}

	}

	void HandleClusterPreTick()
	{
		// StartInterception will be handled once settings synchronization is completed to ensure the same behavior across the cluster
		if (bStartInterceptionRequested)
		{
			IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
			if (Interceptor && ClusterManager)
			{
				if (bSettingsSynchronizationDone)
				{
					bStartInterceptionRequested = false;
					Interceptor->Start(IMessagingModule::Get().GetDefaultBus().ToSharedRef());
				}
			}
		}

		if (Interceptor)
		{
			Interceptor->SyncMessages();
		}
	}

	void StartInterception()
	{
		bStartInterceptionRequested = true;
	}

	void StopInterception()
	{
		if (Interceptor)
		{
			Interceptor->Stop();
		}
	}

	void HandleMessageInterceptorSetupEvent(const FDisplayClusterClusterEvent& InEvent)
	{
		if (Interceptor)
		{
			const FString& ExportedSettings = InEvent.Parameters.FindChecked(DisplayClusterInterceptionModuleUtils::MessageInterceptionSetupEventParameterSettings);
			FMessageInterceptionSettings::StaticStruct()->ImportText(*ExportedSettings, &SynchronizedSettings, nullptr, EPropertyPortFlags::PPF_None, GLog, FMessageInterceptionSettings::StaticStruct()->GetName());

			IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
			if (ClusterManager)
			{
				Interceptor->Setup(ClusterManager, SynchronizedSettings);
				bSettingsSynchronizationDone = true;

				UE_LOG(LogDisplayClusterInterception, Display, TEXT("Node '%s' received synchronization settings event"), *ClusterManager->GetNodeId());
			}
		}
	}

private:

	/** MessageBus interceptor */
	TSharedPtr<FDisplayClusterMessageInterceptor, ESPMode::ThreadSafe> Interceptor;

	/** Settings to be used synchronized around the cluster */
	FMessageInterceptionSettings SynchronizedSettings;

	/** Cluster event listener delegate */
	FOnClusterEventListener ListenerDelegate;

	/** Console commands handle. */
	TUniquePtr<FAutoConsoleCommand> StartMessageSyncCommand;
	TUniquePtr<FAutoConsoleCommand> StopMessageSyncCommand;

	/** Request to start interception. Cached to be done once synchronization is done. */
	bool bStartInterceptionRequested;

	/** Flag to check if synchronization was done */
	bool bSettingsSynchronizationDone = false;
};

#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FDisplayClusterMessageInterceptionModule, DisplayClusterMessageInterception);

