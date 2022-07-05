// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSubsystem.h"
#include "ILiveLinkClient.h"
#include "VCamPixelStreamingLiveLink.h"
#include "VCamPixelStreamingSession.h"

void UVCamPixelStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkSource = MakeShared<FPixelStreamingLiveLinkSource>();
		LiveLinkClient->AddSource(LiveLinkSource);
	}
}

void UVCamPixelStreamingSubsystem::Deinitialize()
{
	Super::Deinitialize();
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (LiveLinkSource && ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient* LiveLinkClient = &ModularFeatures.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		LiveLinkClient->RemoveSource(LiveLinkSource);
	}
	LiveLinkSource.Reset();
}

UVCamPixelStreamingSubsystem* UVCamPixelStreamingSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UVCamPixelStreamingSubsystem>() : nullptr;
}

void UVCamPixelStreamingSubsystem::RegisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	if (!OutputProvider) return;
	
	ActiveOutputProviders.AddUnique(OutputProvider);
	if (LiveLinkSource)
	{
		LiveLinkSource->CreateSubject(OutputProvider->GetFName());
		LiveLinkSource->PushTransformForSubject(OutputProvider->GetFName(), FTransform::Identity);
	}
}

void UVCamPixelStreamingSubsystem::UnregisterActiveOutputProvider(UVCamPixelStreamingSession* OutputProvider)
{
	if (!OutputProvider) return;
	if (ActiveOutputProviders.Remove(OutputProvider) && LiveLinkSource)
	{
		LiveLinkSource->RemoveSubject(OutputProvider->GetFName());
	}
}