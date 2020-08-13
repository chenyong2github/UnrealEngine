// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayImpl.h"

const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetNumName(TEXT("GetNum"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName(TEXT("IsValidIndex"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetValueName(TEXT("GetValue"));

const FName FNiagaraDataInterfaceArrayImplHelper::Function_Reset(TEXT("Reset"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_SetNumValue(TEXT("SetNum"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_SetValueName(TEXT("SetValue"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_PushValueName(TEXT("PushValue"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_PopValueName(TEXT("PopValue"));

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferName(const FString& InterfaceName)
{
	return TEXT("ArrayBuffer_") + InterfaceName;
}

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(const FString& InterfaceName)
{
	return TEXT("ArrayBufferSize_") + InterfaceName;
}

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ArrayImpl);
