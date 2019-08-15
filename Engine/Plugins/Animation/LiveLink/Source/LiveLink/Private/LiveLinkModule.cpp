// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkModule.h"

#include "Features/IModularFeatures.h"
#include "LiveLinkMotionController.h"

#include "LiveLinkMessageBusDiscoveryManager.h"

#include "LiveLinkClient.h"

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
	{}

	// IModuleInterface interface

	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		LiveLinkMotionController.RegisterController();
	}

	virtual void ShutdownModule() override
	{
		HeartbeatEmitter->Exit();
		DiscoveryManager->Stop();
		LiveLinkMotionController.UnregisterController();
		IModularFeatures::Get().UnregisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
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
};

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE