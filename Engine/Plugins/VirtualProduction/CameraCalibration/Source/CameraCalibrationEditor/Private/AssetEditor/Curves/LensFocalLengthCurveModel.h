// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "LensDataCurveModel.h"

#include "Curves/RichCurve.h"

/**
 * Handles focal length curves to be displayed
 */
class FLensFocalLengthCurveModel : public FLensDataCurveModel
{
public:

	FLensFocalLengthCurveModel(ULensFile* InOwner, float InFocus, int32 InParameterIndex);

	//~ Begin FRichCurveEditorModel interface
	virtual void AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles) override;
	virtual void RemoveKeys(TArrayView<const FKeyHandle> InKeys) override;
	virtual void SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions, EPropertyChangeType::Type ChangeType) override;
	//~ End FRichCurveEditorModel interface

protected:

	/** Whether a key is protected or not. If it was added with calibration data, it is. */
	bool IsKeyProtected(FKeyHandle InHandle) const;

private:

	/** Input focus we are currently showing */
	float Focus = 0.0f;

	/**
	 * Focal length parameter index
	 *-1: Focal length curve in mm based on Fx and SensorDimensions.Width
	 * 0: Fx
	 * 1: Fy
	 */
	int32 ParameterIndex = INDEX_NONE; //Defaults to Focal Length derived curve
};
