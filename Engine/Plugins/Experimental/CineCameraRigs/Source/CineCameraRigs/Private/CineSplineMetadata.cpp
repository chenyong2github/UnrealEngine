// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineSplineMetadata.h"
#include "CineSplineComponent.h"
#include "CineSplineLog.h"

void UCineSplineMetadata::InsertPoint(int32 Index, float Alpha, bool bClosedLoop)
{
	if (Index < 0)
	{
		UE_LOG(LogCineSpline, Error, TEXT("InsertPoint - invalid Index: %d"), Index);
		return;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	Modify();

	int32 NumPoints = FocalLength.Points.Num();
	float InputKey = static_cast<float>(Index);
	if (Index >= NumPoints)
	{
		// Just add point to the end instead of trying to insert
		AddPoint(InputKey);
	}
	else
	{
		const int32 PrevIndex = (bClosedLoop && Index == 0 ? NumPoints - 1 : Index - 1);
		const bool bHasPrevIndex = (PrevIndex >= 0 && PrevIndex < NumPoints);
		float NewFocalLengthVal = FocalLength.Points[Index].OutVal;
		float NewApertureVal = Aperture.Points[Index].OutVal;
		float NewFocusDistanceVal = FocusDistance.Points[Index].OutVal;
		float NewCustomPositionVal = CustomPosition.Points[Index].OutVal;
		FQuat NewRotationVal = CameraRotation.Points[Index].OutVal;
		if (bHasPrevIndex)
		{
			float PrevFocalLengthVal = FocalLength.Points[PrevIndex].OutVal;
			float PrevApertureVal = Aperture.Points[PrevIndex].OutVal;
			float PrevFocusDistanceVal = FocusDistance.Points[PrevIndex].OutVal;
			float PrevCustomPositionVal = CustomPosition.Points[PrevIndex].OutVal;
			FQuat PrevRotationVal = CameraRotation.Points[PrevIndex].OutVal;
			NewFocalLengthVal = FMath::LerpStable(PrevFocalLengthVal, NewFocalLengthVal, Alpha);
			NewApertureVal = FMath::LerpStable(PrevApertureVal, NewApertureVal, Alpha);
			NewFocusDistanceVal = FMath::LerpStable(PrevFocusDistanceVal, NewFocusDistanceVal, Alpha);
			NewCustomPositionVal = FMath::LerpStable(PrevCustomPositionVal, NewCustomPositionVal, Alpha);
			NewRotationVal = FMath::LerpStable(PrevRotationVal, NewRotationVal, Alpha);
		}
		FInterpCurvePoint<float> NewCustomPosition(InputKey, NewCustomPositionVal);
		CustomPosition.Points.Insert(NewCustomPosition, Index);

		FInterpCurvePoint<float> NewFocalLength(InputKey, NewFocalLengthVal);
		FocalLength.Points.Insert(NewFocalLength, Index);

		FInterpCurvePoint<float> NewAperture(InputKey, NewApertureVal);
		Aperture.Points.Insert(NewAperture, Index);

		FInterpCurvePoint<float> NewFocusDistance(InputKey, NewFocusDistanceVal);
		FocusDistance.Points.Insert(NewFocusDistance, Index);

		FInterpCurvePoint<FQuat> NewRotation(InputKey, NewRotationVal);
		CameraRotation.Points.Insert(NewRotation, Index);


		for (int32 i = Index + 1; i < FocalLength.Points.Num(); ++i)
		{
			FocalLength.Points[i].InVal += 1.0f;
			Aperture.Points[i].InVal += 1.0f;
			FocusDistance.Points[i].InVal += 1.0f;
			CustomPosition.Points[i].InVal += 1.0f;
			CameraRotation.Points[i].InVal += 1.0f;
		}
	}
}

void UCineSplineMetadata::UpdatePoint(int32 Index, float Alpha, bool bClosedLoop)
{
	int32 NumPoints = FocalLength.Points.Num();
	if (!FocalLength.Points.IsValidIndex(Index))
	{
		UE_LOG(LogCineSpline, Error, TEXT("UpdatePoint - invalid Index: %d"), Index);
		return;
	}
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

	int32 PrevIndex = (bClosedLoop && Index == 0 ? NumPoints - 1 : Index - 1);
	int32 NextIndex = (bClosedLoop && Index + 1 > NumPoints ? 0 : Index + 1);

	bool bHasPrevIndex = (PrevIndex >= 0 && PrevIndex < NumPoints);
	bool bHasNextIndex = (NextIndex >= 0 && NextIndex < NumPoints);

	Modify();

	if (bHasPrevIndex && bHasNextIndex)
	{
		float PrevCustomPositionVal = CustomPosition.Points[PrevIndex].OutVal;
		float PrevFocalLengthVal = FocalLength.Points[PrevIndex].OutVal;
		float PrevApertureVal = Aperture.Points[PrevIndex].OutVal;
		float PrevFocusDistanceVal = FocusDistance.Points[PrevIndex].OutVal;
		FQuat PrevRotationVal = CameraRotation.Points[PrevIndex].OutVal;
		float NextCustomPositionVal = CustomPosition.Points[NextIndex].OutVal;
		float NextFocalLengthVal = FocalLength.Points[NextIndex].OutVal;
		float NextApertureVal = Aperture.Points[NextIndex].OutVal;
		float NextFocusDistanceVal = FocusDistance.Points[NextIndex].OutVal;
		FQuat NextRotationVal = CameraRotation.Points[NextIndex].OutVal;

		CustomPosition.Points[Index].OutVal = FMath::LerpStable(PrevCustomPositionVal, NextCustomPositionVal, Alpha);
		FocalLength.Points[Index].OutVal = FMath::LerpStable(PrevFocalLengthVal, NextFocalLengthVal, Alpha);
		Aperture.Points[Index].OutVal = FMath::LerpStable(PrevApertureVal, NextApertureVal, Alpha);
		FocusDistance.Points[Index].OutVal = FMath::LerpStable(PrevFocusDistanceVal, NextFocusDistanceVal, Alpha);
		CameraRotation.Points[Index].OutVal = FMath::LerpStable(PrevRotationVal, NextRotationVal, Alpha);
	}
}

void UCineSplineMetadata::AddPoint(float InputKey)
{
	Modify();

	float NewCustomPositionVal = -1.0f;
	float NewFocalLengthVal = 35.0f;
	float NewApertureVal = 2.8f;
	float NewFocusDistanceVal = 100000.f;
	FQuat NewRotationVal = FQuat::Identity;
	
	int Index = FocalLength.Points.Num() - 1;
	if (Index >= 0)
	{
		NewFocalLengthVal = FocalLength.Points[Index].OutVal;
		NewApertureVal = Aperture.Points[Index].OutVal;
		NewFocusDistanceVal = FocusDistance.Points[Index].OutVal;
		NewCustomPositionVal = CustomPosition.Points[Index].OutVal + 1.0f;
		NewRotationVal = CameraRotation.Points[Index].OutVal;
	}

	float NewInputKey = static_cast<float>(++Index);
	FocalLength.Points.Emplace(NewInputKey, NewFocalLengthVal);
	Aperture.Points.Emplace(NewInputKey, NewApertureVal);
	FocusDistance.Points.Emplace(NewInputKey, NewFocusDistanceVal);
	CustomPosition.Points.Emplace(NewInputKey, NewCustomPositionVal);
	CameraRotation.Points.Emplace(NewInputKey, NewRotationVal);
}

void UCineSplineMetadata::RemovePoint(int32 Index)
{
	check(Index < FocalLength.Points.Num());

	Modify();
	CustomPosition.Points.RemoveAt(Index);
	FocalLength.Points.RemoveAt(Index);
	Aperture.Points.RemoveAt(Index);
	FocusDistance.Points.RemoveAt(Index);
	CameraRotation.Points.RemoveAt(Index);

	for (int32 i = Index; i < FocalLength.Points.Num(); ++i)
	{
		CustomPosition.Points[i].InVal -= 1.0f;
		FocalLength.Points[i].InVal -= 1.0f;
		Aperture.Points[i].InVal -= 1.0f;
		FocusDistance.Points[i].InVal -= 1.0f;
		CameraRotation.Points[i].InVal -= 1.0f;
	}
}

void UCineSplineMetadata::DuplicatePoint(int32 Index)
{
	check(Index < FocalLength.Points.Num());

	int32 NumPoints = FocalLength.Points.Num();
	float NewValue = CustomPosition.Points[Index].OutVal + 1.0f;
	if (Index < NumPoints - 1)
	{
		NewValue = (CustomPosition.Points[Index].OutVal + CustomPosition.Points[Index + 1].OutVal) * 0.5;
	}

	Modify();
	CustomPosition.Points.Insert(FInterpCurvePoint<float>(CustomPosition.Points[Index]), Index);
	FocalLength.Points.Insert(FInterpCurvePoint<float>(FocalLength.Points[Index]), Index);
	Aperture.Points.Insert(FInterpCurvePoint<float>(Aperture.Points[Index]), Index);
	FocusDistance.Points.Insert(FInterpCurvePoint<float>(FocusDistance.Points[Index]), Index);
	CameraRotation.Points.Insert(FInterpCurvePoint<FQuat>(CameraRotation.Points[Index]), Index);
	CustomPosition.Points[Index+1].OutVal = NewValue;


	for (int32 i = Index + 1; i < FocalLength.Points.Num(); ++i)
	{
		CustomPosition.Points[i].InVal += 1.0f;
		FocalLength.Points[i].InVal += 1.0f;
		Aperture.Points[i].InVal += 1.0f;
		FocusDistance.Points[i].InVal += 1.0f;
		CameraRotation.Points[i].InVal += 1.0f;
	}

}

void UCineSplineMetadata::CopyPoint(const USplineMetadata* FromSplineMetadata, int32 FromIndex, int32 ToIndex)
{
	check(FromSplineMetadata != nullptr);

	if (const UCineSplineMetadata* FromMetadata = Cast<UCineSplineMetadata>(FromSplineMetadata))
	{
		check(ToIndex < FocalLength.Points.Num());
		check(FromIndex < FromMetadata->FocalLength.Points.Num());

		Modify();
		FocalLength.Points[ToIndex].OutVal = FromMetadata->FocalLength.Points[FromIndex].OutVal;
		Aperture.Points[ToIndex].OutVal = FromMetadata->Aperture.Points[FromIndex].OutVal;
		FocusDistance.Points[ToIndex].OutVal = FromMetadata->FocusDistance.Points[FromIndex].OutVal;
		CustomPosition.Points[ToIndex].OutVal = FromMetadata->CustomPosition.Points[FromIndex].OutVal;
		CameraRotation.Points[ToIndex].OutVal = FromMetadata->CameraRotation.Points[FromIndex].OutVal;
	}
}

void UCineSplineMetadata::Reset(int32 NumPoints)
{
	Modify();
	FocalLength.Points.Reset(NumPoints);
	Aperture.Points.Reset(NumPoints);
	FocusDistance.Points.Reset(NumPoints);
	CustomPosition.Points.Reset(NumPoints);
	CameraRotation.Points.Reset(NumPoints);
}

#if WITH_EDITORONLY_DATA
template <class T>
void FixupCurve(FInterpCurve<T>& Curve, const T& DefaultValue, int32 NumPoints)
{
	// Fixup bad InVal values from when the add operation below used the wrong value
	for (int32 PointIndex = 0; PointIndex < Curve.Points.Num(); PointIndex++)
	{
		float InVal = PointIndex;
		Curve.Points[PointIndex].InVal = InVal;
	}

	while (Curve.Points.Num() < NumPoints)
	{
		// InVal is the point index which is ascending so use previous point plus one.
		float InVal = Curve.Points.Num() > 0 ? Curve.Points[Curve.Points.Num() - 1].InVal + 1.0f : 0.0f;
		Curve.Points.Add(FInterpCurvePoint<T>(InVal, DefaultValue));
	}

	if (Curve.Points.Num() > NumPoints)
	{
		Curve.Points.RemoveAt(NumPoints, Curve.Points.Num() - NumPoints);
	}
}
#endif

void UCineSplineMetadata::Fixup(int32 NumPoints, USplineComponent* SplineComp)
{
	const FCineSplineCurveDefaults& Defaults = CastChecked<UCineSplineComponent>(SplineComp)->CameraSplineDefaults;
#if WITH_EDITORONLY_DATA
	FixupCurve(FocalLength, Defaults.DefaultFocalLength, NumPoints);
	FixupCurve(Aperture, Defaults.DefaultAperture, NumPoints);
	FixupCurve(FocusDistance, Defaults.DefaultFocusDistance, NumPoints);
	FixupCurve(CustomPosition, Defaults.DefaultCustomPosition, NumPoints);
	FixupCurve(CameraRotation, Defaults.DefaultCameraRotation, NumPoints);
#endif
}