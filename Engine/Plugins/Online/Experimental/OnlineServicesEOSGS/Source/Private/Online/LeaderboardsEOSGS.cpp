// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/LeaderboardsEOSGS.h"
#include "Online/AuthEOSGS.h"
#include "Online/OnlineIdEOSGS.h"
#include "Online/OnlineServicesEOSGS.h"
#include "Online/OnlineServicesEOSGSTypes.h"
#include "Math/UnrealMathUtility.h"

#include "eos_leaderboards.h"

namespace UE::Online {

#define EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH 256 + 1 // 256 plus null terminator
#define EOS_LEADERBOARD_MAX_ENTRIES 1000

FLeaderboardsEOSGS::FLeaderboardsEOSGS(FOnlineServicesEOSGS& InServices)
	: Super(InServices)
{
}

void FLeaderboardsEOSGS::Initialize()
{
	Super::Initialize();

	LeaderboardsHandle = EOS_Platform_GetLeaderboardsInterface(static_cast<FOnlineServicesEOSGS&>(GetServices()).GetEOSPlatformHandle());
	check(LeaderboardsHandle);
}

TOnlineAsyncOpHandle<FReadEntriesForUsers> FLeaderboardsEOSGS::ReadEntriesForUsers(FReadEntriesForUsers::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesForUsers> Op = GetOp<FReadEntriesForUsers>(MoveTemp(Params));

	if (!Services.Get<FAuthEOSGS>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FReadEntriesForUsers>& InAsyncOp, TPromise<const EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallbackInfo*>&& Promise)
	{
		TArray<EOS_ProductUserId> ProductUserIds;
		ProductUserIds.AddZeroed(InAsyncOp.GetParams().UserIds.Num());
		for (const FOnlineAccountIdHandle& UserId : InAsyncOp.GetParams().UserIds)
		{
			ProductUserIds.Emplace(GetProductUserIdChecked(UserId));
		}

		EOS_Leaderboards_UserScoresQueryStatInfo StatInfo;
		StatInfo.ApiVersion = EOS_LEADERBOARDS_USERSCORESQUERYSTATINFO_API_LATEST;
		static_assert(EOS_LEADERBOARDS_USERSCORESQUERYSTATINFO_API_LATEST == 1, "EOS_Leaderboards_UserScoresQueryStatInfo updated, check new fields");

		char StatNameANSI[EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH];
		FCStringAnsi::Strncpy(StatNameANSI, TCHAR_TO_UTF8(*InAsyncOp.GetParams().BoardName), EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH);

		StatInfo.StatName = StatNameANSI;
		StatInfo.Aggregation = EOS_ELeaderboardAggregation::EOS_LA_Latest; // TODO: Use Stats definitions

		EOS_Leaderboards_QueryLeaderboardUserScoresOptions Options = { };
		Options.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDUSERSCORES_API_LATEST;
		static_assert(EOS_LEADERBOARDS_QUERYLEADERBOARDUSERSCORES_API_LATEST == 2, "EOS_Leaderboards_QueryLeaderboardUserScores updated, check new fields");
		Options.UserIds = ProductUserIds.GetData();
		Options.UserIdsCount = ProductUserIds.Num();
		Options.StatInfo = &StatInfo;
		Options.StatInfoCount = 1;
		Options.StartTime = EOS_LEADERBOARDS_TIME_UNDEFINED;
		Options.EndTime = EOS_LEADERBOARDS_TIME_UNDEFINED;
		Options.LocalUserId = GetProductUserIdChecked(InAsyncOp.GetParams().LocalUserId);

		EOS_Async(EOS_Leaderboards_QueryLeaderboardUserScores, LeaderboardsHandle, Options, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FReadEntriesForUsers>& InAsyncOp, const EOS_Leaderboards_OnQueryLeaderboardUserScoresCompleteCallbackInfo* Data)
	{
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("EOS_Leaderboards_IngestStat failed with result=[%s]"), *LexToString(Data->ResultCode));
			InAsyncOp.SetError(FromEOSError(Data->ResultCode));
			return;
		}

		char StatNameANSI[EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH];
		FCStringAnsi::Strncpy(StatNameANSI, TCHAR_TO_UTF8(*InAsyncOp.GetParams().BoardName), EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH);

		FReadEntriesForUsers::Result Result;

		for (const FOnlineAccountIdHandle& UserId : InAsyncOp.GetParams().UserIds)
		{
			EOS_Leaderboards_CopyLeaderboardUserScoreByUserIdOptions UserCopyOptions = { };
			UserCopyOptions.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDUSERSCOREBYUSERID_API_LATEST;
			static_assert(EOS_LEADERBOARDS_COPYLEADERBOARDUSERSCOREBYUSERID_API_LATEST == 1, "EOS_Leaderboards_CopyLeaderboardUserScoreByUserIdOptions updated, check new fields");

			UserCopyOptions.UserId = GetProductUserIdChecked(UserId);
			UserCopyOptions.StatName = StatNameANSI;

			EOS_Leaderboards_LeaderboardUserScore* LeaderboardUserScore = nullptr;
			EOS_EResult CopyResult = EOS_Leaderboards_CopyLeaderboardUserScoreByUserId(LeaderboardsHandle, &UserCopyOptions, &LeaderboardUserScore);

			if (CopyResult != EOS_EResult::EOS_Success)
			{
				UE_LOG(LogTemp, VeryVerbose, TEXT("Value not found for leaderboard %s."), *InAsyncOp.GetParams().BoardName);
				continue;
			}

			FLeaderboardEntry& Entry = Result.Entries.Emplace_GetRef();
			Entry.Rank = UE_LEADERBOARD_RANK_UNKNOWN;
			Entry.UserId = UserId;
			Entry.Score = LeaderboardUserScore->Score;

			EOS_Leaderboards_LeaderboardUserScore_Release(LeaderboardUserScore);
		}

		InAsyncOp.SetResult(MoveTemp(Result));
	}) 
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

