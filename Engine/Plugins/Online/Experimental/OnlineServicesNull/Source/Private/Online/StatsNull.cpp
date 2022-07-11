// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/StatsNull.h"
#include "Online/AuthNull.h"
#include "Online/OnlineServicesNull.h"
#include "Math/UnrealMathUtility.h"

namespace UE::Online {

FStatsNull::FStatsNull(FOnlineServicesNull& InOwningSubsystem)
	: Super(InOwningSubsystem)
{
}

struct FFindUserStatsNull
{
	FFindUserStatsNull(const FOnlineAccountIdHandle& InUserId)
		: UserId(InUserId)
	{
	}

	bool operator()(const FUserStats& UserStats)
	{
		return UserStats.UserId == UserId;
	}

	FOnlineAccountIdHandle UserId;
};

TOnlineAsyncOpHandle<FUpdateStats> FStatsNull::UpdateStats(FUpdateStats::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateStats> Op = GetOp<FUpdateStats>(MoveTemp(Params));

	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FUpdateStats>& InAsyncOp) mutable
	{
		for (const FUserStats& UpdateUserStats : InAsyncOp.GetParams().UpdateUsersStats)
		{
			FUserStats* ExistingUserStats = UsersStats.FindByPredicate(FFindUserStatsNull(UpdateUserStats.UserId));
			if (!ExistingUserStats)
			{
				ExistingUserStats = &UsersStats.Emplace_GetRef();
				ExistingUserStats->UserId = UpdateUserStats.UserId;
			}

			TMap<FString, FStatValue>& UserStats = ExistingUserStats->Stats;
			for (const auto& UpdateUserStatPair : UpdateUserStats.Stats)
			{
				if (FStatDefinition* StatDefinition = StatDefinitions.Find(UpdateUserStatPair.Key))
				{
					if (FStatValue* StatValue = UserStats.Find(UpdateUserStatPair.Key))
					{
						switch (StatDefinition->ModifyMethod)
						{
						case EStatModifyMethod::Set:
							*StatValue = UpdateUserStatPair.Value;
							break;
						case EStatModifyMethod::Sum:
							if (StatValue->VariantData.IsType<double>())
							{
								StatValue->Set(StatValue->GetDouble() + UpdateUserStatPair.Value.GetDouble());
							}
							else if (StatValue->VariantData.IsType<int64>())
							{
								StatValue->Set(StatValue->GetInt64() + UpdateUserStatPair.Value.GetInt64());
							}
							break;
						case EStatModifyMethod::Largest:
							if (StatValue->VariantData.IsType<double>())
							{
								StatValue->Set(FMath::Max(StatValue->GetDouble(), UpdateUserStatPair.Value.GetDouble()));
							}
							else if (StatValue->VariantData.IsType<int64>())
							{
								StatValue->Set(FMath::Max(StatValue->GetInt64(), UpdateUserStatPair.Value.GetInt64()));
							}
							break;
						case EStatModifyMethod::Smallest:
							if (StatValue->VariantData.IsType<double>())
							{
								StatValue->Set(FMath::Min(StatValue->GetDouble(), UpdateUserStatPair.Value.GetDouble()));
							}
							else if (StatValue->VariantData.IsType<int64>())
							{
								StatValue->Set(FMath::Min(StatValue->GetInt64(), UpdateUserStatPair.Value.GetInt64()));
							}
							break;
						}
					}
					else
					{
						UserStats.Emplace(UpdateUserStatPair.Key, UpdateUserStatPair.Value);
					}

					if (StatDefinition->UsageFlags & (uint32)EStatUsageFlags::Leaderboard)
					{
						// TODO: Call LeaderboardsNull to update
					}
				}
			}
		}

		InAsyncOp.SetResult({});
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FQueryStats> FStatsNull::QueryStats(FQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryStats> Op = GetOp<FQueryStats>(MoveTemp(Params));

	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FQueryStats>& InAsyncOp)
	{
		FQueryStats::Result Result;

		if (FUserStats* ExistingUserStats = UsersStats.FindByPredicate(FFindUserStatsNull(InAsyncOp.GetParams().TargetUserId)))
		{
			Result.Stats = ExistingUserStats->Stats;
		}

		InAsyncOp.SetResult(MoveTemp(Result));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FBatchQueryStats> FStatsNull::BatchQueryStats(FBatchQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FBatchQueryStats> Op = GetOp<FBatchQueryStats>(MoveTemp(Params));
	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FBatchQueryStats>& InAsyncOp)
	{
		FBatchQueryStats::Result Result;

		for (const FOnlineAccountIdHandle& TargetUserId : InAsyncOp.GetParams().TargetUserIds)
		{
			if (FUserStats* ExistingUserStats = UsersStats.FindByPredicate(FFindUserStatsNull(TargetUserId)))
			{
				Result.UsersStats.Emplace(*ExistingUserStats);
			}
		}

		InAsyncOp.SetResult(MoveTemp(Result));
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

#if !UE_BUILD_SHIPPING
TOnlineAsyncOpHandle<FResetStats> FStatsNull::ResetStats(FResetStats::Params&& Params)
{
	TOnlineAsyncOpRef<FResetStats> Op = GetOp<FResetStats>(MoveTemp(Params));

	if (!Services.Get<FAuthNull>()->IsLoggedIn(Op->GetParams().LocalUserId))
	{
		Op->SetError(Errors::InvalidUser());
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FResetStats>& InAsyncOp)
	{
		uint32 Index = UsersStats.IndexOfByPredicate(FFindUserStatsNull(InAsyncOp.GetParams().LocalUserId));
		if (Index != INDEX_NONE)
		{
			UsersStats.RemoveAt(Index);
		}
		InAsyncOp.SetResult({});
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}
#endif // !UE_BUILD_SHIPPING

/* UE::Online */ }
