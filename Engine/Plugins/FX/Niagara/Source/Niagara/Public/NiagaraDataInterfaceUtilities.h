// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"

namespace FNiagaraDataInterfaceUtilities
{
	// Finds all VM function calls made using the data interface that equals this one (i.e. A->Equals(B))
	// The same function call made be made multiple times across different scripts so you may see the same function multiple times
	// The action should return True to continue iteration or False to stop
	NIAGARA_API void ForEachVMFunctionEquals(class UNiagaraDataInterface* DataInterface, class UNiagaraSystem* NiagaraSystem, class UNiagaraComponent* Component, TFunction<bool(const FVMExternalFunctionBindingInfo&)> Action);
	// Finds all Gpu function calls made using the data interface that equals this one (i.e. A->Equals(B))
	// The same function call made be made multiple times across different scripts so you may see the same function multiple times
	// The action should return True to continue iteration or False to stop
	NIAGARA_API void ForEachGpuFunctionEquals(class UNiagaraDataInterface* DataInterface, class UNiagaraSystem* NiagaraSystem, class UNiagaraComponent* Component, TFunction<bool(const FNiagaraDataInterfaceGeneratedFunction&)> Action);

	// Finds all VM function calls made using the data interface (i.e. pointer comparison A == B)
	// The same function call made be made multiple times across different scripts so you may see the same function multiple times
	// The action should return True to continue iteration or False to stop
	NIAGARA_API void ForEachVMFunction(class UNiagaraDataInterface* DataInterface, class FNiagaraSystemInstance* SystemInstance, TFunction<bool(const FVMExternalFunctionBindingInfo&)> Action);
	// Finds all Gpu function calls made using the data interface (i.e. pointer comparison A == B)
	// The same function call made be made multiple times across different scripts so you may see the same function multiple times
	// The action should return True to continue iteration or False to stop
	NIAGARA_API void ForEachGpuFunction(class UNiagaraDataInterface* DataInterface, class FNiagaraSystemInstance* SystemInstance, TFunction<bool(const FNiagaraDataInterfaceGeneratedFunction&)> Action);
}
