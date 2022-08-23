// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraSimCache.h"

struct FNiagaraSimCacheAttributeReaderHelper
{
	explicit FNiagaraSimCacheAttributeReaderHelper(const UNiagaraSimCache* SimCache, FName EmitterName, FName AttributeName, int FrameIndex)
	{
		if (SimCache->IsCacheValid() == false)
		{
			return;
		}

		if (SimCache->CacheFrames.IsValidIndex(FrameIndex) == false)
		{
			return;
		}
		CacheFrame = &SimCache->CacheFrames[FrameIndex];

		if ( EmitterName.IsNone() == false )
		{
			const int32 EmitterIndex = SimCache->CacheLayout.EmitterLayouts.IndexOfByPredicate([&EmitterName](const FNiagaraSimCacheDataBuffersLayout& FoundLayout) { return FoundLayout.LayoutName == EmitterName; });
			if (EmitterIndex == INDEX_NONE)
			{
				return;
			}

			DataBuffers = &CacheFrame->EmitterData[EmitterIndex].ParticleDataBuffers;
			DataBuffersLayout = &SimCache->CacheLayout.EmitterLayouts[EmitterIndex];
		}
		else
		{
			DataBuffers = &CacheFrame->SystemData.SystemDataBuffers;
			DataBuffersLayout = &SimCache->CacheLayout.SystemLayout;
		}

		if (DataBuffers->NumInstances == 0)
		{
			return;
		}

		const int32 VariableIndex = DataBuffersLayout->Variables.IndexOfByPredicate([&AttributeName](const FNiagaraSimCacheVariable& FoundVariable) { return FoundVariable.Variable.GetName() == AttributeName; });
		if (VariableIndex == INDEX_NONE)
		{
			return;
		}

		Variable = &DataBuffersLayout->Variables[VariableIndex];
	}

	bool IsValid() const { return Variable != nullptr; }

	int32 GetNumInstances() const { check(IsValid()); return DataBuffers->NumInstances; }

	float ReadInt(int32 Instance) const
	{
		check(IsValid());
		check(Variable->Int32Offset != INDEX_NONE && Variable->Int32Count == 1);

		const int32 Int32Offset = Instance + (Variable->Int32Offset * DataBuffers->NumInstances);

		int32 Value;
		FMemory::Memcpy(&Value, &DataBuffers->Int32Data[Int32Offset * sizeof(int32)], sizeof(int32));
		return Value;
	}

	float InternalReadFloat(int32 Offset, int32 Instance) const
	{
		check(IsValid());
		
		const int32 FloatOffset = Instance + ((Variable->FloatOffset + Offset) * DataBuffers->NumInstances);

		float Value;
		FMemory::Memcpy(&Value, &DataBuffers->FloatData[FloatOffset * sizeof(float)], sizeof(float));
		return Value;
	}

	float ReadFloat(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 1);
		return InternalReadFloat(0, Instance);
	}

	FVector2D ReadFloat2(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 2);
		return FVector2D(InternalReadFloat(0, Instance), InternalReadFloat(1, Instance));
	}

	FVector ReadFloat3(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 3);
		return FVector(InternalReadFloat(0, Instance), InternalReadFloat(1, Instance), InternalReadFloat(2, Instance));
	}

	FVector4 ReadFloat4(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 4);
		return FVector4(InternalReadFloat(0, Instance), InternalReadFloat(1, Instance), InternalReadFloat(2, Instance), InternalReadFloat(3, Instance));
	}

	FLinearColor ReadColor(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 4);
		return FLinearColor(InternalReadFloat(0, Instance), InternalReadFloat(1, Instance), InternalReadFloat(2, Instance), InternalReadFloat(3, Instance));
	}

	FQuat ReadQuat(int32 Instance) const
	{
		check(Variable->FloatOffset != INDEX_NONE && Variable->FloatCount == 4);
		return FQuat(InternalReadFloat(0, Instance), InternalReadFloat(1, Instance), InternalReadFloat(2, Instance), InternalReadFloat(3, Instance));
	}
	
	const FNiagaraSimCacheFrame*				CacheFrame = nullptr;
	const FNiagaraSimCacheDataBuffers*			DataBuffers = nullptr;
	const FNiagaraSimCacheDataBuffersLayout*	DataBuffersLayout = nullptr;
	const FNiagaraSimCacheVariable*				Variable = nullptr;
};
