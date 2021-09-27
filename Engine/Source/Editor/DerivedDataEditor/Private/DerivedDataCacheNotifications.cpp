// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheNotifications.h"
#include "Misc/EngineBuildSettings.h"
#include "Logging/LogMacros.h"
#include "Misc/CoreMisc.h"
#include "Misc/App.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SHyperlink.h"
#include "Framework/Notifications/NotificationManager.h"
#include "DerivedDataCacheInterface.h"
#include "Editor/EditorPerformanceSettings.h"

#define LOCTEXT_NAMESPACE "DerivedDataCacheNotifications"

DEFINE_LOG_CATEGORY_STATIC(DerivedDataCacheNotifications, Log, All);

FDerivedDataCacheNotifications::FDerivedDataCacheNotifications() :
	bShowSharedDDCNotification(false),
	bSubscribed(false)
{
	Subscribe(true);
}

FDerivedDataCacheNotifications::~FDerivedDataCacheNotifications()
{
	Subscribe(false);
}

void FDerivedDataCacheNotifications::ClearSharedDDCNotification()
{
	// Don't call back into slate if already exiting
	if (IsEngineExitRequested())
	{
		return;
	}

	if (SharedDDCNotification.IsValid())
	{
		SharedDDCNotification.Get()->SetCompletionState(SNotificationItem::CS_None);
		SharedDDCNotification.Get()->ExpireAndFadeout();
		SharedDDCNotification.Reset();
	}

}

void FDerivedDataCacheNotifications::OnDDCNotificationEvent(FDerivedDataCacheInterface::EDDCNotification DDCNotification)
{
	// Early out if we have turned off notifications
	if (!GetDefault<UEditorPerformanceSettings>()->bEnableSharedDDCPerformanceNotifications)
	{
		return;
	}

	if (!bShowSharedDDCNotification || DDCNotification != FDerivedDataCacheInterface::SharedDDCPerformanceNotification)
	{
		return;
	}

	// Only show DDC notifications once per session
	bShowSharedDDCNotification = false;

	FNotificationInfo Info(NSLOCTEXT("SharedDDCNotification", "SharedDDCNotificationMessage", "Shared Data Cache not in use, performance is impacted."));
	Info.bFireAndForget = false;
	Info.bUseThrobber = false;
	Info.FadeOutDuration = 0.0f;
	Info.ExpireDuration = 0.0f;

	Info.Hyperlink = FSimpleDelegate::CreateLambda([this]() {
		const FString DerivedDataCacheUrl = TEXT("https://docs.unrealengine.com/latest/INT/Engine/Basics/DerivedDataCache/");
		ClearSharedDDCNotification();
		FPlatformProcess::LaunchURL(*DerivedDataCacheUrl, nullptr, nullptr);
	});

	Info.HyperlinkText = LOCTEXT("SharedDDCNotificationHyperlink", "View Shared Data Cache Documentation");
		
	Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("SharedDDCNotificationDismiss", "Dismiss"), FText(), FSimpleDelegate::CreateLambda([this]() {
		ClearSharedDDCNotification();
	})));
			
	SharedDDCNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (SharedDDCNotification.IsValid())
	{
		SharedDDCNotification.Get()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FDerivedDataCacheNotifications::Subscribe(bool bSubscribe)
{
	if (bSubscribe != bSubscribed)
	{
		FDerivedDataCacheInterface::FOnDDCNotification& DDCNotificationEvent = GetDerivedDataCacheRef().GetDDCNotificationEvent();

		if (bSubscribe)
		{
			DDCNotificationEvent.AddRaw(this, &FDerivedDataCacheNotifications::OnDDCNotificationEvent);
		}
		else
		{
			DDCNotificationEvent.RemoveAll(this);
		}

		bSubscribed = bSubscribe;
	}
}

#undef LOCTEXT_NAMESPACE
