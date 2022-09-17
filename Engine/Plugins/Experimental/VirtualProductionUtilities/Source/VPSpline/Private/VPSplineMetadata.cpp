// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPSplineMetadata.h"
#include "VPSplineComponent.h"
#include "VPSplineLog.h"

void UVPSplineMetadata::InsertPoint(int32 Index, float t, bool bClosedLoop)
{
	if (Index < 0)
	{
		UE_LOG(LogVPSpline, Error, TEXT("InsertPoint - invalid Index: %d"), Index);
		return;
	}

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
		int32 PrevIndex = (bClosedLoop && Index == 0 ? NumPoints - 1 : Index - 1);
		bool bHasPrevIndex = (PrevIndex >= 0 && PrevIndex < NumPoints);
		float NewFocalLengthVal = FocalLength.Points[Index].OutVal;
		float NewApertureVal = Aperture.Points[Index].OutVal;
		float NewFocusDistanceVal = FocusDistance.Points[Index].OutVal;
		float NewNormalizedPositionVal = NormalizedPosition.Points[Index].OutVal;

		if (bHasPrevIndex)
		{
			float PrevFocalLengthVal = FocalLength.Points[PrevIndex].OutVal;
			float PrevApertureVal = Aperture.Points[PrevIndex].OutVal;
			float PrevFocusDistanceVal = FocusDistance.Points[PrevIndex].OutVal;
			float PrevNormalizedPositionVal = NormalizedPosition.Points[PrevIndex].OutVal;
			NewFocalLengthVal = FMath::LerpStable(PrevFocalLengthVal, NewFocalLengthVal, t);
			NewApertureVal = FMath::LerpStable(PrevApertureVal, NewApertureVal, t);
			NewFocusDistanceVal = FMath::LerpStable(PrevFocusDistanceVal, NewFocusDistanceVal, t);
			NewNormalizedPositionVal = FMath::LerpStable(PrevNormalizedPositionVal, NewNormalizedPositionVal, t);
		}
		FInterpCurvePoint<float> NewNormalizedPosition(InputKey, NewNormalizedPositionVal);
		NormalizedPosition.Points.Insert(NewNormalizedPosition, Index);

		FInterpCurvePoint<float> NewFocalLength(InputKey, NewFocalLengthVal);
		FocalLength.Points.Insert(NewFocalLength, Index);

		FInterpCurvePoint<float> NewAperture(InputKey, NewApertureVal);
		Aperture.Points.Insert(NewAperture, Index);

		FInterpCurvePoint<float> NewFocusDistance(InputKey, NewFocusDistanceVal);
		FocusDistance.Points.Insert(NewFocusDistance, Index);

		for (int32 i = Index + 1; i < FocalLength.Points.Num(); ++i)
		{
			FocalLength.Points[i].InVal += 1.0f;
			Aperture.Points[i].InVal += 1.0f;
			FocusDistance.Points[i].InVal += 1.0f;
			NormalizedPosition.Points[i].InVal += 1.0f;
		}
	}
}

void UVPSplineMetadata::UpdatePoint(int32 Index, float t, bool bClosedLoop)
{
	int32 NumPoints = FocalLength.Points.Num();
	if (!FocalLength.Points.IsValidIndex(Index))
	{
		UE_LOG(LogVPSpline, Error, TEXT("UpdatePoint - invalid Index: %d"), Index);
		return;
	}

	int32 PrevIndex = (bClosedLoop && Index == 0 ? NumPoints - 1 : Index - 1);
	int32 NextIndex = (bClosedLoop && Index + 1 > NumPoints ? 0 : Index + 1);

	bool bHasPrevIndex = (PrevIndex >= 0 && PrevIndex < NumPoints);
	bool bHasNextIndex = (NextIndex >= 0 && NextIndex < NumPoints);

	Modify();

	if (bHasPrevIndex && bHasNextIndex)
	{
		float PrevNormalizedPositionVal = NormalizedPosition.Points[PrevIndex].OutVal;
		float PrevFocalLengthVal = FocalLength.Points[PrevIndex].OutVal;
		float PrevApertureVal = Aperture.Points[PrevIndex].OutVal;
		float PrevFocusDistanceVal = FocusDistance.Points[PrevIndex].OutVal;
		float NextNormalizedPositionVal = NormalizedPosition.Points[NextIndex].OutVal;
		float NextFocalLengthVal = FocalLength.Points[NextIndex].OutVal;
		float NextApertureVal = Aperture.Points[NextIndex].OutVal;
		float NextFocusDistanceVal = FocusDistance.Points[NextIndex].OutVal;

		NormalizedPosition.Points[Index].OutVal = FMath::LerpStable(PrevNormalizedPositionVal, NextNormalizedPositionVal, t);
		FocalLength.Points[Index].OutVal = FMath::LerpStable(PrevFocalLengthVal, NextFocalLengthVal, t);
		Aperture.Points[Index].OutVal = FMath::LerpStable(PrevApertureVal, NextApertureVal, t);
		FocusDistance.Points[Index].OutVal = FMath::LerpStable(PrevFocusDistanceVal, NextFocusDistanceVal, t);
	}
}

