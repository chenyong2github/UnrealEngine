// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraValidationRule.h"

#if WITH_EDITOR
TArray<FNiagaraValidationResult> UNiagaraValidationRule::CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const
{
	return TArray<FNiagaraValidationResult>();
}
#endif