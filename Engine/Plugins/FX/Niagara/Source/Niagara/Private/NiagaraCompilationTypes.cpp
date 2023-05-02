// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCompilationTypes.h"

#include "NiagaraAsyncCompile.h"

#if WITH_EDITORONLY_DATA

TUniquePtr<FNiagaraActiveCompilation> FNiagaraActiveCompilation::CreateCompilation()
{
	return MakeUnique<FNiagaraActiveCompilationDefault>();
}

#else // WITH_EDITORONLY_DATA

TUniquePtr<FNiagaraActiveCompilation> FNiagaraActiveCompilation::CreateCompilation()
{
	return nullptr;
}

#endif // WITH_EDITORONLY_DATA