namespace Private
{

void QueryLeaderboardsEOS(EOS_HLeaderboards LeaderboardsHandle, const FOnlineAccountIdHandle& LocalUserId, const FString& BoardName, TPromise<const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo*>&& Promise)
{
	EOS_Leaderboards_QueryLeaderboardRanksOptions Options;
	Options.LocalUserId = GetProductUserIdChecked(LocalUserId);
	Options.ApiVersion = EOS_LEADERBOARDS_QUERYLEADERBOARDRANKS_API_LATEST;
	static_assert(EOS_LEADERBOARDS_QUERYLEADERBOARDRANKS_API_LATEST == 2, "EOS_Leaderboards_QueryLeaderboardRanks updated, check new fields");

	char LeaderboardNameANSI[EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH];
	FCStringAnsi::Strncpy(LeaderboardNameANSI, TCHAR_TO_UTF8(*BoardName), EOS_LEADERBOARD_STAT_NAME_MAX_LENGTH);
	Options.LeaderboardId = LeaderboardNameANSI;

	EOS_Async(EOS_Leaderboards_QueryLeaderboardRanks, LeaderboardsHandle, Options, MoveTemp(Promise));
}

void ReadEntriesInRange(EOS_HLeaderboards LeaderboardsHandle, uint32 StartIndex, uint32 EndIndex, TArray<FLeaderboardEntry>& OutEntries)
{
	for (uint32 i = StartIndex; i <= EndIndex; ++i)
	{
		EOS_Leaderboards_CopyLeaderboardRecordByIndexOptions CopyRecordOptions;
		CopyRecordOptions.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYINDEX_API_LATEST;
		static_assert(EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYINDEX_API_LATEST == 2, "EOS_Leaderboards_CopyLeaderboardRecordByIndexOptions updated, check new fields");
		CopyRecordOptions.LeaderboardRecordIndex = i;

		EOS_Leaderboards_LeaderboardRecord* LeaderboardRecord = nullptr;
		EOS_EResult CopyResult = EOS_Leaderboards_CopyLeaderboardRecordByIndex(LeaderboardsHandle, &CopyRecordOptions, &LeaderboardRecord);
		if (CopyResult == EOS_EResult::EOS_Success)
		{
			FLeaderboardEntry& LeaderboardEntry = OutEntries.Emplace_GetRef();
			LeaderboardEntry.UserId = FindAccountIdChecked(LeaderboardRecord->UserId);
			LeaderboardEntry.Rank = LeaderboardRecord->Rank;
			LeaderboardEntry.Score = LeaderboardRecord->Score;

			EOS_Leaderboards_LeaderboardRecord_Release(LeaderboardRecord);
		}
	}
}

}

