// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayBehaviorsBlueprintFunctionLibrary.h"
#include "GameFramework/Actor.h"


bool UGameplayBehaviorsBlueprintFunctionLibrary::StopGameplayBehavior(TSubclassOf<UGameplayBehavior> GameplayBehaviorClass, AActor* Avatar)
{
	if (Avatar == nullptr || !GameplayBehaviorClass)
	{
		return false;
	}

	return false;
}