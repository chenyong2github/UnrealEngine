// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"
#include "Online/AchievementsCommon.h"

namespace UE::Online {

class FOnlineServicesNull;

class ONLINESERVICESNULL_API FAchievementsNull : public FAchievementsCommon
{
public:
	using Super = FAchievementsCommon;

	FAchievementsNull(FOnlineServicesNull& InOwningSubsystem);

	//IAchievements
	virtual TOnlineAsyncOpHandle<FQueryAchievementDefinitions> QueryAchievementDefinitions(FQueryAchievementDefinitions::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementIds> GetAchievementIds(FGetAchievementIds::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementDefinition> GetAchievementDefinition(FGetAchievementDefinition::Params&& Params) override;
	virtual TOnlineAsyncOpHandle<FQueryAchievementStates> QueryAchievementStates(FQueryAchievementStates::Params&& Params) override;
	virtual TOnlineResult<FGetAchievementState> GetAchievementState(FGetAchievementState::Params&& Params) const override;
	virtual TOnlineAsyncOpHandle<FUnlockAchievements> UnlockAchievements(FUnlockAchievements::Params&& Params) override;
	virtual TOnlineResult<FDisplayAchievementUI> DisplayAchievementUI(FDisplayAchievementUI::Params&& Params) override;

protected:
	using FAchievementDefinitionMap = TMap<FString, FAchievementDefinition>;
	using FAchievementStateMap = TMap<FString, FAchievementState>;

	TOptional<FAchievementDefinitionMap> AchievementDefinitions;
	TMap<FOnlineAccountIdHandle, FAchievementStateMap> AchievementStates;
};

/* UE::Online */ }
