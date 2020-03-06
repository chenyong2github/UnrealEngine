// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "NiagaraModule.h"

class FNiagaraDataSet;

class FNiagaraBoundsCalculator
{
public:
	virtual ~FNiagaraBoundsCalculator() { }
	virtual void InitAccessors(FNiagaraDataSet& DataSet) = 0;
	virtual void RefreshAccessors() = 0;
	virtual FBox CalculateBounds(const int32 NumInstances) const = 0;

	FORCEINLINE static float GetFloatMin(TArrayView<const float> Values)
	{
		float MinValue = FLT_MAX;

		for (auto Value : Values)
		{
			MinValue = FMath::Min(MinValue, Value);
		}

		return MinValue;
	}

	FORCEINLINE static float GetFloatMax(TArrayView<const float> Values)
	{
		float MaxValue = -FLT_MAX;

		for (auto Value : Values)
		{
			MaxValue = FMath::Max(MaxValue, Value);
		}

		return MaxValue;
	}
};
