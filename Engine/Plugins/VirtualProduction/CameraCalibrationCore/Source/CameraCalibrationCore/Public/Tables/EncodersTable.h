// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "Engine/EngineTypes.h"
#include "LensData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "EncodersTable.generated.h"


/**
 * Encoder table containing mapping from raw input value to nominal value
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FEncodersTable 
{
	GENERATED_BODY()

public:
	/** Returns number of focus points */
	int32 GetNumFocusPoints() const;

	/** Returns  value for a given index */
	float GetFocusInput(int32 Index) const;
	
	/** Returns zoom value for a given index */
	float GetFocusValue(int32 Index) const;

	/** Returns number of focus points */
	int32 GetNumIrisPoints() const;

	/** Returns  value for a given index */
	float GetIrisInput(int32 Index) const;
	
	/** Returns zoom value for a given index */
	float GetIrisValue(int32 Index) const;	


public:
	/** Focus curve from encoder values to nominal values */
	UPROPERTY()
	FRichCurve Focus;

	/** Iris curve from encoder values to nominal values */
	UPROPERTY()
	FRichCurve Iris;
};

