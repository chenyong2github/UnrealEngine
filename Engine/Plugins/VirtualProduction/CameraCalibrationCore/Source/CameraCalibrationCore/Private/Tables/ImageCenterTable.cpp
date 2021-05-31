// Copyright Epic Games, Inc. All Rights Reserved.


#include "Tables/ImageCenterTable.h"

#include "LensTableUtils.h"

int32 FImageCenterFocusPoint::GetNumPoints() const
{
	return Cx.GetNumKeys();
}

float FImageCenterFocusPoint::GetZoom(int32 Index) const
{
	return Cx.Keys[Index].Time;
}

bool FImageCenterFocusPoint::AddPoint(float InZoom, const FImageCenterInfo& InData, float InputTolerance,
	bool bIsCalibrationPoint)
{
	const FKeyHandle CxHandle = Cx.FindKey(InZoom, InputTolerance);
	if (CxHandle != FKeyHandle::Invalid())
	{
		const FKeyHandle CyHandle = Cy.FindKey(InZoom, InputTolerance);
		check(CyHandle != FKeyHandle::Invalid());
		Cx.SetKeyValue(CxHandle, InData.PrincipalPoint.X);
		Cy.SetKeyValue(CyHandle, InData.PrincipalPoint.Y);
	}
	else
	{
		//Add new zoom point
		const FKeyHandle NewKeyHandle = Cx.AddKey(InZoom, InData.PrincipalPoint.X);
		Cx.SetKeyTangentMode(NewKeyHandle, ERichCurveTangentMode::RCTM_Auto);
		Cx.SetKeyInterpMode(NewKeyHandle, RCIM_Cubic);

		Cy.AddKey(InZoom, InData.PrincipalPoint.Y, false, NewKeyHandle);
		Cy.SetKeyTangentMode(NewKeyHandle, ERichCurveTangentMode::RCTM_Auto);
		Cy.SetKeyInterpMode(NewKeyHandle, RCIM_Cubic);
	}

	return true;
}

void FImageCenterFocusPoint::RemovePoint(float InZoomValue)
{
	const FKeyHandle CxKeyHandle = Cx.FindKey(InZoomValue);
	if(CxKeyHandle != FKeyHandle::Invalid())
	{
		Cx.DeleteKey(CxKeyHandle);
	}

	const FKeyHandle CyKeyHandle = Cy.FindKey(InZoomValue);
	if (CyKeyHandle != FKeyHandle::Invalid())
	{
		Cy.DeleteKey(CyKeyHandle);
	}

}

bool FImageCenterFocusPoint::IsEmpty() const
{
	return Cx.IsEmpty();
}

bool FImageCenterTable::BuildParameterCurve(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const
{
	if(ParameterIndex >= 0 && ParameterIndex < 2)
	{
		if(const FImageCenterFocusPoint* FocusPoint = GetFocusPoint(InFocus))
		{
			if(ParameterIndex == 0)
			{
				OutCurve = FocusPoint->Cx;
			}
			else
			{
				OutCurve = FocusPoint->Cy;
			}
			return true;
		}	
	}

	return false;
}

const FImageCenterFocusPoint* FImageCenterTable::GetFocusPoint(float InFocus) const
{
	return FocusPoints.FindByPredicate([InFocus](const FImageCenterFocusPoint& Points) { return FMath::IsNearlyEqual(Points.Focus, InFocus); });
}

FImageCenterFocusPoint* FImageCenterTable::GetFocusPoint(float InFocus)
{
	return FocusPoints.FindByPredicate([InFocus](const FImageCenterFocusPoint& Points) { return FMath::IsNearlyEqual(Points.Focus, InFocus); });
}

TConstArrayView<FImageCenterFocusPoint> FImageCenterTable::GetFocusPoints() const
{
	return FocusPoints;
}

TArray<FImageCenterFocusPoint>& FImageCenterTable::GetFocusPoints()
{
	return FocusPoints;
}

void FImageCenterTable::RemoveFocusPoint(float InFocus)
{
	LensDataTableUtils::RemoveFocusPoint(FocusPoints, InFocus);
}

void FImageCenterTable::RemoveZoomPoint(float InFocus, float InZoom)
{
	LensDataTableUtils::RemoveZoomPoint(FocusPoints, InFocus, InZoom);
}

bool FImageCenterTable::AddPoint(float InFocus, float InZoom, const FImageCenterInfo& InData, float InputTolerance,
	bool bIsCalibrationPoint)
{
	return LensDataTableUtils::AddPoint(FocusPoints, InFocus, InZoom, InData, InputTolerance, bIsCalibrationPoint);
}
