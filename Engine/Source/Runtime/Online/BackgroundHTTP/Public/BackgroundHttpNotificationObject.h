// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class BACKGROUNDHTTP_API FBackgroundHttpNotificationObject
	: public TSharedFromThis<FBackgroundHttpNotificationObject, ESPMode::ThreadSafe>
{
public:
	FBackgroundHttpNotificationObject(FText InNotificationTitle, FText InNotificationAction, FText InNotificationBody, const FString& InNotificationActivationString, bool InNotifyOnlyOnFullSuccess);
	~FBackgroundHttpNotificationObject();

	void NotifyOfDownloadResult(bool bWasSuccess);

private:
	FText NotificationTitle;
	FText NotificationAction;
	FText NotificationBody;
	FString NotificationActivationString;

	bool bNotifyOnlyOnFullSuccess;
	volatile int32 NumFailedDownloads;

	class ILocalNotificationService* PlatformNotificationService;

	//No default constructor
	FBackgroundHttpNotificationObject() {}
};

typedef TSharedPtr<FBackgroundHttpNotificationObject, ESPMode::ThreadSafe> FBackgroundHttpNotificationObjectPtr;
