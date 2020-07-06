// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "TickableEditorObject.h"
#include "MeshCardRepresentation.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Templates/UniquePtr.h"

/** Notification class for asynchronous card representation building. */
class FCardRepresentationBuildNotificationImpl
	: public FTickableEditorObject
{

public:

	FCardRepresentationBuildNotificationImpl()
	{
		LastEnableTime = 0;
	}

	/** Starts the notification. */
	void BuildStarted();

	/** Ends the notification. */
	void BuildFinished();

protected:
	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override
	{
		return ETickableTickType::Always;
	}
	virtual TStatId GetStatId() const override;

private:

	/** Tracks the last time the notification was started, used to avoid spamming. */
	double LastEnableTime;

	/** In progress message */
	TWeakPtr<SNotificationItem> NotificationPtr;
};

/** Global notification object. */
TUniquePtr<FCardRepresentationBuildNotificationImpl> GCardRepresentationBuildNotification;

void SetupCardRepresentationBuildNotification()
{
	// Create explicitly to avoid relying on static initialization order
	GCardRepresentationBuildNotification = MakeUnique<FCardRepresentationBuildNotificationImpl>();
}

void TearDownCardRepresentationBuildNotification()
{
	GCardRepresentationBuildNotification = nullptr;
}

void FCardRepresentationBuildNotificationImpl::BuildStarted()
{
	LastEnableTime = FPlatformTime::Seconds();

	// Starting a new request! Notify the UI.
	if (NotificationPtr.IsValid())
	{
		NotificationPtr.Pin()->ExpireAndFadeout();
	}
	
	FNotificationInfo Info( NSLOCTEXT("CardRepresentationBuild", "CardRepresentationBuildInProgress", "Building Card Representations") );
	Info.bFireAndForget = false;
	
	// Setting fade out and expire time to 0 as the expire message is currently very obnoxious
	Info.FadeOutDuration = 0.0f;
	Info.ExpireDuration = 0.0f;

	NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (NotificationPtr.IsValid())
	{
		NotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FCardRepresentationBuildNotificationImpl::BuildFinished()
{
	// Finished all requests! Notify the UI.
	TSharedPtr<SNotificationItem> NotificationItem = NotificationPtr.Pin();

	if (NotificationItem.IsValid())
	{
		NotificationItem->SetText( NSLOCTEXT("CardRepresentationBuild", "BuildFinished", "Finished building Card Representations") );
		NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		NotificationItem->ExpireAndFadeout();

		NotificationPtr.Reset();
	}
}

void FCardRepresentationBuildNotificationImpl::Tick(float DeltaTime) 
{
	if (GCardRepresentationAsyncQueue)
	{
		// Trigger a new notification if we are doing an async build, and we haven't displayed the notification recently
		if (GCardRepresentationAsyncQueue->GetNumOutstandingTasks() > 0
			&& !NotificationPtr.IsValid()
			&& (FPlatformTime::Seconds() - LastEnableTime) > 5)
		{
			BuildStarted();
		}
		// Disable the notification when we are no longer doing an async compile
		else if (GCardRepresentationAsyncQueue->GetNumOutstandingTasks() == 0 && NotificationPtr.IsValid())
		{
			BuildFinished();
		}
		else if (GCardRepresentationAsyncQueue->GetNumOutstandingTasks() > 0 && NotificationPtr.IsValid())
		{
			TSharedPtr<SNotificationItem> NotificationItem = NotificationPtr.Pin();

			if (NotificationItem.IsValid())
			{
				FFormatNamedArguments Args;
				Args.Add( TEXT("BuildTasks"), FText::AsNumber( GCardRepresentationAsyncQueue->GetNumOutstandingTasks() ) );
				FText ProgressMessage = FText::Format(NSLOCTEXT("CardRepresentationBuild", "CardRepresentationBuildInProgressFormat", "Building Card Representations ({BuildTasks})"), Args);

				NotificationItem->SetText( ProgressMessage );
			}
		}
	}
}

TStatId FCardRepresentationBuildNotificationImpl::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FCardRepresentationBuildNotificationImpl, STATGROUP_Tickables);
}
