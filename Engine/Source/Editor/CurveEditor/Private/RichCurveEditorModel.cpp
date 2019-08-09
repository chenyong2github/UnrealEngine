// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RichCurveEditorModel.h"
#include "RichCurveKeyProxy.h"
#include "Math/Vector2D.h"
#include "HAL/PlatformMath.h"
#include "Curves/RichCurve.h"
#include "CurveDrawInfo.h"
#include "CurveDataAbstraction.h"
#include "CurveEditor.h"
#include "CurveEditorScreenSpace.h"
#include "CurveEditorSnapMetrics.h"
#include "EditorStyleSet.h"
#include "UObject/Package.h"

FRichCurveEditorModel::FRichCurveEditorModel(FRichCurve* InRichCurve, UObject* InOwner)
	: RichCurve(InRichCurve), WeakOwner(InOwner)
{
	checkf(RichCurve, TEXT("If is not valid to provide a null rich curve to this class"));
}

const void* FRichCurveEditorModel::GetCurve() const
{
	return RichCurve;
}

void FRichCurveEditorModel::Modify()
{
	if (UObject* Owner = WeakOwner.Get())
	{
		Owner->SetFlags(RF_Transactional);
		Owner->Modify();
	}
}

void FRichCurveEditorModel::AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InKeyAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles)
{
	check(InKeyPositions.Num() == InKeyAttributes.Num() && (!OutKeyHandles || OutKeyHandles->Num() == InKeyPositions.Num()));

	UObject* Owner = WeakOwner.Get();
	if (Owner)
	{
		Owner->Modify();

		TArray<FKeyHandle> NewKeyHandles;
		NewKeyHandles.SetNumUninitialized(InKeyPositions.Num());

		for (int32 Index = 0; Index < InKeyPositions.Num(); ++Index)
		{
			FKeyPosition   Position   = InKeyPositions[Index];
			FKeyAttributes Attributes = InKeyAttributes[Index];

			FKeyHandle     NewHandle = RichCurve->AddKey(Position.InputValue, Position.OutputValue);
			FRichCurveKey* NewKey    = &RichCurve->GetKey(NewHandle);

			NewKeyHandles[Index] = NewHandle;
			if (OutKeyHandles)
			{
				(*OutKeyHandles)[Index] = NewHandle;
			}
		}

		// We reuse SetKeyAttributes here as there is complex logic determining which parts of the attributes are valid to pass on.
		// For now we need to duplicate the new key handle array due to API mismatch. This will auto-calculate tangents if required.
		SetKeyAttributes(NewKeyHandles, InKeyAttributes);
	}
}

bool FRichCurveEditorModel::Evaluate(double Time, double& OutValue) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		OutValue = RichCurve->Eval(Time);
		return true;
	}

	return false;
}

void FRichCurveEditorModel::RemoveKeys(TArrayView<const FKeyHandle> InKeys)
{
	if (UObject* Owner = WeakOwner.Get())
	{
		Owner->Modify();
		for (FKeyHandle Handle : InKeys)
		{
			RichCurve->DeleteKey(Handle);
		}
	}
}

void RefineCurvePoints(const FRichCurve* RichCurve, double TimeThreshold, float ValueThreshold, TArray<TTuple<double, double>>& InOutPoints)
{
	const float InterpTimes[] = { 0.25f, 0.5f, 0.6f };

	for (int32 Index = 0; Index < InOutPoints.Num() - 1; ++Index)
	{
		TTuple<double, double> Lower = InOutPoints[Index];
		TTuple<double, double> Upper = InOutPoints[Index + 1];

		if ((Upper.Get<0>() - Lower.Get<0>()) >= TimeThreshold)
		{
			bool bSegmentIsLinear = true;

			TTuple<double, double> Evaluated[UE_ARRAY_COUNT(InterpTimes)];

			for (int32 InterpIndex = 0; InterpIndex < UE_ARRAY_COUNT(InterpTimes); ++InterpIndex)
			{
				double& EvalTime  = Evaluated[InterpIndex].Get<0>();

				EvalTime = FMath::Lerp(Lower.Get<0>(), Upper.Get<0>(), InterpTimes[InterpIndex]);

				float Value = RichCurve->Eval(EvalTime);

				const float LinearValue = FMath::Lerp(Lower.Get<1>(), Upper.Get<1>(), InterpTimes[InterpIndex]);
				if (bSegmentIsLinear)
				{
					bSegmentIsLinear = FMath::IsNearlyEqual(Value, LinearValue, ValueThreshold);
				}

				Evaluated[InterpIndex].Get<1>() = Value;
			}

			if (!bSegmentIsLinear)
			{
				// Add the point
				InOutPoints.Insert(Evaluated, UE_ARRAY_COUNT(Evaluated), Index+1);
				--Index;
			}
		}
	}
}

