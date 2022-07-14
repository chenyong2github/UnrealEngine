// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/LeaderboardsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

const TCHAR* LexToString(ELeaderboardUpdateMethod Value)
{
	switch (Value)
	{
	case ELeaderboardUpdateMethod::Force:
		return TEXT("Force");
	default: checkNoEntry(); // Intentional fallthrough
	case ELeaderboardUpdateMethod::KeepBest:
		return TEXT("KeepBest");
	}
}

void LexFromString(ELeaderboardUpdateMethod& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("KeepBest")) == 0)
	{
		OutValue = ELeaderboardUpdateMethod::KeepBest;
	}
	else if (FCString::Stricmp(InStr, TEXT("Force")) == 0)
	{
		OutValue = ELeaderboardUpdateMethod::Force;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to ELeaderboardUpdateMethod"), InStr);
		OutValue = ELeaderboardUpdateMethod::KeepBest;
	}
}

const TCHAR* LexToString(ELeaderboardOrderMethod Value)
{
	switch (Value)
	{
	case ELeaderboardOrderMethod::Ascending:
		return TEXT("Ascending");
	default: checkNoEntry(); // Intentional fallthrough
	case ELeaderboardOrderMethod::Descending:
		return TEXT("Descending");
	}
}

void LexFromString(ELeaderboardOrderMethod& OutValue, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Ascending")) == 0)
	{
		OutValue = ELeaderboardOrderMethod::Ascending;
	}
	else if (FCString::Stricmp(InStr, TEXT("Descending")) == 0)
	{
		OutValue = ELeaderboardOrderMethod::Descending;
	}
	else
	{
		ensureMsgf(false, TEXT("Can't convert %s to ELeaderboardOrderMethod"), InStr);
		OutValue = ELeaderboardOrderMethod::Descending;
	}
}

FLeaderboardsCommon::FLeaderboardsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Leaderboards"), InServices)
{
}

void FLeaderboardsCommon::LoadConfig()
{
	Super::LoadConfig();

	const TCHAR* ConfigSection = TEXT("OnlineServices.Leaderboards");

	for (int StatIdx = 0;; StatIdx++)
	{
		FString LeaderboardName;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("LeaderboardDef_%d_Name"), StatIdx), LeaderboardName, GEngineIni);
		if (LeaderboardName.IsEmpty())
		{
			break;
		}

		FLeaderboardDefinition& LeaderboardDefinition = LeaderboardDefinitions.Emplace(LeaderboardName);
		LeaderboardDefinition.Name = MoveTemp(LeaderboardName);

		GConfig->GetInt(ConfigSection, *FString::Printf(TEXT("LeaderboardDef_%d_Id"), StatIdx), LeaderboardDefinition.Id, GEngineIni);

		FString LeaderboardUpdateMethod;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("LeaderboardDef_%d_UpdateMethod"), StatIdx), LeaderboardUpdateMethod, GEngineIni);
		if (!LeaderboardUpdateMethod.IsEmpty())
		{
			LexFromString(LeaderboardDefinition.LeaderboardUpdateMethod, *LeaderboardUpdateMethod);
		}

		FString LeaderboardOrderMethod;
		GConfig->GetString(ConfigSection, *FString::Printf(TEXT("LeaderboardDef_%d_OrderMethod"), StatIdx), LeaderboardOrderMethod, GEngineIni);
		if (!LeaderboardOrderMethod.IsEmpty())
		{
			LexFromString(LeaderboardDefinition.LeaderboardOrderMethod, *LeaderboardOrderMethod);
		}
	}
}

void FLeaderboardsCommon::RegisterCommands()
{
	Super::RegisterCommands();

	RegisterCommand(&FLeaderboardsCommon::ReadEntriesForUsers);
	RegisterCommand(&FLeaderboardsCommon::ReadEntriesAroundRank);
	RegisterCommand(&FLeaderboardsCommon::ReadEntriesAroundUser);
}

TOnlineAsyncOpHandle<FReadEntriesForUsers> FLeaderboardsCommon::ReadEntriesForUsers(FReadEntriesForUsers::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesForUsers> Operation = GetOp<FReadEntriesForUsers>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FReadEntriesAroundRank> FLeaderboardsCommon::ReadEntriesAroundRank(FReadEntriesAroundRank::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesAroundRank> Operation = GetOp<FReadEntriesAroundRank>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FReadEntriesAroundUser> FLeaderboardsCommon::ReadEntriesAroundUser(FReadEntriesAroundUser::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesAroundUser> Operation = GetOp<FReadEntriesAroundUser>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FWriteLeaderboardScores> FLeaderboardsCommon::WriteLeaderboardScores(FWriteLeaderboardScores::Params&& Params)
{
	TOnlineAsyncOpRef<FWriteLeaderboardScores> Operation = GetOp<FWriteLeaderboardScores>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

/* UE::Online */ }
