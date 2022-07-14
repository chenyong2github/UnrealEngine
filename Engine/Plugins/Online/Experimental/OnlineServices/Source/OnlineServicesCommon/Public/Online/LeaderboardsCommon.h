// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Leaderboards.h"
#include "Online/OnlineComponent.h"

namespace UE::Online {

enum class ELeaderboardUpdateMethod : uint8
{
	/** If current leaderboard score is better than the uploaded one, keep the current one */
	KeepBest,
	/** Leaderboard score is always replaced with uploaded value */
	Force
};

const TCHAR* LexToString(ELeaderboardUpdateMethod Value);
void LexFromString(ELeaderboardUpdateMethod& OutValue, const TCHAR* InStr);

enum ELeaderboardOrderMethod : uint8
{
	Ascending,
	Descending
};

const TCHAR* LexToString(ELeaderboardOrderMethod Value);
void LexFromString(ELeaderboardOrderMethod& OutValue, const TCHAR* InStr);

struct FLeaderboardDefinition
{
	/* The name of the leaderboard */
	FString Name;
	/* Corresponding leaderboard id on the platform if needed */
	int32 Id = 0;

	/* How the leaderboard score will be updated */
	ELeaderboardUpdateMethod LeaderboardUpdateMethod = ELeaderboardUpdateMethod::Force;
	/* How the leaderboard score will be ordered */
	ELeaderboardOrderMethod LeaderboardOrderMethod = ELeaderboardOrderMethod::Descending;
};

struct FWriteLeaderboardScores
{
	static constexpr TCHAR Name[] = TEXT("WriteLeaderboardScores");

	struct Params
	{
		FOnlineAccountIdHandle LocalUserId;
		FString BoardName;
		uint64 Score;
	};

	struct Result
	{
	};
};

class ONLINESERVICESCOMMON_API FLeaderboardsCommon : public TOnlineComponent<ILeaderboards>
{
public:
	using Super = TOnlineComponent<ILeaderboards>;

	FLeaderboardsCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void LoadConfig() override;
	virtual void RegisterCommands() override;

	// ILeaderboards
	virtual TOnlineAsyncOpHandle<FReadEntriesForUsers> ReadEntriesForUsers(FReadEntriesForUsers::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FReadEntriesAroundRank> ReadEntriesAroundRank(FReadEntriesAroundRank::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FReadEntriesAroundUser> ReadEntriesAroundUser(FReadEntriesAroundUser::Params&& Params) override;

	// Internal use only, for stats common implementation
	virtual TOnlineAsyncOpHandle<FWriteLeaderboardScores> WriteLeaderboardScores(FWriteLeaderboardScores::Params&& Params);

protected:
	TMap<FString, FLeaderboardDefinition> LeaderboardDefinitions;
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FWriteLeaderboardScores::Params)
	ONLINE_STRUCT_FIELD(FWriteLeaderboardScores::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FWriteLeaderboardScores::Params, BoardName),
	ONLINE_STRUCT_FIELD(FWriteLeaderboardScores::Params, Score)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FWriteLeaderboardScores::Result)
END_ONLINE_STRUCT_META()

/* Meta*/ }

/* UE::Online */ }
