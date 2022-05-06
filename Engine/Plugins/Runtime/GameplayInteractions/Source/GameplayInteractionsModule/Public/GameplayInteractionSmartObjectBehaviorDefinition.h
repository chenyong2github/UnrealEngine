// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectDefinition.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.generated.h"

class UStateTreeReferenceWrapper;

/**
 * SmartObject behavior definition for the GameplayInteractions
 */
UCLASS()
class GAMEPLAYINTERACTIONSMODULE_API UGameplayInteractionSmartObjectBehaviorDefinition : public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()

public:
	// @todo: need to forward "meta=(RequiredAssetDataTags="Schema=GameplayInteractionStateTreeSchema")"
	UPROPERTY(EditDefaultsOnly, Category="", Instanced)
	TObjectPtr<UStateTreeReferenceWrapper> StateTreeReferenceWrapper;
};