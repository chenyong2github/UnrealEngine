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
	for (TObjectPtr<UNiagaraScript> Script : Scripts)
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

void UNiagaraScratchPadContainer::AppendScripts(const TArray<TObjectPtr<UNiagaraScript>>& InScripts)
{
#if WITH_EDITORONLY_DATA
	Scripts.Append(InScripts);
	CheckConsistency();
#endif
}

void UNiagaraScratchPadContainer::AppendScripts(TObjectPtr<UNiagaraScratchPadContainer> InScripts)
{
#if WITH_EDITORONLY_DATA
	if (InScripts)
	{
		for (TObjectPtr<UNiagaraScript> Script : InScripts->Scripts)
		{
			if (Script)
			{
				FName UniqueName = MakeUniqueObjectName(this, Script->GetClass(), Script->GetFName());
				Script->Rename(*UniqueName.ToString(), this, REN_ForceNoResetLoaders | REN_NonTransactional);
				Scripts.Add(Script);
			}
		}
		InScripts->Scripts.Empty();
	}
#endif
}
