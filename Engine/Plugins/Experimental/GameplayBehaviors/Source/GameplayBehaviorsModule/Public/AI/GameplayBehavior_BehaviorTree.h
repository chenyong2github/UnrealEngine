// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayBehavior.h"
#include "GameplayBehavior_BehaviorTree.generated.h"

class UBehaviorTree;
class AAIController;

/** NOTE: this behavior works only for AIControlled pawns */
UCLASS()
class GAMEPLAYBEHAVIORSMODULE_API UGameplayBehavior_BehaviorTree : public UGameplayBehavior
{
	GENERATED_BODY()
public:
	UGameplayBehavior_BehaviorTree(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool Trigger(AActor& InAvatar, const UGameplayBehaviorConfig* Config = nullptr, AActor* SmartObjectOwner = nullptr) override;
	virtual void EndBehavior(AActor& InAvatar, const bool bInterrupted) override;

protected:
	virtual bool NeedsInstance(const UGameplayBehaviorConfig* Config) const override;

protected:
	UPROPERTY()
	UBehaviorTree* PreviousBT;

	UPROPERTY()
	AAIController* AIController;
};
