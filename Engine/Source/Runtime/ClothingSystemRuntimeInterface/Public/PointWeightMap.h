// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/ObjectMacros.h"
#include "Math/NumericLimits.h"
#include "PointWeightMap.generated.h"

/** 
 * A mask is simply some storage for a physical mesh parameter painted onto clothing.
 * Used in the editor for users to paint onto and then target to a parameter, which
 * is then later applied to a phys mesh
 */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMEINTERFACE_API FPointWeightMap
{
	GENERATED_BODY();

	FPointWeightMap()
		: Name(NAME_None)
		, CurrentTarget(0) // 0 = NONE
		, bEnabled(false)
	{}
	~FPointWeightMap()
	{}

	/** Initialize the weight map. */
	void
	Initialize(const int32 NumPoints)
	{
		Values.Reset(NumPoints);
		Values.AddZeroed(NumPoints);
		bEnabled = false; // Not sure why we do this...
	}

	/** Copies from \p SourceValues to \c Values. */
	void
	CopyFrom(const TArray<float>& SourceValues, uint8 Target)
	{ Initialize(SourceValues.Num()); Values = SourceValues; CurrentTarget = Target; }
	void
	CopyFrom(const TArray<float>* SourceValues, uint8 Target)
	{ if (SourceValues) CopyFrom(*SourceValues, Target); }

	/** Copies from \c Values to \p TargetValues. */
	void
	CopyTo(TArray<float>& TargetValues) const
	{ TargetValues = Values; }
	void
	CopyTo(TArray<float>* TargetValues) const
	{ if (TargetValues) CopyTo(*TargetValues); }

	/** 
	 * Set a value in the mask
	 * @param InVertexIndex the value/vertex index to set
	 * @param InValue the value to set
	 */
	void 
	SetValue(int32 InVertexIndex, float InValue)
	{ if (Values.IsValidIndex(InVertexIndex)) Values[InVertexIndex] = InValue; }

	/** 
	 * Get a value from the mask
	 * @param InVertexIndex the value/vertex index to retrieve
	 */
	float 
	GetValue(int32 InVertexIndex) const
	{ return Values.IsValidIndex(InVertexIndex) ? Values[InVertexIndex] : 0.0f; }
	
	/** 
	* Read only version of the array holding the mask values
	*/
	const TArray<float>& 
	GetValueArray() const
	{ return Values; }

	/** Calculates Min/Max values based on values. */
	void 
	CalcRanges(float& MinValue, float& MaxValue)
	{
		MinValue = TNumericLimits<float>::Max();
		MaxValue = -TNumericLimits<float>::Max();
		const float* ValuesPtr = Values.GetData();
		for (int32 i = 0; i < Values.Num(); ++i)
		{
			MinValue = ValuesPtr[i] < MinValue ? ValuesPtr[i] : MinValue;
			MaxValue = ValuesPtr[i] > MaxValue ? ValuesPtr[i] : MaxValue;
		}
	}

	/** Name of the mask, mainly for users to differentiate */
	UPROPERTY()
	FName Name;

	/** The currently targeted parameter for the mask */
	UPROPERTY()
	//MaskTarget_PhysMesh CurrentTarget;
	uint8 CurrentTarget;

	/** The actual values stored in the mask */
	UPROPERTY()
	TArray<float> Values;

	/** Whether this mask is enabled and able to effect final mesh values */
	UPROPERTY()
	bool bEnabled;
};
