// Copyright Epic Games, Inc. All Rights Reserved.

#include "ILiveLinkModule.h"

#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "LiveLinkMotionController.h"

#include "LiveLinkMessageBusDiscoveryManager.h"

#include "LiveLinkClient.h"
#include "LiveLinkLogInstance.h"
#include "LiveLinkDebugCommand.h"
#include "LiveLinkHeartbeatEmitter.h"
#include "LiveLinkPreset.h"
#include "LiveLinkSettings.h"

#include "Styling/SlateStyle.h"


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
	TSharedPtr<FSlateStyleSet> StyleSet;
	FString ContentDir;

	FLiveLinkModule()
		: LiveLinkClient()
		, LiveLinkMotionController(LiveLinkClient)
		, HeartbeatEmitter(MakeUnique<FLiveLinkHeartbeatEmitter>())
		, DiscoveryManager(MakeUnique<FLiveLinkMessageBusDiscoveryManager>())
		, LiveLinkDebugCommand(MakeUnique<FLiveLinkDebugCommand>(LiveLinkClient))
	{
		CreateStyle();
	}

	// IModuleInterface interface

	virtual void StartupModule() override
	{
		FLiveLinkLogInstance::CreateInstance();
		IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
		LiveLinkMotionController.RegisterController();

		if (ULiveLinkPreset* Preset = GetDefault<ULiveLinkSettings>()->DefaultLiveLinkPreset.LoadSynchronous())
		{
			Preset->ApplyToClient();
		}
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

	virtual TSharedPtr<FSlateStyleSet> GetStyle() override
	{
		return StyleSet;
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
	void CreateStyle()
	{
		static FName LiveLinkStyle(TEXT("LiveLinkCoreStyle"));
		StyleSet = MakeShared<FSlateStyleSet>(LiveLinkStyle);

		ContentDir = IPluginManager::Get().FindPlugin(TEXT("LiveLink"))->GetContentDir();

		const FVector2D Icon16x16(16.0f, 16.0f);

		StyleSet->Set("LiveLinkIcon", new FSlateImageBrush((ContentDir / TEXT("LiveLink_16x")) + TEXT(".png"), Icon16x16));
	}

private:
	TUniquePtr<FLiveLinkHeartbeatEmitter> HeartbeatEmitter;
	TUniquePtr<FLiveLinkMessageBusDiscoveryManager> DiscoveryManager;

	/** Handler for LiveLink debug command. */
	TUniquePtr<FLiveLinkDebugCommand> LiveLinkDebugCommand;
};

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE