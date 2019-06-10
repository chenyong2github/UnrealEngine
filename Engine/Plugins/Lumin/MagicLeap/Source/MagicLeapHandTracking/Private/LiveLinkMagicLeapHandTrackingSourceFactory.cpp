// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "LiveLinkMagicLeapHandTrackingSourceFactory.h"
#include "IMagicLeapHandTrackingPlugin.h"
#include "MagicLeapHandTracking.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"

#define LOCTEXT_NAMESPACE "MagicLeapHandTracking"

FText ULiveLinkMagicLeapHandTrackingSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceName", "Hand Tracking Source");
}

FText ULiveLinkMagicLeapHandTrackingSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceTooltip", "Hand Tracking Key Points Source");
}

ULiveLinkMagicLeapHandTrackingSourceFactory::EMenuType ULiveLinkMagicLeapHandTrackingSourceFactory::GetMenuType() const
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		if (!IMagicLeapHandTrackingPlugin::Get().IsLiveLinkSourceValid() || !LiveLinkClient.HasSourceBeenAdded(IMagicLeapHandTrackingPlugin::Get().GetLiveLinkSource()))
		{
			return EMenuType::MenuEntry;
		}
	}
	return EMenuType::Disabled;
}

TSharedPtr<ILiveLinkSource> ULiveLinkMagicLeapHandTrackingSourceFactory::CreateSource(const FString& ConnectionString) const
{
	return IMagicLeapHandTrackingPlugin::Get().GetLiveLinkSource();
}

#undef LOCTEXT_NAMESPACE
