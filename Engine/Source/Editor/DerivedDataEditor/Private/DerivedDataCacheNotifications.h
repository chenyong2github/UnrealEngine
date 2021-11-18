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
	
	/** Subscribe to the notifactions */
	void Subscribe(bool bSubscribe);

	/** Whether we are subscribed or not **/
	bool bSubscribed;

	/** Valid when a DDC notification item is being presented */
	TSharedPtr<SNotificationItem> SharedDDCNotification;
};

