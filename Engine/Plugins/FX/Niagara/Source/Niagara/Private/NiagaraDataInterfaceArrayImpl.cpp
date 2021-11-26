// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayImpl.h"

const TCHAR* FNiagaraDataInterfaceArrayImplHelper::HLSLReadTemplateFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceArrayTemplate.ush");
const TCHAR* FNiagaraDataInterfaceArrayImplHelper::HLSLReadWriteTemplateFile = TEXT("/Plugin/FX/Niagara/Private/NiagaraDataInterfaceArrayRWTemplate.ush");

const FName FNiagaraDataInterfaceArrayImplHelper::Function_LengthName(TEXT("Length"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_IsValidIndexName(TEXT("IsValidIndex"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_LastIndexName(TEXT("LastIndex"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_GetName(TEXT("Get"));

const FName FNiagaraDataInterfaceArrayImplHelper::Function_ClearName(TEXT("Clear"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_ResizeName(TEXT("Resize"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName(TEXT("SetArrayElem"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_AddName(TEXT("Add"));
const FName FNiagaraDataInterfaceArrayImplHelper::Function_RemoveLastElemName(TEXT("RemoveLastElem"));

int32 GNiagaraArraySupportRW = 0;
static FAutoConsoleVariableRef CVarNiagaraArraySupportRW(
	TEXT("fx.NiagaraArraySupportRW"),
	GNiagaraArraySupportRW,
	TEXT("Allows the GPU to RW to the array, this comes with the caveat that all arrays will use a UAV slot."),
	ECVF_Default
);

#if WITH_EDITORONLY_DATA
bool FNiagaraDataInterfaceArrayImplHelper::UpgradeFunctionCall(FNiagaraFunctionSignature& FunctionSignature)
{
	// Early out, nothing to do here
	if ( FunctionSignature.FunctionVersion == FFunctionVersion::LatestVersion )
	{
		return false;
	}

	if ( FunctionSignature.FunctionVersion < FFunctionVersion::AddOptionalExecuteToSet )
	{
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
				break;
			}
		}

		FunctionSignature.bExperimental = false;

		if (FunctionSignature.Name == FNiagaraDataInterfaceArrayImplHelper::Function_SetArrayElemName)
		{
			FunctionSignature.Inputs.EmplaceAt(1, FNiagaraTypeDefinition::GetBoolDef(), TEXT("SkipSet"));
		}
	}

	FunctionSignature.FunctionVersion = FFunctionVersion::LatestVersion;

	return true;
}
#endif

bool FNiagaraDataInterfaceArrayImplHelper::SupportsGpuRW()
{
	return GNiagaraArraySupportRW != 0;
}

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_ArrayImpl);
