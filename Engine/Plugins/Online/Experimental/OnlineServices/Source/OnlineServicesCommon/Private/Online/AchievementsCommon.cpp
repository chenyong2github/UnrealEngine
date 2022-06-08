// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/AchievementsCommon.h"

#include "Online/OnlineAsyncOp.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FAchievementsCommon::FAchievementsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Achievements"), InServices)
{
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

TOnlineResult<FGetAchievementState> FAchievementsCommon::GetAchievementState(FGetAchievementState::Params&& Params)
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

/* UE::Online */ }
