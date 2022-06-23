// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineMeta.h"

namespace UE::Online {

//using FStatValue = SchemaVariant;
using FStatValue = TVariant<bool, int32, int64, float, double, FString>;

struct FUserStats
{
	/* The user id */
	FOnlineAccountIdHandle UserId;
	/* The stats of one account */
	TMap<FString, FStatValue> Stats;
};

struct FUpdateStats
{
	static constexpr TCHAR Name[] = TEXT("UpdateStats");

	struct Params
	{
		/* Local user id */
		FOnlineAccountIdHandle LocalUserId;
		/* The stats of users to update */
		TArray<FUserStats> UpdateUsersStats;
	};

	struct Result
	{
	};
};

struct FQueryStats
{
	static constexpr TCHAR Name[] = TEXT("QueryStats");

	struct Params
	{
		/* Local user id */
		FOnlineAccountIdHandle LocalUserId;
		/* Target user id to query */
		FOnlineAccountIdHandle TargetUserId;
		/* The stats to query */
		TArray<FString> StatNames;
	};

	struct Result
	{
		/* The stats of queried user */
		TMap<FString, FStatValue> Stats;
	};
};

struct FBatchQueryStats
{
	static constexpr TCHAR Name[] = TEXT("BatchQueryStats");

	struct Params
	{
		/* Local user id */
		FOnlineAccountIdHandle LocalUserId;
		/* The user ids to query */
		TArray<FOnlineAccountIdHandle> TargetUserIds;
		/* The stats to query */
		TArray<FString> StatNames;
	};

	struct Result
	{
		/* The stats of queried users, there should be no much elements so simply use TArray instead of TMap */
		TArray<FUserStats> UsersStats;
	};
};

#if !UE_BUILD_SHIPPING
struct FResetStats
{
	static constexpr TCHAR Name[] = TEXT("ResetStats");

	struct Params
	{
		/* Local user id */
		FOnlineAccountIdHandle LocalUserId;
	};

	struct Result
	{
	};
};
#endif // !UE_BUILD_SHIPPING

// Stats interface is used to upload the stats to EOS or first party platforms, to complete corresponding InGame/Platform features such as Stats Query, Achievements, Leaderboard, etc.
class IStats
{
public:
	/**
	 * Upload/send stats to the backend/platform.
	 *
	 * @param Parameters for the UpdateStats call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FUpdateStats> UpdateStats(FUpdateStats::Params&& Params) = 0;
	/**
	 * Get stats of the specified user.
	 *
	 * @param Parameters for the QueryStats call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FQueryStats> QueryStats(FQueryStats::Params&& Params) = 0;
	/**
	 * Get stats of the specified users.
	 *
	 * @param Parameters for the BatchQueryStats call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FBatchQueryStats> BatchQueryStats(FBatchQueryStats::Params&& Params) = 0;

#if !UE_BUILD_SHIPPING
	/**
	 * Reset stats of the local user.
	 *
	 * @param Parameters for the ResetStats call
	 * @return
	 */
	virtual TOnlineAsyncOpHandle<FResetStats> ResetStats(FResetStats::Params&& Params) = 0;
#endif // !UE_BUILD_SHIPPING
};

namespace Meta {

BEGIN_ONLINE_STRUCT_META(FUserStats)
	ONLINE_STRUCT_FIELD(FUserStats, UserId),
	ONLINE_STRUCT_FIELD(FUserStats, Stats)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateStats::Params)
	ONLINE_STRUCT_FIELD(FUpdateStats::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FUpdateStats::Params, UpdateUsersStats)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FUpdateStats::Result)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryStats::Params)
	ONLINE_STRUCT_FIELD(FQueryStats::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FQueryStats::Params, TargetUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FQueryStats::Result)
	ONLINE_STRUCT_FIELD(FQueryStats::Result, Stats)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FBatchQueryStats::Params)
	ONLINE_STRUCT_FIELD(FBatchQueryStats::Params, LocalUserId),
	ONLINE_STRUCT_FIELD(FBatchQueryStats::Params, TargetUserIds)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FBatchQueryStats::Result)
	ONLINE_STRUCT_FIELD(FBatchQueryStats::Result, UsersStats)
END_ONLINE_STRUCT_META()

#if !UE_BUILD_SHIPPING
BEGIN_ONLINE_STRUCT_META(FResetStats::Params)
	ONLINE_STRUCT_FIELD(FResetStats::Params, LocalUserId)
END_ONLINE_STRUCT_META()

BEGIN_ONLINE_STRUCT_META(FResetStats::Result)
END_ONLINE_STRUCT_META()
#endif // !UE_BUILD_SHIPPING

/* Meta*/ }

/* UE::Online */ }
