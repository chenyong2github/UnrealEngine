// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensNodalOffsetCurveModel.h"

#include "CameraCalibrationEditorLog.h"
#include "LensFile.h"



FLensNodalOffsetCurveModel::FLensNodalOffsetCurveModel(ULensFile* InOwner, float InFocus, int32 InParameterIndex, EAxis::Type InAxis)
	: FLensDataCurveModel(InOwner)
	, Focus(InFocus)
	, ParameterIndex(InParameterIndex)
	, Axis(InAxis)
{
	//No curve to show when selecting root nodal offset category.
	bIsCurveValid = LensFile->NodalOffsetTable.BuildParameterCurve(Focus, ParameterIndex, Axis, CurrentCurve);
}

void FLensNodalOffsetCurveModel::SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions, EPropertyChangeType::Type ChangeType)
{
	FRichCurveEditorModel::SetKeyPositions(InKeys, InKeyPositions, ChangeType);

	if (FNodalOffsetFocusPoint* Point = LensFile->NodalOffsetTable.GetFocusPoint(Focus))
	{
		FRichCurve* ActiveCurve = nullptr;
		if(ParameterIndex == 0)
		{
			ActiveCurve = &Point->LocationOffset[static_cast<uint8>(Axis) - 1];
		}
		else
		{
			ActiveCurve = &Point->RotationOffset[static_cast<uint8>(Axis) - 1];
		}
		
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			const FKeyHandle Handle = InKeys[Index];
			const int32 KeyIndex = CurrentCurve.GetIndexSafe(Handle);
			if (KeyIndex != INDEX_NONE)
			{
				ActiveCurve->Keys[KeyIndex] = CurrentCurve.GetKey(Handle);
			}
		}

		ActiveCurve->AutoSetTangents();
	}
}


