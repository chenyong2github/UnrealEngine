// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "HAL/IConsoleManager.h"
#include "IMessagingModule.h"
#include "IMessageBus.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "DisplayClusterMessageInterceptor.h"
#include "DisplayClusterMessageInterceptionSettings.h"

#if WITH_EDITOR
	#include "ISettingsModule.h"
	#include "ISettingsSection.h"
#endif 

#define LOCTEXT_NAMESPACE "DisplayClusterInterception"


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
		// Create the message interceptor
		Interceptor = MakeShared<FDisplayClusterMessageInterceptor, ESPMode::ThreadSafe>();

		// Register cluster event listeners
		IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
		if (ClusterManager && !ListenerDelegate.IsBound())
		{
			ListenerDelegate = FOnClusterEventListener::CreateRaw(this, &FDisplayClusterMessageInterceptionModule::HandleClusterEvent);
			ClusterManager->AddClusterEventListener(ListenerDelegate);
		}

		// Register cluster session events
		IDisplayCluster::Get().OnDisplayClusterStartSession().AddRaw(this, &FDisplayClusterMessageInterceptionModule::StartInterception);
		IDisplayCluster::Get().OnDisplayClusterEndSession().AddRaw(this, &FDisplayClusterMessageInterceptionModule::StopInterception);
		IDisplayCluster::Get().OnDisplayClusterPreTick().AddRaw(this, &FDisplayClusterMessageInterceptionModule::HandleClusterPreTick);

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
	void HandleClusterEvent(const FDisplayClusterClusterEvent& InEvent)
	{
		if (Interceptor)
		{
			Interceptor->HandleClusterEvent(InEvent);
		}
	}

	void HandleClusterPreTick()
	{
		if (Interceptor)
		{
			Interceptor->SyncMessages();
		}
	}

	void StartInterception()
	{
		IDisplayClusterClusterManager* ClusterManager = IDisplayCluster::Get().GetClusterMgr();
		if (Interceptor && ClusterManager)
		{
			Interceptor->Setup(ClusterManager, IMessagingModule::Get().GetDefaultBus().ToSharedRef());
			Interceptor->Start();
		}		
	}

	void StopInterception()
	{
		if (Interceptor)
		{
			Interceptor->Stop();
			// remove internal reference to message bus
			Interceptor->Setup(nullptr, nullptr);
		}
	}

	/** MessageBus interceptor */
	TSharedPtr<FDisplayClusterMessageInterceptor, ESPMode::ThreadSafe> Interceptor;

	/** Cluster event listener delegate */
	FOnClusterEventListener ListenerDelegate;

	// Console commands handle.
	TUniquePtr<FAutoConsoleCommand> StartMessageSyncCommand;
	TUniquePtr<FAutoConsoleCommand> StopMessageSyncCommand;
};

#undef LOCTEXT_NAMESPACE


IMPLEMENT_MODULE(FDisplayClusterMessageInterceptionModule, DisplayClusterMessageInterception);