void UVPSplineMetadata::AddPoint(float InputKey)
{
	Modify();

	float NewNormalizedPositionVal = -1.0f;
	float NewFocalLengthVal = 35.0f;
	float NewApertureVal = 2.8f;
	float NewFocusDistanceVal = 100000.f;
	
	int Index = FocalLength.Points.Num() - 1;
	if (Index >= 0)
	{
		NewFocalLengthVal = FocalLength.Points[Index].OutVal;
		NewApertureVal = Aperture.Points[Index].OutVal;
		NewFocusDistanceVal = FocusDistance.Points[Index].OutVal;
		NewNormalizedPositionVal = NormalizedPosition.Points[Index].OutVal;
	}

	float NewInputKey = static_cast<float>(++Index);
	FocalLength.Points.Emplace(NewInputKey, NewFocalLengthVal);
	Aperture.Points.Emplace(NewInputKey, NewApertureVal);
	FocusDistance.Points.Emplace(NewInputKey, NewFocusDistanceVal);
	NormalizedPosition.Points.Emplace(NewInputKey, NewNormalizedPositionVal);

}

void UVPSplineMetadata::RemovePoint(int32 Index)
{
	check(Index < FocalLength.Points.Num());

	Modify();
	NormalizedPosition.Points.RemoveAt(Index);
	FocalLength.Points.RemoveAt(Index);
	Aperture.Points.RemoveAt(Index);
	FocusDistance.Points.RemoveAt(Index);

	for (int32 i = Index; i < FocalLength.Points.Num(); ++i)
	{
		NormalizedPosition.Points[i].InVal -= 1.0f;
		FocalLength.Points[i].InVal -= 1.0f;
		Aperture.Points[i].InVal -= 1.0f;
		FocusDistance.Points[i].InVal -= 1.0f;
	}
}

void UVPSplineMetadata::DuplicatePoint(int32 Index)
{
	check(Index < FocalLength.Points.Num());

	float NewValue = -1.0f;
	float CurrValue = NormalizedPosition.Points[Index].OutVal;

	if (NormalizedPosition.Points.Num() > 1)
	{
		float PrevValue = CurrValue;
		float NextValue = CurrValue;

		if (Index + 1 < NormalizedPosition.Points.Num())
		{
			NewValue = (CurrValue + NormalizedPosition.Points[Index + 1].OutVal) * 0.5;
		}
		else if (Index + 1 == NormalizedPosition.Points.Num())
		{
			NewValue = CurrValue;
			CurrValue = (CurrValue + NormalizedPosition.Points[Index - 1].OutVal) * 0.5;
		}
	}

	Modify();
	NormalizedPosition.Points.Insert(FInterpCurvePoint<float>(NormalizedPosition.Points[Index]), Index);
	FocalLength.Points.Insert(FInterpCurvePoint<float>(FocalLength.Points[Index]), Index);
	Aperture.Points.Insert(FInterpCurvePoint<float>(Aperture.Points[Index]), Index);
	FocusDistance.Points.Insert(FInterpCurvePoint<float>(FocusDistance.Points[Index]), Index);

	NormalizedPosition.Points[Index].OutVal = CurrValue;
	NormalizedPosition.Points[Index+1].OutVal = NewValue;


	for (int32 i = Index + 1; i < FocalLength.Points.Num(); ++i)
	{
		NormalizedPosition.Points[i].InVal += 1.0f;
		FocalLength.Points[i].InVal += 1.0f;
		Aperture.Points[i].InVal += 1.0f;
		FocusDistance.Points[i].InVal += 1.0f;
	}

}

void UVPSplineMetadata::CopyPoint(const USplineMetadata* FromSplineMetadata, int32 FromIndex, int32 ToIndex)
{
	check(FromSplineMetadata != nullptr);

	if (const UVPSplineMetadata* FromMetadata = Cast<UVPSplineMetadata>(FromSplineMetadata))
	{
		check(ToIndex < FocalLength.Points.Num());
		check(FromIndex < FromMetadata->FocalLength.Points.Num());

		Modify();
		FocalLength.Points[ToIndex].OutVal = FromMetadata->FocalLength.Points[FromIndex].OutVal;
		Aperture.Points[ToIndex].OutVal = FromMetadata->Aperture.Points[FromIndex].OutVal;
		FocusDistance.Points[ToIndex].OutVal = FromMetadata->FocusDistance.Points[FromIndex].OutVal;
	}
}

void UVPSplineMetadata::Reset(int32 NumPoints)
{
	Modify();
	FocalLength.Points.Reset(NumPoints);
	Aperture.Points.Reset(NumPoints);
	FocusDistance.Points.Reset(NumPoints);
	NormalizedPosition.Points.Reset(NumPoints);
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

void UVPSplineMetadata::Fixup(int32 NumPoints, USplineComponent* SplineComp)
{
	const FVPSplineCurveDefaults& Defaults = CastChecked<UVPSplineComponent>(SplineComp)->CameraSplineDefaults;
#if WITH_EDITORONLY_DATA
	FixupCurve(FocalLength, Defaults.DefaultFocalLength, NumPoints);
	FixupCurve(Aperture, Defaults.DefaultAperture, NumPoints);
	FixupCurve(FocusDistance, Defaults.DefaultFocusDistance, NumPoints);
	FixupCurve(NormalizedPosition, Defaults.DefaultNormalizedPosition, NumPoints);
#endif
}