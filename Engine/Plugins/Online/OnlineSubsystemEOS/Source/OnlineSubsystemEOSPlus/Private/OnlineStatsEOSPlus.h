// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "Interfaces/OnlineStatsInterface.h"

class FOnlineSubsystemEOSPlus;

/**
 * Interface that mirrors stats on both OSSes
 */
class FOnlineStatsEOSPlus :
	public IOnlineStats
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

PACKAGE_SCOPE:
	FOnlineStatsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
		: EOSPlus(InSubsystem)
	{
	}

private:
	/** Reference to the owning EOS plus subsystem */
	FOnlineSubsystemEOSPlus* EOSPlus;
};

typedef TSharedPtr<FOnlineStatsEOSPlus, ESPMode::ThreadSafe> FOnlineStatsEOSPlusPtr;
