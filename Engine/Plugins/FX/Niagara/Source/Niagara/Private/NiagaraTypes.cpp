// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTypes.h"
#include "NiagaraConstants.h"

FString FNiagaraTypeUtilities::GetNamespaceStringForScriptParameterScope(const ENiagaraParameterScope& InScope)
{
	switch (InScope) {
	case ENiagaraParameterScope::Engine:
		return PARAM_MAP_ENGINE_STR;
		break;
	case ENiagaraParameterScope::Owner:
		return PARAM_MAP_ENGINE_OWNER_STR;
		break;
	case ENiagaraParameterScope::User:
		return PARAM_MAP_USER_STR;
		break;
	case ENiagaraParameterScope::System:
		return PARAM_MAP_SYSTEM_STR;
		break;
	case ENiagaraParameterScope::Emitter:
		return PARAM_MAP_EMITTER_STR;
		break;
	case ENiagaraParameterScope::Particles:
		return PARAM_MAP_ATTRIBUTE_STR;
		break;
	case ENiagaraParameterScope::ScriptPersistent:
		return PARAM_MAP_SCRIPT_PERSISTENT_STR;
		break;
	case ENiagaraParameterScope::ScriptTransient:
		return PARAM_MAP_SCRIPT_TRANSIENT_STR;
		break;
	case ENiagaraParameterScope::Input:
		return PARAM_MAP_MODULE_STR;
		break;
	case ENiagaraParameterScope::Output:
		return PARAM_MAP_OUTPUT_MODULE_STR;
		break;
	case ENiagaraParameterScope::Local:
		return PARAM_MAP_LOCAL_MODULE_STR;
		break;
	default:
		checkf(false, TEXT("Unhandled parameter scope encountered!"));
		return FString();
		break;
	};
}

bool FNiagaraVariableMetaData::GetParameterName(FName& OutName) const
{
	if (bUseLegacyNameString)
	{
		return false;
	}
	OutName = CachedNamespacelessVariableName;
	return true;
}

void FNiagaraVariableMetaData::CopyPerScriptMetaData(const FNiagaraVariableMetaData& OtherMetaData)
{
	SetUsage(OtherMetaData.GetUsage());
	SetScopeName(OtherMetaData.GetScopeName());
	FName OtherMetaDataName;
	if (OtherMetaData.GetParameterName(OtherMetaDataName))
	{
		SetCachedNamespacelessVariableName(OtherMetaDataName);
	}
	SetWasCreatedInSystemEditor(OtherMetaData.GetWasCreatedInSystemEditor());
	SetIsUsingLegacyNameString(OtherMetaData.GetIsUsingLegacyNameString());
}


void FNiagaraVariableMetaData::SetCachedNamespacelessVariableName(const FName& InVariableName)
{
	/*if (InVariableName == NAME_None || InVariableName == TEXT("None"))
	{
	}*/
	//UE_LOG(LogNiagara, Log, TEXT("SetCachedNamespacelessVariableName %s!"), *InVariableName.ToString());
	CachedNamespacelessVariableName = InVariableName;
};
