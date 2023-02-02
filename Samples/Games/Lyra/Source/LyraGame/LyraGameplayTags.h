// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

namespace LyraGameplayTags
{
	FGameplayTag FindTagByString(const FString& TagString, bool bMatchPartialString = false);

	// Declare all of the custom native tags that Lyra will use
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_IsDead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_Cooldown);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_Cost);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_TagsBlocked);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_TagsMissing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_Networking);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_ActivateFail_ActivationGroup);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Behavior_SurvivesDeath);
	
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Move);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Look_Mouse);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Look_Stick);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Crouch);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_AutoRun);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InitState_Spawned);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InitState_DataAvailable);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InitState_DataInitialized);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(InitState_GameplayReady);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Death);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_Reset);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayEvent_RequestReset);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Damage);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Heal);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cheat_GodMode);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cheat_UnlimitedHealth);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Crouching);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_AutoRunning);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Death);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Death_Dying);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Death_Dead);

	// These are mappings from MovementMode enums to GameplayTags associated with those enums (below)
	extern const TMap<uint8, FGameplayTag> MovementModeTagMap;
	extern const TMap<uint8, FGameplayTag> CustomMovementModeTagMap;

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Walking);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_NavWalking);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Falling);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Swimming);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Flying);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Movement_Mode_Custom);
};
