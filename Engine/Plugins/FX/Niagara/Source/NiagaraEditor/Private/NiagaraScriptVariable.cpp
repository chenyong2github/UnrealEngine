// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptVariable.h"

UNiagaraScriptVariable::UNiagaraScriptVariable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

void UNiagaraScriptVariable::PostLoad()
{
	Super::PostLoad();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}
}
