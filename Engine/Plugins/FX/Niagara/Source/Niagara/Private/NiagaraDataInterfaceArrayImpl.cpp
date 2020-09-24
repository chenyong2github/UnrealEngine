// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayImpl.h"

const FName FNiagaraDataInterfaceArrayImplHelper::Function_LengthName(TEXT("Length"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName(TEXT("IsValidIndex"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_LastIndexName(TEXT("LastIndex"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetName(TEXT("Get"));

const FName FNiagaraDataInterfaceArrayImplHelper::Function_ClearName(TEXT("Clear"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_ResizeName(TEXT("Resize"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName(TEXT("SetArrayElem"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_AddName(TEXT("Add"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_RemoveLastElemName(TEXT("RemoveLastElem"));

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferName(const FString& InterfaceName)
{
	return TEXT("ArrayBuffer_") + InterfaceName;
}

FString FNiagaraDataInterfaceArrayImplHelper::GetBufferSizeName(const FString& InterfaceName)
{
	return TEXT("ArrayBufferSize_") + InterfaceName;
}

#if WITH_EDITORONLY_DATA
bool FNiagaraDataInterfaceArrayImplHelper::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	bool bModified = false;

	static const TPair<FName, FName> NodeRenames[] =
	{
		MakeTuple(FName("GetNum"),			FNiagaraDataInterfaceArrayImplHelper::Function_LengthName),
		MakeTuple(FName("GetValue"),		FNiagaraDataInterfaceArrayImplHelper::Function_GetName),
		MakeTuple(FName("Reset"),			FNiagaraDataInterfaceArrayImplHelper::Function_ClearName),
		MakeTuple(FName("SetNum"),			FNiagaraDataInterfaceArrayImplHelper::Function_ResizeName),
		MakeTuple(FName("SetValue"),		FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName),
		MakeTuple(FName("PushValue"),		FNiagaraDataInterfaceArrayImplHelper::Function_AddName),
		MakeTuple(FName("PopValue"),		FNiagaraDataInterfaceArrayImplHelper::Function_RemoveLastElemName),
	};

	for (const auto& Pair : NodeRenames)
	{
		if (Pair.Key == FunctionSignature.Name)
		{
			FunctionSignature.Name = Pair.Value;
			bModified = true;
			break;
		}
	}

	return bModified;
}
#endif

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ArrayImpl);
