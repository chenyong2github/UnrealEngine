// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "../IKRigDataTypes.h"

#include "IKRigInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UIKGoalCreatorInterface : public UInterface
{
	GENERATED_BODY()
};

class IIKGoalCreatorInterface
{    
	GENERATED_BODY()

public:
	
	/** Provide a Goal container for callers to read from.*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category=IKRigGoals)
    void GetIKGoals(TMap<FName, FIKRigGoal>& OutGoals);
};