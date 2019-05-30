// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "NiagaraModule.h"

class FNiagaraBoundsCalculator
{
public:
	virtual ~FNiagaraBoundsCalculator() { }
	virtual void InitAccessors(FNiagaraDataSet& DataSet) = 0;
	virtual FBox CalculateBounds(const int32 NumInstances, bool& bOutContainsNaN) = 0;
};
