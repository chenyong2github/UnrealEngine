// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#include "NiagaraDataSet.h"

struct FNiagaraDataSetDebugAccessor
{
	bool Init(const FNiagaraDataSetCompiledData& CompiledData, FName InVariableName)
	{
		VariableName = InVariableName;
		bIsFloat = false;
		bIsHalf = false;
		bIsInt = false;
		NumComponents = 0;
		ComponentIndex = INDEX_NONE;

		for (int32 i = 0; i < CompiledData.Variables.Num(); ++i)
		{
			if (CompiledData.Variables[i].GetName() != VariableName)
			{
				continue;
			}

			if (CompiledData.VariableLayouts[i].GetNumFloatComponents() > 0)
			{
				bIsFloat = true;
				ComponentIndex = CompiledData.VariableLayouts[i].FloatComponentStart;
				NumComponents = CompiledData.VariableLayouts[i].GetNumFloatComponents();
			}
			else if (CompiledData.VariableLayouts[i].GetNumHalfComponents() > 0)
			{
				bIsHalf = true;
				ComponentIndex = CompiledData.VariableLayouts[i].HalfComponentStart;
				NumComponents = CompiledData.VariableLayouts[i].GetNumHalfComponents();
			}
			else if (CompiledData.VariableLayouts[i].GetNumInt32Components() > 0)
			{
				bIsInt = true;
				ComponentIndex = CompiledData.VariableLayouts[i].Int32ComponentStart;
				NumComponents = CompiledData.VariableLayouts[i].GetNumInt32Components();
			}
			return NumComponents > 0;
		}
		return false;
	}

	FVector4 ReadFloats(const FNiagaraDataBuffer* DataBuffer, uint32 Instance) const
	{
		FVector4 Value = FVector4(0.0f);
		if (DataBuffer != nullptr && ComponentIndex != INDEX_NONE && Instance < DataBuffer->GetNumInstances())
		{
			if (bIsFloat)
			{
				for (uint32 i = 0; i < NumComponents; ++i)
				{
					const float* FloatData = reinterpret_cast<const float*>(DataBuffer->GetComponentPtrFloat(ComponentIndex + i));
					Value[i] = FloatData[Instance];
				}
			}
			else if (bIsHalf)
			{
				for (uint32 i = 0; i < NumComponents; ++i)
				{
					const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(DataBuffer->GetComponentPtrHalf(ComponentIndex + i));
					Value[i] = HalfData[Instance];
				}
			}
		}
		return Value;
	}

	FIntVector4 ReadInts(const FNiagaraDataBuffer* DataBuffer, uint32 Instance) const
	{
		FIntVector4 Value = FIntVector4(0);
		if (DataBuffer != nullptr && ComponentIndex != INDEX_NONE && Instance < DataBuffer->GetNumInstances())
		{
			for (uint32 i = 0; i < NumComponents; ++i)
			{
				const int32* IntData = reinterpret_cast<const int32*>(DataBuffer->GetComponentPtrInt32(ComponentIndex + i));
				Value[i] = IntData[Instance];
			}
		}
		return Value;
	}

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
			switch (NumComponents)
			{
			case 1: StringType.Appendf(TEXT("%d"), Value[0]); break;
			case 2: StringType.Appendf(TEXT("%d, %d"), Value[0], Value[1]); break;
			case 3: StringType.Appendf(TEXT("%d, %d, %d"), Value[0], Value[1], Value[2]); break;
			case 4: StringType.Appendf(TEXT("%d, %d, %d, %d, %d"), Value[0], Value[1], Value[2], Value[3]); break;
			}
		}
	}

	FName GetName() const { return VariableName; }
	bool IsFloat() const { return bIsFloat; }
	bool IsHalf() const { return bIsHalf; }
	bool IsInt() const { return bIsInt; }
	uint32 GetNumComponents() const { return NumComponents; }

private:
	FName	VariableName;
	bool	bIsFloat = false;
	bool	bIsHalf = false;
	bool	bIsInt = false;
	uint32	NumComponents = 0;
	uint32	ComponentIndex = INDEX_NONE;
};
