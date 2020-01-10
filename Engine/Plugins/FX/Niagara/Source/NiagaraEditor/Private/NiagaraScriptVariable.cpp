// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptVariable.h"
#include "NiagaraCommon.h"

UNiagaraScriptVariable::UNiagaraScriptVariable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultMode(ENiagaraDefaultMode::Value)
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
