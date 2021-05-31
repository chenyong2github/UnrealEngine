// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tables/DistortionParametersTable.h"

#include "LensTableUtils.h"


int32 FDistortionFocusPoint::GetNumPoints() const
{
	return MapBlendingCurve.GetNumKeys();
}

float FDistortionFocusPoint::GetZoom(int32 Index) const
{
	return MapBlendingCurve.Keys[Index].Time;
}

bool FDistortionFocusPoint::AddPoint(float InZoom, const FDistortionInfo& InData, float InputTolerance, bool bIsCalibrationPoint)
{
	const FKeyHandle Handle = MapBlendingCurve.FindKey(InZoom, InputTolerance);
	if(Handle != FKeyHandle::Invalid())
	{
		const int32 PointIndex = MapBlendingCurve.GetIndexSafe(Handle);
		check(ZoomPoints.IsValidIndex(PointIndex));

		//No need to update map curve since x == y
		ZoomPoints[PointIndex].DistortionInfo = InData;
	}
	else
	{
		//Add new zoom point
		const FKeyHandle NewKeyHandle = MapBlendingCurve.AddKey(InZoom, InZoom);
		MapBlendingCurve.SetKeyTangentMode(NewKeyHandle, ERichCurveTangentMode::RCTM_Auto);
		MapBlendingCurve.SetKeyInterpMode(NewKeyHandle, RCIM_Cubic);

		//Insert point at the same index as the curve key
		const int32 KeyIndex = MapBlendingCurve.GetIndexSafe(NewKeyHandle);
		FDistortionZoomPoint NewZoomPoint;
		NewZoomPoint.Zoom = InZoom;
		NewZoomPoint.DistortionInfo = InData;
		ZoomPoints.Insert(MoveTemp(NewZoomPoint), KeyIndex);
	}

	return true;
}

void FDistortionFocusPoint::RemovePoint(float InZoomValue)
{
	const int32 FoundIndex = ZoomPoints.IndexOfByPredicate([InZoomValue](const FDistortionZoomPoint& Point) { return FMath::IsNearlyEqual(Point.Zoom, InZoomValue); });
	if(FoundIndex != INDEX_NONE)
	{
		ZoomPoints.RemoveAt(FoundIndex);
	}

	const FKeyHandle KeyHandle = MapBlendingCurve.FindKey(InZoomValue);
	if(KeyHandle != FKeyHandle::Invalid())
	{
		MapBlendingCurve.DeleteKey(KeyHandle);
	}
}

bool FDistortionFocusPoint::IsEmpty() const
{
	return MapBlendingCurve.IsEmpty();
}

void FDistortionFocusPoint::SetParameterValue(int32 InZoomIndex, float InZoomValue, int32 InParameterIndex, float InParameterValue)
{
	//We can't move keys on the time axis so our indices should match
	if (ZoomPoints.IsValidIndex(InZoomIndex))
	{
		if (ensure(FMath::IsNearlyEqual(ZoomPoints[InZoomIndex].Zoom, InZoomValue)))
		{
			ZoomPoints[InZoomIndex].DistortionInfo.Parameters[InParameterIndex] = InParameterValue;
		}
	}
}

bool FDistortionTable::BuildParameterCurve(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const
{
	if (const FDistortionFocusPoint* ThisFocusPoints = GetFocusPoint(InFocus))
	{
		//Go over each zoom points
		for (const FDistortionZoomPoint& ZoomPoint : ThisFocusPoints->ZoomPoints)
		{
			if (ZoomPoint.DistortionInfo.Parameters.IsValidIndex(ParameterIndex))
			{
				const FKeyHandle Handle = OutCurve.AddKey(ZoomPoint.Zoom, ZoomPoint.DistortionInfo.Parameters[ParameterIndex]);
				OutCurve.SetKeyInterpMode(Handle, RCIM_Linear);
			}
			else //Defaults to map blending
			{
				OutCurve = ThisFocusPoints->MapBlendingCurve;
				return true;
			}
		}

		return true;
	}

	return false;
}

const FDistortionFocusPoint* FDistortionTable::GetFocusPoint(float InFocus) const
{
	return FocusPoints.FindByPredicate([InFocus](const FDistortionFocusPoint& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
}

FDistortionFocusPoint* FDistortionTable::GetFocusPoint(float InFocus)
{
	return FocusPoints.FindByPredicate([InFocus](const FDistortionFocusPoint& Point) { return FMath::IsNearlyEqual(Point.Focus, InFocus); });
}

TConstArrayView<FDistortionFocusPoint> FDistortionTable::GetFocusPoints() const
{
	return FocusPoints;
}

TArray<FDistortionFocusPoint>& FDistortionTable::GetFocusPoints()
{
	return FocusPoints;
}

void FDistortionTable::RemoveFocusPoint(float InFocus)
{
	LensDataTableUtils::RemoveFocusPoint(FocusPoints, InFocus);
}

void FDistortionTable::RemoveZoomPoint(float InFocus, float InZoom)
{
	LensDataTableUtils::RemoveZoomPoint(FocusPoints, InFocus, InZoom);
}

bool FDistortionTable::AddPoint(float InFocus, float InZoom, const FDistortionInfo& InData, float InputTolerance, bool bIsCalibrationPoint)
{
	return LensDataTableUtils::AddPoint(FocusPoints, InFocus, InZoom, InData, InputTolerance, bIsCalibrationPoint);
}



