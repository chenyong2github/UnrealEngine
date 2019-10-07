// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkModule.h"

#include "Features/IModularFeatures.h"
#include "LiveLinkMotionController.h"

#include "LiveLinkMessageBusDiscoveryManager.h"

#include "LiveLinkClient.h"
#include "LiveLinkLogInstance.h"
#include "LiveLinkDebugCommand.h"
#include "LiveLinkHeartbeatEmitter.h"

/**
 * Implements the Messaging module.
 */

#define LOCTEXT_NAMESPACE "LiveLinkModule"


class FLiveLinkModule
	: public ILiveLinkModule
{
public:
	FLiveLinkClient LiveLinkClient;
	FLiveLinkMotionController LiveLinkMotionController;

	FLiveLinkModule()
		: LiveLinkClient()
		, LiveLinkMotionController(LiveLinkClient)
		, HeartbeatEmitter(MakeUnique<FLiveLinkHeartbeatEmitter>())
		, DiscoveryManager(MakeUnique<FLiveLinkMessageBusDiscoveryManager>())
		, LiveLinkDebugCommand(MakeUnique<FLiveLinkDebugCommand>(LiveLinkClient))
	{}

	// IModuleInterface interface

	virtual void StartupModule() override
	{
		FLiveLinkLogInstance::CreateInstance();
		IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		LiveLinkMotionController.RegisterController();
	}

	virtual void ShutdownModule() override
	{
		HeartbeatEmitter->Exit();
		DiscoveryManager->Stop();
		LiveLinkMotionController.UnregisterController();
		IModularFeatures::Get().UnregisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		FLiveLinkLogInstance::DestroyInstance();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	virtual FLiveLinkHeartbeatEmitter& GetHeartbeatEmitter() override
	{
		return *HeartbeatEmitter;
	}

	virtual FLiveLinkMessageBusDiscoveryManager& GetMessageBusDiscoveryManager() override
	{
		return *DiscoveryManager;
	}

private:
	TUniquePtr<FLiveLinkHeartbeatEmitter> HeartbeatEmitter;
	TUniquePtr<FLiveLinkMessageBusDiscoveryManager> DiscoveryManager;

	/** Handler for LiveLink debug command. */
	TUniquePtr<FLiveLinkDebugCommand> LiveLinkDebugCommand;
};

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE