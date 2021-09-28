// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayBehavior.h"
#include "GameplayBehaviorsBlueprintFunctionLibrary.generated.h"


class AActor;

UCLASS(meta = (ScriptName = "GameplayBehaviorsLibrary"))
class GAMEPLAYBEHAVIORSMODULE_API UGameplayBehaviorsBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Will force-stop GameplayBehavior on given Avatar assuming the current 
	 *	behavior is of GameplayBehaviorClass class*/
	static bool StopGameplayBehavior(TSubclassOf<UGameplayBehavior> GameplayBehaviorClass,  AActor* Avatar);
};
