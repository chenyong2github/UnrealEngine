// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineStatsInterface.h"
#include "Interfaces/OnlineEventsInterface.h"

class FOnlineSubsystemEOSPlus;
class FUniqueNetIdEOSPlus;

/**
 * Interface that mirrors stats on both OSSes
 */
class FOnlineStatsEOSPlus :
	public IOnlineStats,
	public IOnlineEvents
{
public:
	FOnlineStatsEOSPlus() = delete;
	virtual ~FOnlineStatsEOSPlus() = default;

// IOnlineStats Interface
	virtual void QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TSharedRef<const FUniqueNetId> StatsUser, const FOnlineStatsQueryUserStatsComplete& Delegate) override;
	virtual void QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& StatUsers, const TArray<FString>& StatNames, const FOnlineStatsQueryUsersStatsComplete& Delegate) override;
	virtual TSharedPtr<const FOnlineStatsUserStats> GetStats(const TSharedRef<const FUniqueNetId> StatsUserId) const override;
	virtual void UpdateStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<FOnlineStatsUserUpdatedStats>& UpdatedUserStats, const FOnlineStatsUpdateStatsComplete& Delegate) override;
#if !UE_BUILD_SHIPPING
	virtual void ResetStats(const TSharedRef<const FUniqueNetId> StatsUserId) override;
#endif
// ~IOnlineStats Interface

// IOnlineEvents Interface
	virtual bool TriggerEvent(const FUniqueNetId& PlayerId, const TCHAR* EventName, const FOnlineEventParms& Parms) override;
	virtual void SetPlayerSessionId(const FUniqueNetId& PlayerId, const FGuid& PlayerSessionId) override;
// ~IOnlineEvents Interface

PACKAGE_SCOPE:
	FOnlineStatsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
		: EOSPlus(InSubsystem)
	{
	}

private:
	TSharedPtr<FUniqueNetIdEOSPlus> GetNetIdPlus(const FString& SourceId);

private:
	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;
};

typedef TSharedPtr<FOnlineStatsEOSPlus, ESPMode::ThreadSafe> FOnlineStatsEOSPlusPtr;