void FRichCurveEditorModel::DrawCurve(const FCurveEditor& CurveEditor, const FCurveEditorScreenSpace& ScreenSpace, TArray<TTuple<double, double>>& InOutPoints) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		const double StartTimeSeconds = ScreenSpace.GetInputMin();
		const double EndTimeSeconds   = ScreenSpace.GetInputMax();
		const double TimeThreshold    = FMath::Max(0.0001, 1.0 / ScreenSpace.PixelsPerInput());
		const double ValueThreshold   = FMath::Max(0.0001, 1.0 / ScreenSpace.PixelsPerOutput());

		InOutPoints.Add(MakeTuple(StartTimeSeconds, double(RichCurve->Eval(StartTimeSeconds))));

		for (const FRichCurveKey& Key : RichCurve->GetConstRefOfKeys())
		{
			if (Key.Time > StartTimeSeconds && Key.Time < EndTimeSeconds)
			{
				InOutPoints.Add(MakeTuple(double(Key.Time), double(Key.Value)));
			}
		}

		InOutPoints.Add(MakeTuple(EndTimeSeconds, double(RichCurve->Eval(EndTimeSeconds))));

		int32 OldSize = InOutPoints.Num();
		do
		{
			OldSize = InOutPoints.Num();
			RefineCurvePoints(RichCurve, TimeThreshold, ValueThreshold, InOutPoints);
		}
		while(OldSize != InOutPoints.Num());
	}
}

void FRichCurveEditorModel::GetKeys(const FCurveEditor& CurveEditor, double MinTime, double MaxTime, double MinValue, double MaxValue, TArray<FKeyHandle>& OutKeyHandles) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		for (auto It = RichCurve->GetKeyHandleIterator(); It; ++It)
		{
			const FRichCurveKey& Key = RichCurve->GetKey(*It);
			if (Key.Time >= MinTime && Key.Time <= MaxTime && Key.Value >= MinValue && Key.Value <= MaxValue)
			{
				OutKeyHandles.Add(*It);
			}
		}
	}
}

void FRichCurveEditorModel::GetKeyDrawInfo(ECurvePointType PointType, const FKeyHandle InKeyHandle, FKeyDrawInfo& OutDrawInfo) const
{
	if (PointType == ECurvePointType::ArriveTangent || PointType == ECurvePointType::LeaveTangent)
	{
		OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.TangentHandle");
		OutDrawInfo.ScreenSize = FVector2D(9, 9);
	}
	else
	{
		// All keys are the same size by default
		OutDrawInfo.ScreenSize = FVector2D(11, 11);

		ERichCurveInterpMode KeyType = RichCurve->IsKeyHandleValid(InKeyHandle) ? RichCurve->GetKey(InKeyHandle).InterpMode.GetValue() : RCIM_None;
		switch (KeyType)
		{
		case ERichCurveInterpMode::RCIM_Constant:
			OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.ConstantKey");
			OutDrawInfo.Tint = FLinearColor(0, 0.45f, 0.70f);
			break;
		case ERichCurveInterpMode::RCIM_Linear:
			OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.LinearKey");
			OutDrawInfo.Tint = FLinearColor(0, 0.62f, 0.46f);
			break;
		case ERichCurveInterpMode::RCIM_Cubic:
			OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.CubicKey");
			OutDrawInfo.Tint = FLinearColor::White;
			break;
		default:
			OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.Key");
			OutDrawInfo.Tint = FLinearColor::White;
			break;
		}
	}
}

void FRichCurveEditorModel::GetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyPosition> OutKeyPositions) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			if (RichCurve->IsKeyHandleValid(InKeys[Index]))
			{
				const FRichCurveKey& Key = RichCurve->GetKey(InKeys[Index]);

				OutKeyPositions[Index].InputValue  = Key.Time;
				OutKeyPositions[Index].OutputValue = Key.Value;
			}
		}
	}
}

void FRichCurveEditorModel::SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions)
{
	if (UObject* Owner = WeakOwner.Get())
	{
		Owner->Modify();

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			FKeyHandle Handle = InKeys[Index];
			if (RichCurve->IsKeyHandleValid(Handle))
			{
				// Set key time last so we don't have to worry about the key handle changing
				RichCurve->GetKey(Handle).Value = InKeyPositions[Index].OutputValue;
				RichCurve->SetKeyTime(Handle, InKeyPositions[Index].InputValue);
			}
		}
		RichCurve->AutoSetTangents();
	}
}

