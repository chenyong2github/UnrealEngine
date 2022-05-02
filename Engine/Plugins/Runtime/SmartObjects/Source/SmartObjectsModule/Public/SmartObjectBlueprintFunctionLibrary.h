// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectBlueprintFunctionLibrary.generated.h"

struct FGameplayTagContainer;
class UBlackboardComponent;
class AAIController;

UCLASS(meta = (ScriptName = "SmartObjectLibrary"))
class SMARTOBJECTSMODULE_API USmartObjectBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	static FSmartObjectClaimHandle GetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName);
	
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	static void SetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName, FSmartObjectClaimHandle Value);
	
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	static bool IsValidSmartObjectClaimHandle(const FSmartObjectClaimHandle Handle)	{ return Handle.IsValid(); }

	UFUNCTION(BlueprintCallable, Category = "SmartObject", meta = (DisplayName = "SetSmartObjectEnabled"))
	static bool K2_SetSmartObjectEnabled(AActor* SmartObject, const bool bEnabled);
};
