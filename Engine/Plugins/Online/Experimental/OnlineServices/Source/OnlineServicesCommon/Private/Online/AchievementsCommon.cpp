// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AchievementsCommon.h"

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineServicesCommon.h"
#include "Online/StatsCommon.h"

namespace UE::Online {

bool FAchievementUnlockRule::ContainsStat(const FString& StatName) const
{
	return Conditions.ContainsByPredicate([&StatName](const FAchievementUnlockCondition& Condition) { return Condition.StatToCheck == StatName; });
}

FAchievementsCommon::FAchievementsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Achievements"), InServices)
{
}

void FAchievementsCommon::Initialize()
{
	TOnlineComponent<IAchievements>::Initialize();

	StatEventHandle = Services.Get<FStatsCommon>()->OnStatsUpdated().Add([this](const FStatsUpdated& StatsUpdated) { UnlockAchievementsByStats(StatsUpdated); });
}

void FAchievementsCommon::Shutdown()
{
	StatEventHandle.Unbind();

	TOnlineComponent<IAchievements>::Shutdown();
}

void FAchievementsCommon::LoadConfig()
{
	TOnlineComponent<IAchievements>::LoadConfig();

	const TCHAR* ConfigSection = TEXT("OnlineServices.AchievementUnlockRules");
	GConfig->GetBool(ConfigSection, TEXT("AchievementDef_IsTitleManaged"), bIsTitleManaged, GEngineIni);

	if (bIsTitleManaged)
	{
		for (int RuleIdx = 0;; RuleIdx++)
		{
			FString AchievementToUnlock;
			GConfig->GetString(ConfigSection, *FString::Printf(TEXT("AchievementDef_%d_UnlockRule_AchievementToUnlock"), RuleIdx), AchievementToUnlock, GEngineIni);

			if (AchievementToUnlock.IsEmpty())
			{
				break;
			}

			FAchievementUnlockRule& AchievementUnlockRule = AchievementUnlockRules.Emplace_GetRef();
			AchievementUnlockRule.AchievementToUnlock = MoveTemp(AchievementToUnlock);

			FString ConditionsStr;
			GConfig->GetString(ConfigSection, *FString::Printf(TEXT("AchievementDef_%d_UnlockRule_Conditions"), RuleIdx), ConditionsStr, GEngineIni);

			bool bCullEmpty = true;
			TArray<FString> ConditionStrs;
			ConditionsStr.ParseIntoArray(ConditionStrs, TEXT(","), bCullEmpty);

			for (const FString& ConditionStr : ConditionStrs)
			{
				FString StatToCheck;
				FString UnlockThreshold;
				ConditionStr.Split(TEXT(":"), &StatToCheck, &UnlockThreshold);

				FAchievementUnlockCondition& Condition = AchievementUnlockRule.Conditions.Emplace_GetRef();
				Condition.StatToCheck = MoveTemp(StatToCheck);
				Condition.UnlockThreshold = (int64)FCString::Atoi(*UnlockThreshold);
			}
		}
	}
}

void FAchievementsCommon::RegisterCommands()
{
	RegisterCommand(&FAchievementsCommon::QueryAchievementDefinitions);
	RegisterCommand(&FAchievementsCommon::GetAchievementIds);
	RegisterCommand(&FAchievementsCommon::GetAchievementDefinition);
	RegisterCommand(&FAchievementsCommon::QueryAchievementStates);
	RegisterCommand(&FAchievementsCommon::GetAchievementState);
	RegisterCommand(&FAchievementsCommon::UnlockAchievements);
	RegisterCommand(&FAchievementsCommon::DisplayAchievementUI);
}

TOnlineAsyncOpHandle<FQueryAchievementDefinitions> FAchievementsCommon::QueryAchievementDefinitions(FQueryAchievementDefinitions::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryAchievementDefinitions> Operation = GetOp<FQueryAchievementDefinitions>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());  
	return Operation->GetHandle();
}

TOnlineResult<FGetAchievementIds> FAchievementsCommon::GetAchievementIds(FGetAchievementIds::Params&& Params)
{
	return TOnlineResult<FGetAchievementIds>(Errors::NotImplemented());
}

TOnlineResult<FGetAchievementDefinition> FAchievementsCommon::GetAchievementDefinition(FGetAchievementDefinition::Params&& Params)
{
	return TOnlineResult<FGetAchievementDefinition>(Errors::NotImplemented());
}

