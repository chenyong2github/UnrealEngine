// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITORONLY_DATA

#include "NiagaraTypes.h"

class UNiagaraDataInterface;
class UNiagaraSystem;

namespace FNiagaraResolveDIHelpers
{
	void CollectDIBindingsAndAssignments(
		const UNiagaraSystem* System,
		TMap<FGuid, TMap<FNiagaraVariableBase, UNiagaraDataInterface*>>& OutEmitterIdToVariableAssignmentsMap,
		TMap<FGuid, TMap<FNiagaraVariableBase, FNiagaraVariableBase>>& OutEmitterIdToVariableBindingsMap,
		TArray<FText>& OutErrorMessages);

	void ResolveDIs(
		UNiagaraSystem* System,
		const TMap<FGuid, TMap<FNiagaraVariableBase, UNiagaraDataInterface*>>& EmitterIdToVariableAssignmentsMap,
		const TMap<FGuid, TMap<FNiagaraVariableBase, FNiagaraVariableBase>>& EmitterIdToVariableBindingsMap,
		TArray<FText>& OutErrorMessages);
}

#endif