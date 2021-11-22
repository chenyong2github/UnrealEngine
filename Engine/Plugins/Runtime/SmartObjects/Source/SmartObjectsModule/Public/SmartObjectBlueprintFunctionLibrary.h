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

	UFUNCTION(BlueprintCallable, Category = "SmartObject", meta = (DisplayName = "UseSmartObject"))
	static bool K2_UseSmartObject(AActor* Avatar, AActor* SmartObject);

	UFUNCTION(BlueprintCallable, Category = "SmartObject", meta = (DisplayName = "SetSmartObjectEnabled"))
	static bool K2_SetSmartObjectEnabled(AActor* SmartObject, const bool bEnabled);

	UE_DEPRECATED(5.0, "K2_AddLooseGameplayTags has been deprecated and will be removed soon. Use UAbilitySystemBlueprintLibrary::AddLooseGameplayTags instead")
	UFUNCTION(BlueprintCallable, Category = "SmartObject", meta = (DisplayName = "DEPRECATED_AddLooseGameplayTags", DeprecatedFunction, DeprecationMessage = "Use AbilitySystemBlueprintLibrary::AddLooseGameplayTags instead"))
	static bool K2_AddLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags);

	UE_DEPRECATED(5.0, "K2_AddLooseGameplayTags has been deprecated and will be removed soon. Use UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags instead")
	UFUNCTION(BlueprintCallable, Category = "SmartObject", meta = (DisplayName = "DEPRECATED_RemoveLooseGameplayTags", DeprecatedFunction, DeprecationMessage = "Use AbilitySystemBlueprintLibrary::RemoveLooseGameplayTags instead"))
	static bool K2_RemoveLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags);
};