void FRichCurveEditorModel::GetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyAttributes> OutAttributes) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		const TArray<FRichCurveKey>& AllKeys = RichCurve->GetConstRefOfKeys();
		if (AllKeys.Num() == 0)
		{
			return;
		}

		const FRichCurveKey* FirstKey = &AllKeys[0];
		const FRichCurveKey* LastKey  = &AllKeys.Last();

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			if (RichCurve->IsKeyHandleValid(InKeys[Index]))
			{
				const FRichCurveKey* ThisKey    = &RichCurve->GetKey(InKeys[Index]);
				FKeyAttributes&      Attributes = OutAttributes[Index];

				Attributes.SetInterpMode(ThisKey->InterpMode);

				if (ThisKey->InterpMode != RCIM_Constant && ThisKey->InterpMode != RCIM_Linear)
				{
					Attributes.SetTangentMode(ThisKey->TangentMode);
					if (ThisKey != FirstKey)
					{
						Attributes.SetArriveTangent(ThisKey->ArriveTangent);
					}

					if (ThisKey != LastKey)
					{
						Attributes.SetLeaveTangent(ThisKey->LeaveTangent);
					}
					if (ThisKey->InterpMode == RCIM_Cubic)
					{
						Attributes.SetTangentWeightMode(ThisKey->TangentWeightMode);
						if (ThisKey->TangentWeightMode != RCTWM_WeightedNone)
						{
							Attributes.SetArriveTangentWeight(ThisKey->ArriveTangentWeight);
							Attributes.SetLeaveTangentWeight(ThisKey->LeaveTangentWeight);
						}
					}
				}
			}
		}
	}
}

void FRichCurveEditorModel::SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes)
{
	if (UObject* Owner = WeakOwner.Get())
	{
		const TArray<FRichCurveKey>& AllKeys = RichCurve->GetConstRefOfKeys();
		if (AllKeys.Num() == 0)
		{
			return;
		}

		Owner->Modify();

		const FRichCurveKey* FirstKey = &AllKeys[0];
		const FRichCurveKey* LastKey  = &AllKeys.Last();

		bool bAutoSetTangents = false;

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			if (RichCurve->IsKeyHandleValid(InKeys[Index]))
			{
				FRichCurveKey*        ThisKey    = &RichCurve->GetKey(InKeys[Index]);
				const FKeyAttributes& Attributes = InAttributes[Index];

				if (Attributes.HasInterpMode())    { ThisKey->InterpMode  = Attributes.GetInterpMode();  bAutoSetTangents = true; }
				if (Attributes.HasTangentMode())
				{
					ThisKey->TangentMode = Attributes.GetTangentMode();
					if (ThisKey->TangentMode == RCTM_Auto)
					{
						ThisKey->TangentWeightMode = RCTWM_WeightedNone;
					}
					bAutoSetTangents = true;
				}
				if (Attributes.HasTangentWeightMode()) 
				{ 
					if (ThisKey->TangentWeightMode == RCTWM_WeightedNone) //set tangent weights to default use
					{
						const float OneThird = 1.0f / 3.0f;

						//calculate a tangent weight based upon tangent and time difference
						//calculate arrive tangent weight
						if (ThisKey != FirstKey)
						{
							const float Y = ThisKey->ArriveTangent;
							ThisKey->ArriveTangentWeight = FMath::Sqrt(1.f + Y*Y) * OneThird;
						}
						//calculate leave weight
						if(ThisKey != LastKey)
						{
							const float Y = ThisKey->LeaveTangent;
							ThisKey->LeaveTangentWeight = FMath::Sqrt(1.f + Y*Y) * OneThird;
						}
					}
					ThisKey->TangentWeightMode = Attributes.GetTangentWeightMode();

					if( ThisKey->TangentWeightMode != RCTWM_WeightedNone )
					{
						if (ThisKey->TangentMode != RCTM_User && ThisKey->TangentMode != RCTM_Break)
						{
							ThisKey->TangentMode = RCTM_User;
						}
					}
				}

				if (Attributes.HasArriveTangent())
				{
					if (ThisKey->TangentMode == RCTM_Auto)
					{
						ThisKey->TangentMode = RCTM_User;
						ThisKey->TangentWeightMode = RCTWM_WeightedNone;
					}

					ThisKey->ArriveTangent = Attributes.GetArriveTangent();
					if (ThisKey->InterpMode == RCIM_Cubic && ThisKey->TangentMode != RCTM_Break)
					{
						ThisKey->LeaveTangent = ThisKey->ArriveTangent;
					}
				}

				if (Attributes.HasLeaveTangent())
				{
					if (ThisKey->TangentMode == RCTM_Auto)
					{
						ThisKey->TangentMode = RCTM_User;
						ThisKey->TangentWeightMode = RCTWM_WeightedNone;
					}

					ThisKey->LeaveTangent = Attributes.GetLeaveTangent();
					if (ThisKey->InterpMode == RCIM_Cubic && ThisKey->TangentMode != RCTM_Break)
					{
						ThisKey->ArriveTangent = ThisKey->LeaveTangent;
					}
				}

				if (Attributes.HasArriveTangentWeight())
				{
					if (ThisKey->TangentMode == RCTM_Auto)
					{
						ThisKey->TangentMode = RCTM_User;
						ThisKey->TangentWeightMode = RCTWM_WeightedNone;
					}

					ThisKey->ArriveTangentWeight = Attributes.GetArriveTangentWeight();
					if (ThisKey->InterpMode == RCIM_Cubic && ThisKey->TangentMode != RCTM_Break)
					{
						ThisKey->LeaveTangentWeight = ThisKey->ArriveTangentWeight;
					}
				}

				if (Attributes.HasLeaveTangentWeight())
				{
				
					if (ThisKey->TangentMode == RCTM_Auto)
					{
						ThisKey->TangentMode = RCTM_User;
						ThisKey->TangentWeightMode = RCTWM_WeightedNone;
					}

					ThisKey->LeaveTangentWeight = Attributes.GetLeaveTangentWeight();
					if (ThisKey->InterpMode == RCIM_Cubic && ThisKey->TangentMode != RCTM_Break)
					{
						ThisKey->ArriveTangentWeight = ThisKey->LeaveTangentWeight;
					}
				}
			}
		}

		if (bAutoSetTangents)
		{
			RichCurve->AutoSetTangents();
		}
	}
}

