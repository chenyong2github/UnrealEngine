// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayBehaviorConfig_Animation.h"
#include "GameplayBehavior_AnimationBased.h"


UGameplayBehaviorConfig_Animation::UGameplayBehaviorConfig_Animation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BehaviorClass = UGameplayBehavior_AnimationBased::StaticClass();
}
