// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineLeaderboardsEOSPlus.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemEOSPlus.h"
#include "EOSSettings.h"

FOnlineLeaderboardsEOSPlus::FOnlineLeaderboardsEOSPlus(FOnlineSubsystemEOSPlus* InSubsystem)
	: EOSPlus(InSubsystem)
{
	EosLeaderboardsInterface = EOSPlus->EosOSS->GetLeaderboardsInterface();
	check(EosLeaderboardsInterface.IsValid());

	BaseLeaderboardsInterface = EOSPlus->BaseOSS->GetLeaderboardsInterface();
	check(BaseLeaderboardsInterface.IsValid());
}

FOnlineLeaderboardsEOSPlus::~FOnlineLeaderboardsEOSPlus()
{

}

TSharedPtr<FUniqueNetIdEOSPlus> FOnlineLeaderboardsEOSPlus::GetNetIdPlus(const FString& SourceId)
{
	return EOSPlus->UserInterfacePtr->GetNetIdPlus(SourceId);
}

bool FOnlineLeaderboardsEOSPlus::ReadLeaderboards(const TArray< FUniqueNetIdRef >& Players, FOnlineLeaderboardReadRef& ReadObject)
{
	return true;
}

bool FOnlineLeaderboardsEOSPlus::ReadLeaderboardsForFriends(int32 LocalUserNum, FOnlineLeaderboardReadRef& ReadObject)
{
	return true;
}

bool FOnlineLeaderboardsEOSPlus::ReadLeaderboardsAroundRank(int32 Rank, uint32 Range, FOnlineLeaderboardReadRef& ReadObject)
{
	return true;
}

bool FOnlineLeaderboardsEOSPlus::ReadLeaderboardsAroundUser(FUniqueNetIdRef Player, uint32 Range, FOnlineLeaderboardReadRef& ReadObject)
{
	return true;
}

void FOnlineLeaderboardsEOSPlus::FreeStats(FOnlineLeaderboardRead& ReadObject)
{

}

bool FOnlineLeaderboardsEOSPlus::WriteLeaderboards(const FName& SessionName, const FUniqueNetId& Player, FOnlineLeaderboardWrite& WriteObject)
{
	return true;
}

bool FOnlineLeaderboardsEOSPlus::FlushLeaderboards(const FName& SessionName)
{
	return true;
}

bool FOnlineLeaderboardsEOSPlus::WriteOnlinePlayerRatings(const FName& SessionName, int32 LeaderboardId, const TArray<FOnlinePlayerScore>& PlayerScores)
{
	return true;
}