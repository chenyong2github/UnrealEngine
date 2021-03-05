// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#include "NiagaraDataSet.h"

struct FNiagaraDataSetDebugAccessor
{
	bool Init(const FNiagaraDataSetCompiledData& CompiledData, FName InVariableName);

	FVector4 ReadFloats(const FNiagaraDataBuffer* DataBuffer, uint32 Instance) const;
	FIntVector4 ReadInts(const FNiagaraDataBuffer* DataBuffer, uint32 Instance) const;

	template<typename TString>
	void StringAppend(TString& StringType, FNiagaraDataBuffer* DataBuffer, uint32 Instance) const
	{
		if (IsFloat() || IsHalf())
		{
			FVector4 Value = ReadFloats(DataBuffer, Instance);
			switch (NumComponents)
			{
				case 1: StringType.Appendf(TEXT("%.2f"), Value[0]); break;
				case 2: StringType.Appendf(TEXT("%.2f, %.2f"), Value[0], Value[1]); break;
				case 3: StringType.Appendf(TEXT("%.2f, %.2f, %.2f"), Value[0], Value[1], Value[2]); break;
				case 4: StringType.Appendf(TEXT("%.2f, %.2f, %.2f, %.2f"), Value[0], Value[1], Value[2], Value[3]); break;
			}
		}
		else if (IsInt())
		{
			FIntVector4 Value = ReadInts(DataBuffer, Instance);
			if (NiagaraType == FNiagaraTypeDefinition::GetBoolDef())
			{
				const TCHAR* TrueText = TEXT("true");
				const TCHAR* FalseText = TEXT("false");
				for ( uint32 iComponent=0; iComponent < NumComponents; ++iComponent)
				{
					if (iComponent != 0 )
					{
						StringType.Append(TEXT(", "));
					}
					StringType.Append(Value[iComponent] == FNiagaraBool::True ? TrueText : FalseText);
				}
			}
			else
			{
				switch (NumComponents)
				{
					case 1: StringType.Appendf(TEXT("%d"), Value[0]); break;
					case 2: StringType.Appendf(TEXT("%d, %d"), Value[0], Value[1]); break;
					case 3: StringType.Appendf(TEXT("%d, %d, %d"), Value[0], Value[1], Value[2]); break;
					case 4: StringType.Appendf(TEXT("%d, %d, %d, %d, %d"), Value[0], Value[1], Value[2], Value[3]); break;
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
