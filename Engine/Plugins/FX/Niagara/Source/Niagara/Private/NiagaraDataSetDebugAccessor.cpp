// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataSetDebugAccessor.h"
#include "NiagaraSystem.h"

bool FNiagaraDataSetDebugAccessor::Init(const FNiagaraDataSetCompiledData& CompiledData, FName InVariableName)
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
			NiagaraType = CompiledData.Variables[i].GetType();
		}
		else if (CompiledData.VariableLayouts[i].GetNumHalfComponents() > 0)
		{
			bIsHalf = true;
			ComponentIndex = CompiledData.VariableLayouts[i].HalfComponentStart;
			NumComponents = CompiledData.VariableLayouts[i].GetNumHalfComponents();
			NiagaraType = CompiledData.Variables[i].GetType();
		}
		else if (CompiledData.VariableLayouts[i].GetNumInt32Components() > 0)
		{
			bIsInt = true;
			ComponentIndex = CompiledData.VariableLayouts[i].Int32ComponentStart;
			NumComponents = CompiledData.VariableLayouts[i].GetNumInt32Components();
			NiagaraType = CompiledData.Variables[i].GetType();
		}
		return NumComponents > 0;
	}
	return false;
}

float FNiagaraDataSetDebugAccessor::ReadFloat(const FNiagaraDataBuffer* DataBuffer, uint32 Instance, uint32 Component) const
{
	if (DataBuffer != nullptr && ComponentIndex != INDEX_NONE && Instance < DataBuffer->GetNumInstances() && Component < NumComponents)
	{
		if (bIsFloat)
		{
			const float* FloatData = reinterpret_cast<const float*>(DataBuffer->GetComponentPtrFloat(ComponentIndex + Component));
			return FloatData[Instance];
		}
		else if (bIsHalf)
		{
			const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(DataBuffer->GetComponentPtrHalf(ComponentIndex + Component));
			return HalfData[Instance];
		}
	}
	return 0.0f;
}

int32 FNiagaraDataSetDebugAccessor::ReadInt(const FNiagaraDataBuffer* DataBuffer, uint32 Instance, uint32 Component) const
{
	if (DataBuffer != nullptr && ComponentIndex != INDEX_NONE && Instance < DataBuffer->GetNumInstances() && Component < NumComponents)
	{
		const int32* IntData = reinterpret_cast<const int32*>(DataBuffer->GetComponentPtrInt32(ComponentIndex + Component));
		return IntData[Instance];
	}
	return 0;
}

bool FNiagaraDataSetDebugAccessor::ValidateDataBuffer(const FNiagaraDataSetCompiledData& CompiledData, const FNiagaraDataBuffer* DataBuffer, uint32 iInstance, TFunction<void(const FNiagaraVariable&, int32)> ErrorCallback)
{
	bool bIsValid = true;

	// If it's not a valid index skip it
	if ( iInstance >= DataBuffer->GetNumInstances() )
	{
		return bIsValid;
	}

	// For each variable look at data
	for (int32 iVariable = 0; iVariable < CompiledData.Variables.Num(); ++iVariable)
	{
		// Look over float data
		if (CompiledData.VariableLayouts[iVariable].GetNumFloatComponents() > 0)
		{
			const int32 ComponentIndex = CompiledData.VariableLayouts[iVariable].FloatComponentStart;
			const int32 NumComponents = CompiledData.VariableLayouts[iVariable].GetNumFloatComponents();

			for (int32 iComponent = 0; iComponent < NumComponents; ++iComponent)
			{
				const float* FloatData = reinterpret_cast<const float*>(DataBuffer->GetComponentPtrFloat(ComponentIndex + iComponent));
				check(FloatData);

				if (!FMath::IsFinite(FloatData[iInstance]))
				{
					bIsValid = false;
					ErrorCallback(CompiledData.Variables[iVariable], iComponent);
				}
			}
		}
		else if (CompiledData.VariableLayouts[iVariable].GetNumHalfComponents() > 0)
		{
			const int32 ComponentIndex = CompiledData.VariableLayouts[iVariable].HalfComponentStart;
			const int32 NumComponents = CompiledData.VariableLayouts[iVariable].GetNumHalfComponents();

			for (int32 iComponent = 0; iComponent < NumComponents; ++iComponent)
			{
				const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(DataBuffer->GetComponentPtrHalf(ComponentIndex + iComponent));
				check(HalfData);

				const float Value = HalfData[iInstance].GetFloat();
				if (!FMath::IsFinite(Value))
				{
					bIsValid = false;
					ErrorCallback(CompiledData.Variables[iVariable], iComponent);
				}
			}
		}
	}

	return bIsValid;
}

bool FNiagaraDataSetDebugAccessor::ValidateDataBuffer(const FNiagaraDataSetCompiledData& CompiledData, const FNiagaraDataBuffer* DataBuffer, TFunction<void(const FNiagaraVariable&, uint32, int32)> ErrorCallback)
{
	bool bIsValid = true;

	// For each variable look at data
	for (int32 iVariable=0; iVariable < CompiledData.Variables.Num(); ++iVariable)
	{
		// Look over float data
		if (CompiledData.VariableLayouts[iVariable].GetNumFloatComponents() > 0)
		{
			const int32 ComponentIndex = CompiledData.VariableLayouts[iVariable].FloatComponentStart;
			const int32 NumComponents = CompiledData.VariableLayouts[iVariable].GetNumFloatComponents();

			for ( int32 iComponent=0; iComponent < NumComponents; ++iComponent )
			{
				const float* FloatData = reinterpret_cast<const float*>(DataBuffer->GetComponentPtrFloat(ComponentIndex + iComponent));
				check(FloatData);

				for ( uint32 iInstance=0; iInstance < DataBuffer->GetNumInstances(); ++iInstance )
				{
					if ( !FMath::IsFinite(FloatData[iInstance]) )
					{
						bIsValid = false;
						ErrorCallback(CompiledData.Variables[iVariable], iInstance, iComponent);
					}
				}
			}
		}
		else if (CompiledData.VariableLayouts[iVariable].GetNumHalfComponents() > 0)
		{
			const int32 ComponentIndex = CompiledData.VariableLayouts[iVariable].HalfComponentStart;
			const int32 NumComponents = CompiledData.VariableLayouts[iVariable].GetNumHalfComponents();

			for (int32 iComponent = 0; iComponent < NumComponents; ++iComponent)
			{
				const FFloat16* HalfData = reinterpret_cast<const FFloat16*>(DataBuffer->GetComponentPtrHalf(ComponentIndex + iComponent));
				check(HalfData);

				for (uint32 iInstance = 0; iInstance < DataBuffer->GetNumInstances(); ++iInstance)
				{
					const float Value = HalfData[iInstance].GetFloat();
					if (!FMath::IsFinite(Value))
					{
						bIsValid = false;
						ErrorCallback(CompiledData.Variables[iVariable], iInstance, iComponent);
					}
				}
			}
		}
	}

	return bIsValid;
}
