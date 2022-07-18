// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/Achievements.h"
#include "Online/OnlineComponent.h"
#include "Online/Stats.h"

namespace UE::Online {

class FOnlineServicesCommon;

struct FAchievementUnlockCondition
{
	FString StatToCheck;
	FStatValue UnlockThreshold; // The unlock rule depends on Stat modification type
};

struct FAchievementUnlockRule
{
	FString AchievementToUnlock;
	TArray<FAchievementUnlockCondition> Conditions;

	bool ContainsStat(const FString& StatName) const;
};

class ONLINESERVICESCOMMON_API FAchievementsCommon : public TOnlineComponent<IAchievements>
{
public:
	using Super = IAchievements;

	FAchievementsCommon(FOnlineServicesCommon& InServices);

	// TOnlineComponent
	virtual void Initialize() override;
	virtual void Shutdown() override;
	virtual void LoadConfig() override;
	virtual void RegisterCommands() override;

	// IAchievements
	virtual TOnlineAsyncOpHandle<FQueryAchievementDefinitions> QueryAchievementDefinitions(FQueryAchievementDefinitions::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementIds> GetAchievementIds(FGetAchievementIds::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementDefinition> GetAchievementDefinition(FGetAchievementDefinition::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FQueryAchievementStates> QueryAchievementStates(FQueryAchievementStates::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementState> GetAchievementState(FGetAchievementState::Params&& Params) const override;
	virtual TOnlineAsyncOpHandle<FUnlockAchievements> UnlockAchievements(FUnlockAchievements::Params&& Params) override;
	virtual TOnlineResult<FDisplayAchievementUI> DisplayAchievementUI(FDisplayAchievementUI::Params&& Params) override;
	virtual TOnlineEvent<void(const FAchievementStateUpdated&)> OnAchievementStateUpdated() override;

protected:
	TOnlineEventCallable<void(const FAchievementStateUpdated&)> OnAchievementStateUpdatedEvent;

	bool IsTitleManaged() const;
	void UnlockAchievementsByStats(const FStatsUpdated& StatsUpdated);
	void ExecuteUnlockRulesRelatedToStat(const FOnlineAccountIdHandle& UserId, const FString& StatName, const TMap<FString, FStatValue>& Stats, TArray<FString>& OutAchievementsToUnlock);
	bool MeetUnlockCondition(FAchievementUnlockRule AchievementUnlockRule, const TMap<FString, FStatValue>& Stats);
	bool IsUnlocked(const FOnlineAccountIdHandle& UserId, const FString& AchievementName) const;

	TArray<FAchievementUnlockRule> AchievementUnlockRules;
	FOnlineEventDelegateHandle StatEventHandle;

	bool bIsTitleManaged = false;
};

/* UE::Online */ }
