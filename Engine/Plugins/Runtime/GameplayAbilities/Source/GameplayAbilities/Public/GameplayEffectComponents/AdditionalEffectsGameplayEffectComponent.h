// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffect.h"
#include "GameplayEffectComponent.h"
#include "AdditionalEffectsGameplayEffectComponent.generated.h"

/** Add additional Gameplay Effects that attempt to activate under certain conditions (or no conditions) */
UCLASS(CollapseCategories)
class GAMEPLAYABILITIES_API UAdditionalEffectsGameplayEffectComponent : public UGameplayEffectComponent
{
	GENERATED_BODY()

public:
	/**
     * Called when a Gameplay Effect is Added to the ActiveGameplayEffectsContainer.  GE's are added to that container when they have duration (or are predicting locally)
     * Return if the effect should remain active, or false to inhibit.  Note: Inhibit does not remove the effect (it remains added but dormant, waiting to unhibit).
     */
	virtual bool OnActiveGameplayEffectAdded(FActiveGameplayEffectsContainer& GEContainer, FActiveGameplayEffect& ActiveGE) const override;

	/**
	 * Called when a Gameplay Effect is executed.  GE's can only Execute on ROLE_Authority.  GE's only Execute when they're instant (otherwise they're added to the ActiveGameplayEffectsContainer).
	 */
	virtual void OnGameplayEffectExecuted(FActiveGameplayEffectsContainer& ActiveGEContainer, FGameplayEffectSpec& GESpec, FPredictionKey& PredictionKey) const override;

#if WITH_EDITOR
	/**
	 * There are some fields that are incompatible with one another, so let's catch them during configuration phase. 
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif // WITH_EDITOR


protected:
	/** 
	 * Define our own Applied which gets called in both cases (Added or Executed).
	 */
	void OnGameplayEffectApplied(UAbilitySystemComponent& AppliedToASC, FGameplayEffectSpec& GESpec, FPredictionKey& PredictionKey) const;

	/**
	 * Whenever the ActiveGE gets removed, we want to apply the configured OnComplete GameplayEffects
	 */
	void OnActiveGameplayEffectRemoved(const FGameplayEffectRemovalInfo& RemovalInfo, FActiveGameplayEffectsContainer* ActiveGEContainer) const;

public:
	/**
	 * This will copy all of the data (e.g. SetByCallerMagnitudes) from the GESpec that Applied this GameplayEffect to the new OnApplicationGameplayEffect Specs.
	 * One would think this is normally desirable (and how OnComplete works by default), but we default to false here for backwards compatability.
	 */
	UPROPERTY(EditDefaultsOnly, Category = OnApplication)
	bool bOnApplicationCopyDataFromOriginalSpec = false;

	/** Other gameplay effects that will be applied to the target of this effect if the owning effect applies */
	UPROPERTY(EditDefaultsOnly, Category = OnApplication)
	TArray<FConditionalGameplayEffect> OnApplicationGameplayEffects;

	/** Effects to apply when this effect completes, regardless of how it ends */
	UPROPERTY(EditDefaultsOnly, Category = OnComplete)
	TArray<TSubclassOf<UGameplayEffect>> OnCompleteAlways;

	/** Effects to apply when this effect expires naturally via its duration */
	UPROPERTY(EditDefaultsOnly, Category = OnComplete)
	TArray<TSubclassOf<UGameplayEffect>> OnCompleteNormal;

	/** Effects to apply when this effect is made to expire prematurely (e.g. via a forced removal, clear tags, etc.) */
	UPROPERTY(EditDefaultsOnly, Category = OnComplete)
	TArray<TSubclassOf<UGameplayEffect>> OnCompletePrematurely;
};
