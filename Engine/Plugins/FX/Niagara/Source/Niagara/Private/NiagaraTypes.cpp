// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTypes.h"

#include "Misc/StringBuilder.h"
#include "NiagaraConstants.h"
#include "String/ParseTokens.h"

void FNiagaraVariableMetaData::CopyUserEditableMetaData(const FNiagaraVariableMetaData& OtherMetaData)
{
	for (const FProperty* ChildProperty : TFieldRange<FProperty>(StaticStruct()))
	{
		if (ChildProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			int32 PropertyOffset = ChildProperty->GetOffset_ForInternal();
			ChildProperty->CopyCompleteValue((uint8*)this + PropertyOffset, (uint8*)&OtherMetaData + PropertyOffset);
		};
	}
}


FNiagaraVariable FNiagaraVariable::ResolveAliases(const FNiagaraVariable& InVar, const TMap<FString, FString>& InAliases, const TMap<FString, FString>& InStartOnlyAliases, const TCHAR* InJoinSeparator)
{
	FNiagaraVariable OutVar = InVar;

	TStringBuilder<128> VarName;
	InVar.GetName().ToString(VarName);
	TArray<FStringView, TInlineAllocator<16>> SplitName;
	UE::String::ParseTokens(VarName, TEXT('.'), [&SplitName](FStringView Token) { SplitName.Add(Token); });

	for (int32 i = 0; i < SplitName.Num() - 1; i++)
	{
		TMap<FString, FString>::TConstIterator It = InAliases.CreateConstIterator();
		while (It)
		{
			if (SplitName[i].Equals(It.Key()))
			{
				SplitName[i] = It.Value();
			}
			++It;
		}
	}

	if (SplitName.Num() > 0)
	{
		TMap<FString, FString>::TConstIterator It = InStartOnlyAliases.CreateConstIterator();
		while (It)
		{
			if (SplitName[0].Equals(It.Key()))
			{
				SplitName[0] = It.Value();
			}
			++It;
		}
	}

	TStringBuilder<128> OutVarStrName;
	OutVarStrName.Join(SplitName, InJoinSeparator);

	OutVar.SetName(OutVarStrName.ToString());
	return OutVar;
}
