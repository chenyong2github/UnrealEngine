// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkWindowsMixedRealityHandTrackingSourceFactory.h"
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "WindowsMixedRealityHandTracking.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"

#define LOCTEXT_NAMESPACE "WindowsMixedRealityHandTracking"

FText ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::GetSourceDisplayName() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceName", "Windows Mixed Reality Hand Tracking Source");
}

FText ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::GetSourceTooltip() const
{
	return LOCTEXT("HandTrackingLiveLinkSourceTooltip", "Windows Mixed Reality Hand Tracking Key Points Source");
}

ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::EMenuType ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::GetMenuType() const
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		if (!IWindowsMixedRealityHandTrackingModule::Get().IsLiveLinkSourceValid() || !LiveLinkClient.HasSourceBeenAdded(IWindowsMixedRealityHandTrackingModule::Get().GetLiveLinkSource()))
		{
			return EMenuType::MenuEntry;
		}
	}
	return EMenuType::Disabled;
}

TSharedPtr<ILiveLinkSource> ULiveLinkWindowsMixedRealityHandTrackingSourceFactory::CreateSource(const FString& ConnectionString) const
{
	return IWindowsMixedRealityHandTrackingModule::Get().GetLiveLinkSource();
}

#undef LOCTEXT_NAMESPACE