// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraValidationRule.h"
#include "NiagaraValidationRules.generated.h"


UCLASS(Category = "Validation")
class UNiagara_NoWarmupTime : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual TArray<FNiagaraValidationResult> CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const override;
};


UCLASS(Category = "Validation")
class UNiagara_FixedGPUBoundsSet : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual TArray<FNiagaraValidationResult> CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const override;
};


UCLASS(Category = "Validation")
class UNiagara_NoComponentRendererOnLowSettings : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual TArray<FNiagaraValidationResult> CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const override;
};

UCLASS(Category = "Validation")
class UNiagara_InvalidEffectType : public UNiagaraValidationRule
{
	GENERATED_BODY()
public:
	virtual TArray<FNiagaraValidationResult> CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const override;
};
