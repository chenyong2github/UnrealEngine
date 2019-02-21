// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BackgroundHttpNotificationObject.h"

#include "Misc/ConfigCacheIni.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "LocalNotification.h"

FBackgroundHttpNotificationObject::FBackgroundHttpNotificationObject(FText InNotificationTitle, FText InNotificationBody, FText InNotificationAction, const FString& InNotificationActivationString, bool InNotifyOnlyOnFullSuccess)
	: NotificationTitle(InNotificationTitle)
    , NotificationAction(InNotificationAction)
    , NotificationBody(InNotificationBody)
	, NotificationActivationString(InNotificationActivationString)
	, bNotifyOnlyOnFullSuccess(InNotifyOnlyOnFullSuccess)
	, NumFailedDownloads(0)
    , PlatformNotificationService(nullptr)
{
	if (GConfig)
	{
		FString ModuleName;
		GConfig->GetString(TEXT("LocalNotification"), TEXT("DefaultPlatformService"), ModuleName, GEngineIni);

		if (ModuleName.Len() > 0)
		{
			// load the module by name from the .ini
			if (ILocalNotificationModule* Module = FModuleManager::LoadModulePtr<ILocalNotificationModule>(*ModuleName))
			{
				PlatformNotificationService = Module->GetLocalNotificationService();
			}
		}
	}
}

FBackgroundHttpNotificationObject::~FBackgroundHttpNotificationObject()
{
	if (nullptr != PlatformNotificationService)
	{
		if (!bNotifyOnlyOnFullSuccess || (NumFailedDownloads == 0))
		{
			//make a notification 1 second from now
			FDateTime TargetTime = FDateTime::Now();
			TargetTime += FTimespan::FromSeconds(1);

			if (nullptr != PlatformNotificationService)
			{
				PlatformNotificationService->ScheduleLocalNotificationAtTime(TargetTime, true, NotificationTitle, NotificationBody, NotificationAction, NotificationActivationString);
			}
		}
	}
}

void FBackgroundHttpNotificationObject::NotifyOfDownloadResult(bool bWasSuccess)
{
	if (!bWasSuccess)
	{
		FPlatformAtomics::InterlockedIncrement(&NumFailedDownloads);
	}
}
