// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Templates/Tuple.h"
#include "CurveDataAbstraction.h"

struct FCurveEditorScreenSpace;

class FCurveEditor;
class FCurveModel;

/**
 * Represents a buffered curve which can be applied to a standard curve model
 */
class IBufferedCurveModel
{
public:
	IBufferedCurveModel(TArray<FKeyPosition>&& InKeyPositions, TArray<FKeyAttributes>&& InKeyAttributes, const FString& InIntentionName, const double InValueMin, const double InValueMax) 
	: KeyPositions(MoveTemp(InKeyPositions))
	, KeyAttributes(MoveTemp(InKeyAttributes))
	, IntentionName(InIntentionName)
	, ValueMin(InValueMin)
	, ValueMax(InValueMax)
	{}

	virtual ~IBufferedCurveModel() = default;

	/** draws the curve into an array of (input, output) pairs with a given screen space */
	virtual void DrawCurve(const FCurveEditor& CurveEditor, const FCurveEditorScreenSpace& ScreenSpace, TArray<TTuple<double, double>>& InterpolatingPoints) const = 0;
	
	/**
	 * Retrieve all key positions stored in this buffered curve
	 *
	 * @param OutKeyPositions         Array to receive key positions, one per index of InKeys
	 */
	FORCEINLINE void GetKeyPositions(TArray<FKeyPosition>& OutKeyPositions) const { OutKeyPositions = KeyPositions; }
	
	/**
	 * Retrieve all key attributes stored in this buffered curve
	 * 
	 * @param OutAttributes         Array to receive key attributes, one per index of InKeys
	 */
	FORCEINLINE void GetKeyAttributes(TArray<FKeyAttributes>& OutKeyAttributes) const { OutKeyAttributes = KeyAttributes; }

	/**
	 * Returns the intention name for this buffered curve. This is an internal name used to try to match 
	 * different curves with each other. See the CurveModel's GetIntentionName method for more detail on
	 * what an intention name is
	 */
	FORCEINLINE FString GetIntentionName() const { return IntentionName; }

	/** returns the lowest output value in curve space for this buffered curve */
	FORCEINLINE double GetValueMin() const { return ValueMin; }

	/** returns the highest output value in curve space for this buffered curve */
	FORCEINLINE double GetValueMax() const { return ValueMax; }

protected:
	TArray<FKeyPosition> KeyPositions;
	TArray<FKeyAttributes> KeyAttributes;
	FString IntentionName;
	double ValueMin, ValueMax;
};