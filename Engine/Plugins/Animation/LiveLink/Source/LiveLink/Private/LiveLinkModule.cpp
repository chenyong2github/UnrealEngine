// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkModule.h"

#include "LiveLinkLogInstance.h"
#include "LiveLinkPreset.h"
#include "LiveLinkSettings.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "LiveLinkModule"

FLiveLinkClient* FLiveLinkModule::StaticLiveLinkClient = nullptr;

FLiveLinkModule::FLiveLinkModule()
	: LiveLinkClient()
	, LiveLinkMotionController(LiveLinkClient)
	, HeartbeatEmitter(MakeUnique<FLiveLinkHeartbeatEmitter>())
	, DiscoveryManager(MakeUnique<FLiveLinkMessageBusDiscoveryManager>())
	, LiveLinkDebugCommand(MakeUnique<FLiveLinkDebugCommand>(LiveLinkClient))
{
}

void FLiveLinkModule::StartupModule()
{
	FLiveLinkLogInstance::CreateInstance();
	CreateStyle();

	StaticLiveLinkClient = &LiveLinkClient;
	IModularFeatures::Get().RegisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
	LiveLinkMotionController.RegisterController();

	if (ULiveLinkPreset* Preset = GetDefault<ULiveLinkSettings>()->DefaultLiveLinkPreset.LoadSynchronous())
	{
		Preset->ApplyToClient();
	}
}

void FLiveLinkModule::ShutdownModule()
{
	HeartbeatEmitter->Exit();
	DiscoveryManager->Stop();
	LiveLinkMotionController.UnregisterController();
	
	IModularFeatures::Get().UnregisterModularFeature(FLiveLinkClient::ModularFeatureName, &LiveLinkClient);
	StaticLiveLinkClient = nullptr;

	FLiveLinkLogInstance::DestroyInstance();
}

void FLiveLinkModule::CreateStyle()
{
	static FName LiveLinkStyle(TEXT("LiveLinkCoreStyle"));
	StyleSet = MakeShared<FSlateStyleSet>(LiveLinkStyle);

	FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("LiveLink"))->GetContentDir();

	const FVector2D Icon16x16(16.0f, 16.0f);

	StyleSet->Set("LiveLinkIcon", new FSlateImageBrush((ContentDir / TEXT("LiveLink_16x")) + TEXT(".png"), Icon16x16));
}

IMPLEMENT_MODULE(FLiveLinkModule, LiveLink);

#undef LOCTEXT_NAMESPACE