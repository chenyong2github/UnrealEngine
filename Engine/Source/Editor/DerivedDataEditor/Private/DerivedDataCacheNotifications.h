// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "UObject/WeakObjectPtr.h"
#include "DerivedDataCacheInterface.h"
#include "IDerivedDataCacheNotifications.h"

class SNotificationItem;

class FDerivedDataCacheNotifications : public IDerivedDataCacheNotifications
{
public:
	FDerivedDataCacheNotifications();
	virtual ~FDerivedDataCacheNotifications();

private: 

	/** DDC data put notification handler */
	void OnDDCNotificationEvent(FDerivedDataCacheInterface::EDDCNotification DDCNotification);
	
	/** Manually clear any presented DDC notifications */
	void ClearSharedDDCNotification();

	/** Subscribe to the notifactions */
	void Subscribe(bool bSubscribe);

	/** Whether or not to show the Shared DDC notification */
	bool bShowSharedDDCNotification;

	/** Whether we are subscribed or not **/
	bool bSubscribed;

	/** Valid when a DDC notification item is being presented */
	TSharedPtr<SNotificationItem> SharedDDCNotification;
};

