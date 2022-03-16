// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraValidationRule.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraValidationRules.generated.h"

class UNiagaraScript;

/** This validation rule ensures that no Systems have a warm up time set. */
UCLASS(Category = "Validation", DisplayName = "No Warmup Time")
class UNiagaraValidationRule_NoWarmupTime : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};

/** This validation rule ensures that GPU emitters have fixed bounds set. */
UCLASS(Category = "Validation", DisplayName = "Fixed GPU Bounds Set")
class UNiagaraValidationRule_FixedGPUBoundsSet : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};

/** This validation rule can ban the use of certain renderers on all or a subset of platforms. */
UCLASS(Category = "Validation", DisplayName = "Banned Renderers")
class UNiagaraValidationRule_BannedRenderers : public UNiagaraValidationRule
{
	GENERATED_BODY()

public:

	//Platforms this validation rule will apply to.
	UPROPERTY(EditAnywhere, Category = Validation)
	FNiagaraPlatformSet Platforms;

	UPROPERTY(EditAnywhere, Category = Validation)
	TArray<TSubclassOf<UNiagaraRendererProperties>> BannedRenderers;

	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};

/** This validation rule can ban the use of certain modules on all or a subset of platforms. */
UCLASS(Category = "Validation", DisplayName = "Banned Modules")
class UNiagaraValidationRule_BannedModules : public UNiagaraValidationRule
{
	GENERATED_BODY()

public:

	//Platforms this validation rule will apply to.
	UPROPERTY(EditAnywhere, Category=Validation)
	FNiagaraPlatformSet Platforms;

	UPROPERTY(EditAnywhere, Category = Validation)
	TArray<TObjectPtr<UNiagaraScript>> BannedModules;

	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};

/** This validation rule can marks this effect type as invalid and so must be changed. Forces a choice of correct Effect Type for an System rather than. Leaving as the default. */
UCLASS(Category = "Validation", DisplayName = "Invalid Effect Type")
class UNiagaraValidationRule_InvalidEffectType : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};

/** This validation rule checks for various issue with Large World Coordinates. */
UCLASS(Category = "Validation", DisplayName = "Large World Coordinates")
class UNiagaraValidationRule_LWC : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const override;
};