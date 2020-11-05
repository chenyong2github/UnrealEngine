// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlinePresenceInterface.h"

class FOnlineSubsystemEOSPlus;

/**
 * Interface that handles friends from both OSSes
 */
class FOnlinePresenceEOSPlus :
	public IOnlinePresence
{
public:
	FOnlinePresenceEOSPlus() = delete;
	virtual ~FOnlinePresenceEOSPlus();

// IOnlinePresence Interface
	virtual void SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;
	virtual void QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;
	virtual EOnlineCachedResult::Type GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence) override;
	virtual EOnlineCachedResult::Type GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence) override;
// ~IOnlinePresence Interface

PACKAGE_SCOPE:
	FOnlinePresenceEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem);

	// Delegates to rebroadcast things back out
	void OnPresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence);
	void OnPresenceArrayUpdated(const FUniqueNetId& UserId, const TArray<TSharedRef<FOnlineUserPresence>>& NewPresenceArray);

private:
	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;
	/** Since we're going to bind to delegates, we need to hold onto these */
	IOnlinePresencePtr BasePresenceInterface;
	IOnlinePresencePtr EOSPresenceInterface;
};

typedef TSharedPtr<FOnlinePresenceEOSPlus, ESPMode::ThreadSafe> FOnlinePresenceEOSPlusPtr;
