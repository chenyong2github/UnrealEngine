// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tables/NodalOffsetTable.h"

#include "LensTableUtils.h"

int32 FNodalOffsetFocusPoint::GetNumPoints() const
{
	return LocationOffset[0].GetNumKeys();
}

float FNodalOffsetFocusPoint::GetZoom(int32 Index) const
{
	return LocationOffset[0].Keys[Index].Time;
}

bool FNodalOffsetFocusPoint::AddPoint(float InZoom, const FNodalPointOffset& InData, float InputTolerance, bool /** bIsCalibrationPoint */)
{
	for(int32 Index = 0; Index < LocationDimension; ++Index)
	{
		FKeyHandle Handle = LocationOffset[Index].FindKey(InZoom, InputTolerance);
		if(Handle != FKeyHandle::Invalid())
		{
			LocationOffset[Index].SetKeyValue(Handle, InData.LocationOffset[Index]);	
		}
		else
		{
			Handle = LocationOffset[Index].AddKey(InZoom, InData.LocationOffset[Index]);
			LocationOffset[Index].SetKeyTangentMode(Handle, ERichCurveTangentMode::RCTM_Auto);
			LocationOffset[Index].SetKeyInterpMode(Handle, RCIM_Cubic);
		}
	}

	const FRotator NewRotator = InData.RotationOffset.Rotator();
	for(int32 Index = 0; Index < RotationDimension; ++Index)
	{
		FKeyHandle Handle = RotationOffset[Index].FindKey(InZoom, InputTolerance);
		if(Handle != FKeyHandle::Invalid())
		{
			RotationOffset[Index].SetKeyValue(Handle, NewRotator.GetComponentForAxis(static_cast<EAxis::Type>(Index+1)));	
		}
		else
		{
			Handle = RotationOffset[Index].AddKey(InZoom, NewRotator.GetComponentForAxis(static_cast<EAxis::Type>(Index+1)));
			RotationOffset[Index].SetKeyTangentMode(Handle, ERichCurveTangentMode::RCTM_Auto);
			RotationOffset[Index].SetKeyInterpMode(Handle, RCIM_Cubic);
		}
	}
	return true;
}

void FNodalOffsetFocusPoint::RemovePoint(float InZoomValue)
{
	for(int32 Index = 0; Index < LocationDimension; ++Index)
	{
		const FKeyHandle KeyHandle = LocationOffset[Index].FindKey(InZoomValue);
		if(KeyHandle != FKeyHandle::Invalid())
		{
			LocationOffset[Index].DeleteKey(KeyHandle);
		}
	}

	for(int32 Index = 0; Index < RotationDimension; ++Index)
	{
		const FKeyHandle KeyHandle = RotationOffset[Index].FindKey(InZoomValue);
		if(KeyHandle != FKeyHandle::Invalid())
		{
			RotationOffset[Index].DeleteKey(KeyHandle);
		}
	}
}

bool FNodalOffsetFocusPoint::IsEmpty() const
{
	return LocationOffset[0].IsEmpty();
}

bool FNodalOffsetTable::BuildParameterCurve(float InFocus, int32 ParameterIndex, EAxis::Type InAxis, FRichCurve& OutCurve) const
{
	if((ParameterIndex >= 0) && (ParameterIndex < 2) && (InAxis != EAxis::None))
	{
		if(const FNodalOffsetFocusPoint* FocusPoint = GetFocusPoint(InFocus))
		{
			if(ParameterIndex == 0)
			{
				OutCurve = FocusPoint->LocationOffset[static_cast<uint8>(InAxis) - 1];;
			}
			else
			{
				OutCurve = FocusPoint->RotationOffset[static_cast<uint8>(InAxis) - 1];
			}
			return true;
		}	
	}

	return false;
}

const FNodalOffsetFocusPoint* FNodalOffsetTable::GetFocusPoint(float InFocus) const
{
	return FocusPoints.FindByPredicate([InFocus](const FNodalOffsetFocusPoint& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
}

FNodalOffsetFocusPoint* FNodalOffsetTable::GetFocusPoint(float InFocus)
{
	return FocusPoints.FindByPredicate([InFocus](const FNodalOffsetFocusPoint& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
}

TConstArrayView<FNodalOffsetFocusPoint> FNodalOffsetTable::GetFocusPoints() const
{
	return FocusPoints;
}

TArray<FNodalOffsetFocusPoint>& FNodalOffsetTable::GetFocusPoints()
{
	return FocusPoints;
}

void FNodalOffsetTable::RemoveFocusPoint(float InFocus)
{
	LensDataTableUtils::RemoveFocusPoint(FocusPoints, InFocus);
}

void FNodalOffsetTable::RemoveZoomPoint(float InFocus, float InZoom)
{
	LensDataTableUtils::RemoveZoomPoint(FocusPoints, InFocus, InZoom);
}

bool FNodalOffsetTable::AddPoint(float InFocus, float InZoom, const FNodalPointOffset& InData, float InputTolerance, bool bIsCalibrationPoint)
{
	return LensDataTableUtils::AddPoint(FocusPoints, InFocus, InZoom, InData, InputTolerance, bIsCalibrationPoint);
}

