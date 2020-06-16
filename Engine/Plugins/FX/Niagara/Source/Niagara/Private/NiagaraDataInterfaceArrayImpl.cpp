// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayImpl.h"

const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetNumName(TEXT("GetNum"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetValueName(TEXT("GetValue"));

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferName(const FString& InterfaceName)
{
	return TEXT("ArrayBuffer_") + InterfaceName;
}

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(const FString& InterfaceName)
{
	return TEXT("ArrayBufferSize_") + InterfaceName;
}

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ArrayImpl);
