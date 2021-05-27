// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStatsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

TSharedPtr<FUniqueNetIdEOSPlus> FOnlineStatsEOSPlus::GetNetIdPlus(const FString& SourceId)
{
	return EOSPlus->UserInterfacePtr->GetNetIdPlus(SourceId);
}

void FOnlineStatsEOSPlus::QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TSharedRef<const FUniqueNetId> StatsUser, const FOnlineStatsQueryUserStatsComplete& Delegate)
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->QueryStats(LocalUserId, StatsUser, Delegate);
	}
}

void FOnlineStatsEOSPlus::QueryStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& StatUsers, const TArray<FString>& StatNames, const FOnlineStatsQueryUsersStatsComplete& Delegate)
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->QueryStats(LocalUserId, StatUsers, StatNames, Delegate);
	}
}

TSharedPtr<const FOnlineStatsUserStats> FOnlineStatsEOSPlus::GetStats(const TSharedRef<const FUniqueNetId> StatsUserId) const
{
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		return Stats->GetStats(StatsUserId);
	}
	return nullptr;
}

FOnlineStatsUpdateStatsComplete IgnoredStatsComplete;

void FOnlineStatsEOSPlus::UpdateStats(const TSharedRef<const FUniqueNetId> LocalUserId, const TArray<FOnlineStatsUserUpdatedStats>& UpdatedUserStats, const FOnlineStatsUpdateStatsComplete& Delegate)
{
	TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(LocalUserId->ToString());
	if (NetIdPlus.IsValid())
	{
		// This one is the one that will fire the delegate upon completion
		IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
		if (Stats.IsValid())
		{
			Stats->UpdateStats(LocalUserId, UpdatedUserStats, Delegate);
		}
		if (GetDefault<UEOSSettings>()->bMirrorStatsToEOS)
		{
			// Also write the data to EOS
			IOnlineStatsPtr EOSStats = EOSPlus->EosOSS->GetStatsInterface();
			if (EOSStats.IsValid())
			{
				EOSStats->UpdateStats(LocalUserId, UpdatedUserStats, IgnoredStatsComplete);
			}
		}
	}
}

#if !UE_BUILD_SHIPPING
void FOnlineStatsEOSPlus::ResetStats(const TSharedRef<const FUniqueNetId> StatsUserId)
{
	// Only need to forward to the base since EOS doesn't support this
	IOnlineStatsPtr Stats = EOSPlus->BaseOSS->GetStatsInterface();
	if (Stats.IsValid())
	{
		Stats->ResetStats(StatsUserId);
	}
}
#endif

bool FOnlineStatsEOSPlus::TriggerEvent(const FUniqueNetId& PlayerId, const TCHAR* EventName, const FOnlineEventParms& Parms)
{
	IOnlineEventsPtr Events = EOSPlus->BaseOSS->GetEventsInterface();
	if (Events.IsValid())
	{
		TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(PlayerId.ToString());
		if (NetIdPlus.IsValid())
		{
			return Events->TriggerEvent(*NetIdPlus->GetBaseNetId(), EventName, Parms);
		}
	}

	return false;
}

void FOnlineStatsEOSPlus::SetPlayerSessionId(const FUniqueNetId& PlayerId, const FGuid& PlayerSessionId)
{
	IOnlineEventsPtr Events = EOSPlus->BaseOSS->GetEventsInterface();
	if (Events.IsValid())
	{
		TSharedPtr<FUniqueNetIdEOSPlus> NetIdPlus = GetNetIdPlus(PlayerId.ToString());
		if (NetIdPlus.IsValid())
		{
			Events->SetPlayerSessionId(*NetIdPlus->GetBaseNetId(), PlayerSessionId);
		}
	}
}
