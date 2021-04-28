// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#include "NiagaraDataSet.h"

struct FNiagaraDataSetDebugAccessor
{
	bool Init(const FNiagaraDataSetCompiledData& CompiledData, FName InVariableName);

	float ReadFloat(const FNiagaraDataBuffer* DataBuffer, uint32 Instance, uint32 Component) const;
	int32 ReadInt(const FNiagaraDataBuffer* DataBuffer, uint32 Instance, uint32 Component) const;

	template<typename TString>
	void StringAppend(TString& StringType, FNiagaraDataBuffer* DataBuffer, uint32 Instance) const
	{
		if (IsFloat() || IsHalf())
		{
			for (uint32 iComponent = 0; iComponent < NumComponents; ++iComponent)
			{
				if (iComponent != 0)
				{
					StringType.Append(TEXT(", "));
				}
				StringType.Appendf(TEXT("%.2f"), ReadFloat(DataBuffer, Instance, iComponent));
			}
		}
		else if (IsInt())
		{
			if (NiagaraType == FNiagaraTypeDefinition::GetBoolDef())
			{
				const TCHAR* TrueText = TEXT("true");
				const TCHAR* FalseText = TEXT("false");

				for (uint32 iComponent = 0; iComponent < NumComponents; ++iComponent)
				{
					if (iComponent != 0)
					{
						StringType.Append(TEXT(", "));
					}
					StringType.Append(ReadInt(DataBuffer, Instance, iComponent) == FNiagaraBool::True ? TrueText : FalseText);
				}
			}
			else
			{
				for (uint32 iComponent = 0; iComponent < NumComponents; ++iComponent)
				{
					if (iComponent != 0)
					{
						StringType.Append(TEXT(", "));
					}
					StringType.Appendf(TEXT("%d"), ReadInt(DataBuffer, Instance, iComponent));
				}
			}
		}
	}

	FName GetName() const { return VariableName; }
	bool IsFloat() const { return bIsFloat; }
	bool IsHalf() const { return bIsHalf; }
	bool IsInt() const { return bIsInt; }
	uint32 GetNumComponents() const { return NumComponents; }

	static bool ValidateDataBuffer(const FNiagaraDataSetCompiledData& CompiledData, const FNiagaraDataBuffer* DataBuffer, uint32 iInstance, TFunction<void(const FNiagaraVariable&, int32)> ErrorCallback);
	static bool ValidateDataBuffer(const FNiagaraDataSetCompiledData& CompiledData, const FNiagaraDataBuffer* DataBuffer, TFunction<void(const FNiagaraVariable&, uint32, int32)> ErrorCallback);

private:
	FName					VariableName;
	FNiagaraTypeDefinition	NiagaraType;
	bool					bIsFloat = false;
	bool					bIsHalf = false;
	bool					bIsInt = false;
	uint32					NumComponents = 0;
	uint32					ComponentIndex = INDEX_NONE;
};
