// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"
#include "GameplayEffectComponent.h"
#include "RemoveOtherGameplayEffectComponent.generated.h"

struct FGameplayEffectRemovalInfo;

/** Remove other Gameplay Effects based on certain conditions */
UCLASS()
class GAMEPLAYABILITIES_API URemoveOtherGameplayEffectComponent : public UGameplayEffectComponent
{
	GENERATED_BODY()

public:
	/** Constructor to set EditorFriendlyName */
	URemoveOtherGameplayEffectComponent();

	/** Once we've applied, we need to register for ongoing requirements */
	virtual bool OnActiveGameplayEffectAdded(FActiveGameplayEffectsContainer& GEContainer, FActiveGameplayEffect& ActiveGE) const override;

	/** If we're only executed, it's an indication something has gone wrong and we should log it */
	virtual void OnGameplayEffectExecuted(FActiveGameplayEffectsContainer& ActiveGEContainer, FGameplayEffectSpec& GESpec, FPredictionKey& PredictionKey) const override;

#if WITH_EDITOR
	/**
	 * Warn about periodic Gameplay Effects, that you probably instead want the OngoingTagRequirements in TagRequirementsGameplayEffectComponent
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif // WITH_EDITOR

private:
	void OnGameplayEffectApplied(FActiveGameplayEffectsContainer& ActiveGEContainer, const FActiveGameplayEffectHandle& ActiveGEHandle) const;

public:
	/** On Application of the owning Gameplay Effect, any Active GameplayEffects that *match* these queries will be removed. */
	UPROPERTY(EditDefaultsOnly, Category = None)
	TArray<FGameplayEffectQuery> RemoveGameplayEffectQueries;
};
