// Copyright Epic Games, Inc. All Rights Reserved.


#include "NiagaraScratchPadContainer.h"

void UNiagaraScratchPadContainer::PostLoad()
{
	CheckConsistency();

	Super::PostLoad();
}

void UNiagaraScratchPadContainer::CheckConsistency()
{
#if WITH_EDITORONLY_DATA
	for (UNiagaraScript* Script : Scripts)
	{
		if (Script && Script->GetOuter() != this)
		{
			Script->Rename(*Script->GetName(), this, REN_ForceNoResetLoaders | REN_NonTransactional);
		}
	}
#endif
}

void UNiagaraScratchPadContainer::SetScripts(const TArray<TObjectPtr<UNiagaraScript>>& InScripts)
{
#if WITH_EDITORONLY_DATA
	Scripts = InScripts;
	CheckConsistency();
#endif
}