TOnlineAsyncOpHandle<FReadEntriesAroundRank> FLeaderboardsEOSGS::ReadEntriesAroundRank(FReadEntriesAroundRank::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesAroundRank> Op = GetOp<FReadEntriesAroundRank>(MoveTemp(Params));

	if (!Services.Get<FAuthEOSGS>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	if (Op->GetParams().Rank >= EOS_LEADERBOARD_MAX_ENTRIES || Op->GetParams().Limit == 0)
	{
		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FReadEntriesAroundRank>& InAsyncOp, TPromise<const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo*>&& Promise)
	{
		Private::QueryLeaderboardsEOS(LeaderboardsHandle, InAsyncOp.GetParams().LocalUserId, InAsyncOp.GetParams().BoardName, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FReadEntriesAroundRank>& InAsyncOp, const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo* Data)
	{
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("EOS_Leaderboards_QueryLeaderboardRanks failed with result=[%s]"), *LexToString(Data->ResultCode));
			InAsyncOp.SetError(FromEOSError(Data->ResultCode));
			return;
		}

		FReadEntriesAroundRank::Result Result;
		int32 StartIndex = InAsyncOp.GetParams().Rank;
		int32 EndIndex = StartIndex + InAsyncOp.GetParams().Limit;
		EndIndex = FMath::Clamp(EndIndex, 0, EOS_LEADERBOARD_MAX_ENTRIES-1);
		Private::ReadEntriesInRange(LeaderboardsHandle, StartIndex, EndIndex, Result.Entries);
		InAsyncOp.SetResult(MoveTemp(Result));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FReadEntriesAroundUser> FLeaderboardsEOSGS::ReadEntriesAroundUser(FReadEntriesAroundUser::Params&& Params)
{
	TOnlineAsyncOpRef<FReadEntriesAroundUser> Op = GetOp<FReadEntriesAroundUser>(MoveTemp(Params));

	if (!Services.Get<FAuthEOSGS>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	if (Op->GetParams().Limit > EOS_LEADERBOARD_MAX_ENTRIES || Op->GetParams().Limit == 0 || Op->GetParams().Offset >= EOS_LEADERBOARD_MAX_ENTRIES || Op->GetParams().Offset <= -EOS_LEADERBOARD_MAX_ENTRIES)
	{
		Op->SetError(Errors::InvalidParams());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FReadEntriesAroundUser>& InAsyncOp, TPromise<const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo*>&& Promise)
	{
		Private::QueryLeaderboardsEOS(LeaderboardsHandle, InAsyncOp.GetParams().LocalUserId, InAsyncOp.GetParams().BoardName, MoveTemp(Promise));
	})
	.Then([this](TOnlineAsyncOp<FReadEntriesAroundUser>& InAsyncOp, const EOS_Leaderboards_OnQueryLeaderboardRanksCompleteCallbackInfo* Data)
	{
		if (Data->ResultCode != EOS_EResult::EOS_Success)
		{
			UE_LOG(LogTemp, Warning, TEXT("EOS_Leaderboards_QueryLeaderboardRanks failed with result=[%s]"), *LexToString(Data->ResultCode));
			InAsyncOp.SetError(FromEOSError(Data->ResultCode));
			return;
		}

		FReadEntriesAroundUser::Result Result;

		EOS_Leaderboards_LeaderboardRecord* LeaderboardRecord = nullptr;
		EOS_Leaderboards_CopyLeaderboardRecordByUserIdOptions CopyRecordByUserIdOptions;
		CopyRecordByUserIdOptions.ApiVersion = EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYUSERID_API_LATEST;
		static_assert(EOS_LEADERBOARDS_COPYLEADERBOARDRECORDBYUSERID_API_LATEST == 2, "EOS_Leaderboards_CopyLeaderboardRecordByUserIdOptions updated, check new fields");
		CopyRecordByUserIdOptions.UserId = GetProductUserIdChecked(InAsyncOp.GetParams().UserId);
		EOS_EResult CopyResult = EOS_Leaderboards_CopyLeaderboardRecordByUserId(LeaderboardsHandle, &CopyRecordByUserIdOptions, &LeaderboardRecord);
		if (CopyResult == EOS_EResult::EOS_Success)
		{
			int32 StartIndex = (int32)LeaderboardRecord->Rank + InAsyncOp.GetParams().Offset;
			StartIndex = FMath::Clamp(StartIndex, 0, EOS_LEADERBOARD_MAX_ENTRIES-1);
			int32 EndIndex = StartIndex + InAsyncOp.GetParams().Limit;
			EndIndex = FMath::Clamp(EndIndex, 0, EOS_LEADERBOARD_MAX_ENTRIES-1);
			Private::ReadEntriesInRange(LeaderboardsHandle, StartIndex, EndIndex, Result.Entries);

			EOS_Leaderboards_LeaderboardRecord_Release(LeaderboardRecord);
		}

		InAsyncOp.SetResult(MoveTemp(Result));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/* UE::Online */ }