void FRichCurveEditorModel::GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		OutCurveAttributes.SetPreExtrapolation(RichCurve->PreInfinityExtrap);
		OutCurveAttributes.SetPostExtrapolation(RichCurve->PostInfinityExtrap);
	}
}

void FRichCurveEditorModel::SetCurveAttributes(const FCurveAttributes& InCurveAttributes)
{
	if (UObject* Owner = WeakOwner.Get())
	{
		Owner->Modify();

		if (InCurveAttributes.HasPreExtrapolation())
		{
			RichCurve->PreInfinityExtrap = InCurveAttributes.GetPreExtrapolation();
		}

		if (InCurveAttributes.HasPostExtrapolation())
		{
			RichCurve->PostInfinityExtrap = InCurveAttributes.GetPostExtrapolation();
		}
	}
}

void FRichCurveEditorModel::CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects)
{
	for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	{
		URichCurveKeyProxy* NewProxy = NewObject<URichCurveKeyProxy>(GetTransientPackage(), NAME_None);

		NewProxy->Initialize(InKeyHandles[Index], RichCurve, WeakOwner);
		OutObjects[Index] = NewProxy;
	}
}

void FRichCurveEditorModel::GetTimeRange(double& MinTime, double& MaxTime) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		float MinTimeFloat = 0.f, MaxTimeFloat = 0.f;
		RichCurve->GetTimeRange(MinTimeFloat, MaxTimeFloat);

		MinTime = MinTimeFloat;
		MaxTime = MaxTimeFloat;
	}
}

void FRichCurveEditorModel::GetValueRange(double& MinValue, double& MaxValue) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		float MinValueFloat = 0.f, MaxValueFloat = 0.f;
		RichCurve->GetValueRange(MinValueFloat, MaxValueFloat);

		MinValue = MinValueFloat;
		MaxValue = MaxValueFloat;
	}
}

int32 FRichCurveEditorModel::GetNumKeys() const
{
	return RichCurve->GetNumKeys();
}

void FRichCurveEditorModel::GetNeighboringKeys(const FKeyHandle InKeyHandle, TOptional<FKeyHandle>& OutPreviousKeyHandle, TOptional<FKeyHandle>& OutNextKeyHandle) const
{
	if (UObject* Owner = WeakOwner.Get())
	{
		if (RichCurve->IsKeyHandleValid(InKeyHandle))
		{
			FKeyHandle NextKeyHandle = RichCurve->GetNextKey(InKeyHandle);

			if (RichCurve->IsKeyHandleValid(NextKeyHandle))
			{
				OutNextKeyHandle = NextKeyHandle;
			}

			FKeyHandle PreviousKeyHandle = RichCurve->GetPreviousKey(InKeyHandle);

			if (RichCurve->IsKeyHandleValid(PreviousKeyHandle))
			{
				OutPreviousKeyHandle = PreviousKeyHandle;
			}
		}
	}
}