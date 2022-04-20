// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConcertHeaderRowUtils.h"
#include "MultiUserServerUserSettings.generated.h"

/**
 * 
 */
UCLASS(Config = MultiUserServerUserSettings, DefaultConfig)
class MULTIUSERSERVER_API UMultiUserServerUserSettings : public UObject
{
	GENERATED_BODY()
public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnColumnVisibilitySnapshotChanged, const FColumnVisibilitySnapshot& /*NewValue*/);

	UMultiUserServerUserSettings();
	
	static UMultiUserServerUserSettings* GetUserSettings();

	const FColumnVisibilitySnapshot& GetSessionBrowserColumnVisibility() const { return SessionBrowserColumnVisibility; }
	void SetSessionBrowserColumnVisibility(FColumnVisibilitySnapshot NewValue) { SessionBrowserColumnVisibility = MoveTemp(NewValue); OnSessionBrowserColumnVisibilityChangedEvent.Broadcast(SessionBrowserColumnVisibility); }
	FOnColumnVisibilitySnapshotChanged& OnSessionBrowserColumnVisibilityChanged() { return OnSessionBrowserColumnVisibilityChangedEvent; }

	const FColumnVisibilitySnapshot& GetArchivedActivityBrowserColumnVisibility() const { return ArchivedActivityBrowserColumnVisibility; }
	void SetArchivedActivityBrowserColumnVisibility(FColumnVisibilitySnapshot NewValue) { ArchivedActivityBrowserColumnVisibility = MoveTemp(NewValue); OnArchivedActivityBrowserColumnVisibilityEvent.Broadcast(ArchivedActivityBrowserColumnVisibility); }
	FOnColumnVisibilitySnapshotChanged& OnArchivedActivityBrowserColumnVisibility() { return OnArchivedActivityBrowserColumnVisibilityEvent; }

	const FColumnVisibilitySnapshot& GetLiveActivityBrowserColumnVisibility() const { return LiveActivityBrowserColumnVisibility; }
	void SetLiveActivityBrowserColumnVisibility(FColumnVisibilitySnapshot NewValue) { LiveActivityBrowserColumnVisibility = MoveTemp(NewValue); OnLiveActivityBrowserColumnVisibilityEvent.Broadcast(LiveActivityBrowserColumnVisibility); }
	FOnColumnVisibilitySnapshotChanged& OnLiveActivityBrowserColumnVisibility() { return OnLiveActivityBrowserColumnVisibilityEvent; }

	const FColumnVisibilitySnapshot& GetLiveSessionContentColumnVisibility() const { return LiveSessionContentColumnVisibility; }
	void SetLiveSessionContentColumnVisibility(FColumnVisibilitySnapshot NewValue) { LiveSessionContentColumnVisibility = MoveTemp(NewValue); OnLiveSessionContentColumnVisibilityEvent.Broadcast(LiveSessionContentColumnVisibility); }
	FOnColumnVisibilitySnapshotChanged& OnLiveSessionContentColumnVisibility() { return OnLiveSessionContentColumnVisibilityEvent; }
	
private:
	
	UPROPERTY(Config)
	FColumnVisibilitySnapshot SessionBrowserColumnVisibility;
	FOnColumnVisibilitySnapshotChanged OnSessionBrowserColumnVisibilityChangedEvent;

	UPROPERTY(Config)
	FColumnVisibilitySnapshot ArchivedActivityBrowserColumnVisibility;
	FOnColumnVisibilitySnapshotChanged OnArchivedActivityBrowserColumnVisibilityEvent;

	UPROPERTY(Config)
	FColumnVisibilitySnapshot LiveActivityBrowserColumnVisibility;
	FOnColumnVisibilitySnapshotChanged OnLiveActivityBrowserColumnVisibilityEvent;

	UPROPERTY(Config)
	FColumnVisibilitySnapshot LiveSessionContentColumnVisibility;
	FOnColumnVisibilitySnapshotChanged OnLiveSessionContentColumnVisibilityEvent;
};
