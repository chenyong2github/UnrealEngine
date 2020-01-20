// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkClientReference.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"

FLiveLinkClientReference::FLiveLinkClientReference()
	: LiveLinkClient(nullptr)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FLiveLinkClientReference::OnLiveLinkClientRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FLiveLinkClientReference::OnLiveLinkClientUnregistered);

	InitClient();
}

FLiveLinkClientReference::FLiveLinkClientReference(const FLiveLinkClientReference& Other)
	: LiveLinkClient(Other.LiveLinkClient)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FLiveLinkClientReference::OnLiveLinkClientRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FLiveLinkClientReference::OnLiveLinkClientUnregistered);

	InitClient();
}

FLiveLinkClientReference& FLiveLinkClientReference::operator=(const FLiveLinkClientReference& Other)
{
	LiveLinkClient = Other.LiveLinkClient;
	return *this;
}

FLiveLinkClientReference::~FLiveLinkClientReference()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
	ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);
}

void FLiveLinkClientReference::InitClient()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}
}

void FLiveLinkClientReference::OnLiveLinkClientRegistered(const FName& Type, class IModularFeature* ModularFeature)
{
	if (Type == ILiveLinkClient::ModularFeatureName && !LiveLinkClient)
	{
		LiveLinkClient = static_cast<ILiveLinkClient*>(ModularFeature);
	}
}

void FLiveLinkClientReference::OnLiveLinkClientUnregistered(const FName& Type, class IModularFeature* ModularFeature)
{
	if (Type == ILiveLinkClient::ModularFeatureName && ModularFeature == LiveLinkClient)
	{
		LiveLinkClient = nullptr;
		InitClient();
	}
}