TOnlineAsyncOpHandle<FQueryAchievementStates> FAchievementsCommon::QueryAchievementStates(FQueryAchievementStates::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryAchievementStates> Operation = GetOp<FQueryAchievementStates>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FGetAchievementState> FAchievementsCommon::GetAchievementState(FGetAchievementState::Params&& Params) const
{
	return TOnlineResult<FGetAchievementState>(Errors::NotImplemented());
}

TOnlineAsyncOpHandle<FUnlockAchievements> FAchievementsCommon::UnlockAchievements(FUnlockAchievements::Params&& Params)
{
	TOnlineAsyncOpRef<FUnlockAchievements> Operation = GetOp<FUnlockAchievements>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineResult<FDisplayAchievementUI> FAchievementsCommon::DisplayAchievementUI(FDisplayAchievementUI::Params&& Params)
{
	return TOnlineResult<FDisplayAchievementUI>(Errors::NotImplemented());
}

TOnlineEvent<void(const FAchievementStateUpdated&)> FAchievementsCommon::OnAchievementStateUpdated()
{
	return OnAchievementStateUpdatedEvent;
}

void FAchievementsCommon::UnlockAchievementsByStats(const FStatsUpdated& StatsUpdated)
{
	if (!bIsTitleManaged)
	{
		return;
	}

	TArray<FString> StatNames;
	TArray<FOnlineAccountIdHandle> UserIds;
	for (const FUserStats& UserStats : StatsUpdated.UpdateUsersStats)
	{
		for (const TPair<FString, FStatValue>& StatPair : UserStats.Stats)
		{
			for (const FAchievementUnlockRule& AchievementUnlockRule : AchievementUnlockRules)
			{
				if (AchievementUnlockRule.ContainsStat(StatPair.Key))
				{
					for (const FAchievementUnlockCondition& Condition : AchievementUnlockRule.Conditions)
					{
						StatNames.AddUnique(Condition.StatToCheck);
					}
				}
			}
		}

		UserIds.AddUnique(UserStats.UserId);
	}

	if (StatNames.IsEmpty() || UserIds.IsEmpty())
	{
		return;
	}

	FBatchQueryStats::Params BatchQueryStatsParam;
	BatchQueryStatsParam.LocalUserId = StatsUpdated.LocalUserId;
	BatchQueryStatsParam.TargetUserIds = MoveTemp(UserIds);
	BatchQueryStatsParam.StatNames = MoveTemp(StatNames);

	Services.Get<FStatsCommon>()->BatchQueryStats(MoveTemp(BatchQueryStatsParam))
	.OnComplete([this](const TOnlineResult<FBatchQueryStats>& Result)
	{
		if (Result.IsOk())
		{
			const FBatchQueryStats::Result& BatchQueryStatsResult = Result.GetOkValue();
			for (const FUserStats& UserStats : BatchQueryStatsResult.UsersStats)
			{
				TArray<FString> AchievementsToUnlock;
				for (const TPair<FString, FStatValue>& StatPair : UserStats.Stats)
				{
					ExecuteUnlockRulesRelatedToStat(UserStats.UserId, StatPair.Key, UserStats.Stats, AchievementsToUnlock);
				}

				if (!AchievementsToUnlock.IsEmpty())
				{
					FUnlockAchievements::Params UnlockAchievementsParams;
					UnlockAchievementsParams.LocalUserId = UserStats.UserId;
					UnlockAchievementsParams.AchievementIds = MoveTemp(AchievementsToUnlock);
					UnlockAchievements(MoveTemp(UnlockAchievementsParams));
				}
			}
		}
	});
}

void FAchievementsCommon::ExecuteUnlockRulesRelatedToStat(const FOnlineAccountIdHandle& UserId, const FString& StatName, const TMap<FString, FStatValue>& Stats, TArray<FString>& OutAchievementsToUnlock)
{
	for (const FAchievementUnlockRule& AchievementUnlockRule : AchievementUnlockRules)
	{
		if (AchievementUnlockRule.ContainsStat(StatName)
			&& !IsUnlocked(UserId, AchievementUnlockRule.AchievementToUnlock) 
			&& MeetUnlockCondition(AchievementUnlockRule, Stats))
		{
			OutAchievementsToUnlock.AddUnique(AchievementUnlockRule.AchievementToUnlock);
		}
	}
}

bool FAchievementsCommon::MeetUnlockCondition(FAchievementUnlockRule AchievementUnlockRule, const TMap<FString, FStatValue>& Stats)
{
	for (const FAchievementUnlockCondition& Condition : AchievementUnlockRule.Conditions)
	{
		if (const FStatDefinition* StatDefinition = Services.Get<FStatsCommon>()->GetStatDefinition(Condition.StatToCheck))
		{
			const FStatValue* StatValue = Stats.Find(Condition.StatToCheck);
			if (!StatValue)
			{
				UE_LOG(LogTemp, Warning, TEXT("Can't find stat %s when check if it can unlock achievement."), *Condition.StatToCheck);
				return false;
			}

			switch (StatDefinition->ModifyMethod)
			{
			case EStatModifyMethod::Sum: // Intentional fall through
			case EStatModifyMethod::Largest:
				if (StatValue->GetInt64() < Condition.UnlockThreshold.GetInt64())
				{
					return false;
				}
				break;
			case EStatModifyMethod::Set:
				if (StatValue->GetInt64() != Condition.UnlockThreshold.GetInt64())
				{
					return false;
				}
				break;
			case EStatModifyMethod::Smallest:
				if (StatValue->GetInt64() > Condition.UnlockThreshold.GetInt64())
				{
					return false;
				}
				break;
			}
		}
	}

	return true;
}

bool FAchievementsCommon::IsUnlocked(const FOnlineAccountIdHandle& UserId, const FString& AchievementName) const
{
	FGetAchievementState::Params Params;
	Params.LocalUserId = UserId;
	Params.AchievementId = AchievementName;
	TOnlineResult<FGetAchievementState> Result = GetAchievementState(MoveTemp(Params));
	if (Result.IsOk())
	{
		const FAchievementState& AchievementState = Result.GetOkValue().AchievementState;
		return FMath::IsNearlyEqual(AchievementState.Progress, 1.0f);
	}

	UE_LOG(LogTemp, Warning, TEXT("Can't find state achievement %s when check if it's unlocked."), *AchievementName);

	return false;
}

/* UE::Online */ }
