// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensFocalLengthCurveModel.h"

#include "CameraCalibrationEditorLog.h"
#include "LensFile.h"



FLensFocalLengthCurveModel::FLensFocalLengthCurveModel(ULensFile* InOwner, float InFocus, int32 InParameterIndex)
	: FLensDataCurveModel(InOwner)
	, Focus(InFocus)
	, ParameterIndex(InParameterIndex)
{
	if(ParameterIndex != INDEX_NONE)
	{
		LensFile->FocalLengthTable.BuildParameterCurve(Focus, ParameterIndex, CurrentCurve);
	}
	else
	{
		if (FFocalLengthFocusPoint* Points = LensFile->FocalLengthTable.GetFocusPoint(Focus))
		{
			auto Iterator = Points->Fx.GetKeyHandleIterator();
			for (const FRichCurveKey& Key : Points->Fx.GetConstRefOfKeys())
			{
				CurrentCurve.AddKey(Key.Time, Key.Value * LensFile->LensInfo.SensorDimensions.X, false, *Iterator);
				CurrentCurve.SetKeyInterpMode(*Iterator, RCIM_Cubic);
				CurrentCurve.SetKeyTangentMode(*Iterator, ERichCurveTangentMode::RCTM_Auto);
				++Iterator;
			}
		}
	}
	
	bIsCurveValid = true;
}

void FLensFocalLengthCurveModel::AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles)
{
	//Support add keys only for focal length curve
	if(ParameterIndex != INDEX_NONE)
	{
		return;
	}
	
	FRichCurveEditorModel::AddKeys(InKeyPositions, InAttributes, OutKeyHandles);

	if (OutKeyHandles == nullptr)
	{
		return;
	}

	if (FFocalLengthFocusPoint* Points = LensFile->FocalLengthTable.GetFocusPoint(Focus))
	{
		for (const TOptional<FKeyHandle>& OptionalHandle : *OutKeyHandles)
		{
			if (OptionalHandle.IsSet())
			{
				//Evaluate FxFy at the new point location to get interpolated aspect ratio
				//Adjust Fx directly base don focal length but fix Fy using the interpolated aspect ratio
				const FKeyHandle NewKeyHandle = OptionalHandle.GetValue();
				const FRichCurveKey& NewKey = CurrentCurve.GetKey(NewKeyHandle);
				const float EvalFx = Points->Fx.Eval(NewKey.Time);
				const float EvalFy = Points->Fy.Eval(NewKey.Time);

				float AspectRatio = LensFile->LensInfo.SensorDimensions.Y / LensFile->LensInfo.SensorDimensions.X;
				if(ensure(FMath::IsNearlyZero(EvalFx) == false))
				{
					AspectRatio	= EvalFy / EvalFx;
				}

				const float NewFx = NewKey.Value / LensFile->LensInfo.SensorDimensions.X;
				const float NewFy = NewFx * AspectRatio;

				FFocalLengthInfo Info;
				Info.FxFy = FVector2D(NewFx, NewFy);
				Points->AddPoint(NewKey.Time, Info, ULensFile::InputTolerance, false);
				Points->Fx.AddKey(NewKey.Time, NewFx, false, NewKeyHandle);
				Points->Fx.SetKeyTangentMode(NewKeyHandle, NewKey.TangentMode);
				Points->Fx.SetKeyInterpMode(NewKeyHandle, NewKey.InterpMode);
				Points->Fy.AddKey(NewKey.Time, NewFy, false, NewKeyHandle);
				Points->Fy.SetKeyTangentMode(NewKeyHandle, NewKey.TangentMode);
				Points->Fy.SetKeyInterpMode(NewKeyHandle, NewKey.InterpMode);
			}
		}
	}
}

void FLensFocalLengthCurveModel::RemoveKeys(TArrayView<const FKeyHandle> InKeys)
{
	//Support remove keys only for focal length curve
	if(ParameterIndex != INDEX_NONE)
	{
		return;
	}
	
	TArray<FKeyHandle> FilteredHandles;
	FilteredHandles.Reserve(InKeys.Num());

	for (const FKeyHandle& Handle : InKeys)
	{
		if (IsKeyProtected(Handle) == false)
		{
			FilteredHandles.Add(Handle);
		}
	}

	FRichCurveEditorModel::RemoveKeys(FilteredHandles);

	if (FFocalLengthFocusPoint* Point = LensFile->FocalLengthTable.GetFocusPoint(Focus))
	{
		for(const FKeyHandle& Handle : FilteredHandles)
		{
			const FRichCurveKey& TouchedKey = CurrentCurve.GetKey(Handle);
			const FKeyHandle FxHandle = Point->Fx.FindKey(TouchedKey.Time);
			Point->Fx.DeleteKey(FxHandle);
			const FKeyHandle FyHandle = Point->Fy.FindKey(TouchedKey.Time);
			Point->Fy.DeleteKey(FxHandle);
		}
	}
}

void FLensFocalLengthCurveModel::SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions, EPropertyChangeType::Type ChangeType)
{
	FRichCurveEditorModel::SetKeyPositions(InKeys, InKeyPositions, ChangeType);

	//When modifying the focal length curve, update the underlying fxfy
	if (ParameterIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			const FKeyHandle Handle = InKeys[Index];
			const int32 KeyIndex = CurrentCurve.GetIndexSafe(Handle);
			if (KeyIndex != INDEX_NONE)
			{
				if (FFocalLengthFocusPoint* Point = LensFile->FocalLengthTable.GetFocusPoint(Focus))
				{
					//Convert focal length new value to fx fy
					float AspectRatio = LensFile->LensInfo.SensorDimensions.Y / LensFile->LensInfo.SensorDimensions.X;
					const float CurrentFx = Point->Fx.GetKeyValue(Handle);
					const FKeyHandle FyHandle = Point->Fy.FindKey(Point->Fx.GetKeyTime(Handle));
					const float CurrentFy = Point->Fy.GetKeyValue(FyHandle);
					if(FMath::IsNearlyZero(CurrentFx) == false)
					{
						AspectRatio = CurrentFy / CurrentFx;
					}

					const float NewFx = CurrentCurve.GetKeyValue(Handle) / LensFile->LensInfo.SensorDimensions.X;
					Point->Fx.SetKeyValue(Handle, NewFx);
					Point->Fy.SetKeyValue(FyHandle, NewFx * AspectRatio);
				}
			}
		}
	}
	else
	{
		if (FFocalLengthFocusPoint* Point = LensFile->FocalLengthTable.GetFocusPoint(Focus))
		{
			FRichCurve* ActiveCurve = nullptr;
			if(ParameterIndex == 0)
			{
				ActiveCurve = &Point->Fx;
			}
			else
			{
				ActiveCurve = &Point->Fy;
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
}

bool FLensFocalLengthCurveModel::IsKeyProtected(FKeyHandle InHandle) const
{
	if (FFocalLengthFocusPoint* Points = LensFile->FocalLengthTable.GetFocusPoint(Focus))
	{
		const int32 FxIndex = Points->Fx.GetIndexSafe(InHandle);
		if (ensure(Points->ZoomPoints.IsValidIndex(FxIndex)))
		{
			return Points->ZoomPoints[FxIndex].bIsCalibrationPoint;
		}
	}

	return false;
}

