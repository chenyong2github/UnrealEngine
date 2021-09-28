// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "SmartObjectBlueprintFunctionLibrary.generated.h"


class AAIController;

UCLASS(meta = (ScriptName = "SmartObjectLibrary"))
class SMARTOBJECTSMODULE_API USmartObjectBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "SmartObjects", meta = (DisplayName = "UseSmartObject"))
	static bool K2_UseSmartObject(AActor* Avatar, AActor* SmartObject);

	UFUNCTION(BlueprintCallable, Category = "SmartObjects", meta = (DisplayName = "SetSmartObjectEnabled"))
	static bool K2_SetSmartObjectEnabled(AActor* SmartObject, const bool bEnabled);

	UE_DEPRECATED(5.0, "K2_AddLooseGameplayTags has been deprecated and will be removed soon. Use UAbilitySystemBlueprintLibrary::AddLooseGameplayTags instead")
	UFUNCTION(BlueprintCallable, Category = "SmartObjects", meta = (DisplayName = "DEPRECATED_AddLooseGameplayTags", DeprecatedFunction, DeprecationMessage = "Use AbilitySystemBlueprintLibrary::AddLooseGameplayTags instead"))
	static bool K2_AddLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags);

	UE_DEPRECATED(5.0, "K2_AddLooseGameplayTags has been deprecated and will be removed soon. Use UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags instead")
	UFUNCTION(BlueprintCallable, Category = "SmartObjects", meta = (DisplayName = "DEPRECATED_RemoveLooseGameplayTags", DeprecatedFunction, DeprecationMessage = "Use AbilitySystemBlueprintLibrary::RemoveLooseGameplayTags instead"))
	static bool K2_RemoveLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags);
};
