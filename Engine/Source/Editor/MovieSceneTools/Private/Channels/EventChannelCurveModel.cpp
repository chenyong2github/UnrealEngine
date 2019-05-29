// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EventChannelCurveModel.h"
#include "Math/Vector2D.h"
#include "HAL/PlatformMath.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/FloatChannelKeyProxy.h"
#include "MovieSceneSection.h"
#include "MovieScene.h"
#include "CurveDrawInfo.h"
#include "CurveDataAbstraction.h"
#include "CurveEditor.h"
#include "CurveEditorScreenSpace.h"
#include "CurveEditorSnapMetrics.h"
#include "EditorStyleSet.h"
#include "BuiltInChannelEditors.h"
#include "SequencerChannelTraits.h"
#include "SequencerSectionPainter.h"
#include "ISequencer.h"

ECurveEditorViewID FEventChannelCurveModel::EventView = ECurveEditorViewID::Invalid;

FEventChannelCurveModel::FEventChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneEventChannel> InChannel, UMovieSceneSection* OwningSection, TWeakPtr<ISequencer> InWeakSequencer)
{
	ChannelHandle = InChannel;
	WeakSection = OwningSection;
	WeakSequencer = InWeakSequencer;
	SupportedViews = EventView;

	Color = FSequencerSectionPainter::BlendColor(OwningSection->GetTypedOuter<UMovieSceneTrack>()->GetColorTint());
}

const void* FEventChannelCurveModel::GetCurve() const
{
	return ChannelHandle.Get();
}

void FEventChannelCurveModel::Modify()
{
	if (UMovieSceneSection* Section = WeakSection.Get())
	{
		Section->Modify();
	}
}

void FEventChannelCurveModel::AddKeys(TArrayView<const FKeyPosition> InKeyPositions, TArrayView<const FKeyAttributes> InKeyAttributes, TArrayView<TOptional<FKeyHandle>>* OutKeyHandles)
{
	check(InKeyPositions.Num() == InKeyAttributes.Num() && (!OutKeyHandles || OutKeyHandles->Num() == InKeyPositions.Num()));

	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();
	if (Channel && Section)
	{
		Section->Modify();
		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		for (int32 Index = 0; Index < InKeyPositions.Num(); ++Index)
		{
			FKeyPosition   Position   = InKeyPositions[Index];
			FKeyAttributes Attributes = InKeyAttributes[Index];

			FFrameNumber Time = (Position.InputValue * TickResolution).RoundToFrame();
			Section->ExpandToFrame(Time);

			FMovieSceneEvent Value;
// 
// 			if (Attributes.HasInterpMode())    { Value.InterpMode            = Attributes.GetInterpMode();    }
// 			if (Attributes.HasTangentMode())   { Value.TangentMode           = Attributes.GetTangentMode();   }
// 			if (Attributes.HasArriveTangent()) { Value.Tangent.ArriveTangent = Attributes.GetArriveTangent(); }
// 			if (Attributes.HasLeaveTangent())  { Value.Tangent.LeaveTangent  = Attributes.GetLeaveTangent();  }

			int32 KeyIndex = ChannelData.AddKey(Time, Value);
			if (OutKeyHandles)
			{
				(*OutKeyHandles)[Index] = ChannelData.GetHandle(KeyIndex);
			}
		}

		// Channel->AutoSetTangents();
	}
}

bool FEventChannelCurveModel::Evaluate(double Time, double& OutValue) const
{
	// Events don't evaluate into a valid value
	OutValue = 0.0;
	return false;
}

void FEventChannelCurveModel::RemoveKeys(TArrayView<const FKeyHandle> InKeys)
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();
	if (Channel && Section)
	{
		Section->Modify();

		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();

		for (FKeyHandle Handle : InKeys)
		{
			int32 KeyIndex = ChannelData.GetIndex(Handle);
			if (KeyIndex != INDEX_NONE)
			{
				ChannelData.RemoveKey(KeyIndex);
			}
		}
	}
}

void FEventChannelCurveModel::DrawCurve(const FCurveEditor& CurveEditor, const FCurveEditorScreenSpace& ScreenSpace, TArray<TTuple<double, double>>& InterpolatingPoints) const
{
	// Event Channels don't draw any lines so there's no need to fill out the Interpolating Points array.
}

void FEventChannelCurveModel::GetKeys(const FCurveEditor& CurveEditor, double MinTime, double MaxTime, double MinValue, double MaxValue, TArray<FKeyHandle>& OutKeyHandles) const
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();

	if (Channel && Section)
	{
		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
		TArrayView<const FFrameNumber>          Times  = ChannelData.GetTimes();

		const FFrameNumber StartFrame = MinTime <= MIN_int32 ? MIN_int32 : (MinTime * TickResolution).CeilToFrame();
		const FFrameNumber EndFrame   = MaxTime >= MAX_int32 ? MAX_int32 : (MaxTime * TickResolution).FloorToFrame();

		const int32 StartingIndex = Algo::LowerBound(Times, StartFrame);
		const int32 EndingIndex   = Algo::UpperBound(Times, EndFrame);

		for (int32 KeyIndex = StartingIndex; KeyIndex < EndingIndex; ++KeyIndex)
		{
			// Event Channels don't have Values associated with them so we ignore the Min/Max value and always return the key.
			OutKeyHandles.Add(ChannelData.GetHandle(KeyIndex));
		}
	}
}

void FEventChannelCurveModel::GetKeyDrawInfo(ECurvePointType PointType, const FKeyHandle InKeyHandle, FKeyDrawInfo& OutDrawInfo) const
{
	OutDrawInfo.Brush = FEditorStyle::Get().GetBrush("Sequencer.KeyDiamond");
	OutDrawInfo.ScreenSize = FVector2D(10, 10);
}

void FEventChannelCurveModel::GetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyPosition> OutKeyPositions) const
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();

	if (Channel && Section)
	{
		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
		TArrayView<const FFrameNumber>          Times  = ChannelData.GetTimes();
		TArrayView<const FMovieSceneEvent> Values = ChannelData.GetValues();

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
			if (KeyIndex != INDEX_NONE)
			{
				OutKeyPositions[Index].InputValue  = Times[KeyIndex] / TickResolution;
				// Events have no values so we just output zero.
				OutKeyPositions[Index].OutputValue = 0.0;
			}
		}
	}
}

void FEventChannelCurveModel::SetKeyPositions(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyPosition> InKeyPositions)
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();

	if (Channel && Section)
	{
		Section->MarkAsChanged();

		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
			if (KeyIndex != INDEX_NONE)
			{
				FFrameNumber NewTime = (InKeyPositions[Index].InputValue * TickResolution).FloorToFrame();

				KeyIndex = ChannelData.MoveKey(KeyIndex, NewTime);
				Section->ExpandToFrame(NewTime);
			}
		}
	}
}

void FEventChannelCurveModel::GetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyAttributes> OutAttributes) const
{
// 	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
// 	UMovieSceneSection*      Section = WeakSection.Get();
// 	if (Channel && Section)
// 	{
// 		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
// 		TArrayView<const FFrameNumber>    Times  = ChannelData.GetTimes();
// 		TArrayView<FMovieSceneEvent> Values = ChannelData.GetValues();
// 
// 		float TimeInterval = Section->GetTypedOuter<UMovieScene>()->GetTickResolution().AsInterval();
// 
// 		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
// 		{
// 			const int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
// 			if (KeyIndex != INDEX_NONE)
// 			{
// 				const FMovieSceneEvent& KeyValue    = Values[KeyIndex];
// 				FKeyAttributes&              Attributes  = OutAttributes[Index];
// 
// 				Attributes.SetInterpMode(KeyValue.InterpMode);
// 
// 				if (KeyValue.InterpMode != RCIM_Constant && KeyValue.InterpMode != RCIM_Linear)
// 				{
// 					Attributes.SetTangentMode(KeyValue.TangentMode);
// 					if (KeyIndex != 0)
// 					{
// 						Attributes.SetArriveTangent(KeyValue.Tangent.ArriveTangent / TimeInterval);
// 					}
// 
// 					if (KeyIndex != Times.Num()-1)
// 					{
// 						Attributes.SetLeaveTangent(KeyValue.Tangent.LeaveTangent / TimeInterval);
// 					}
// 					if (KeyValue.InterpMode == RCIM_Cubic)
// 					{
// 						Attributes.SetTangentWeightMode(KeyValue.Tangent.TangentWeightMode);
// 						if (KeyValue.Tangent.TangentWeightMode != RCTWM_WeightedNone)
// 						{
// 							Attributes.SetArriveTangentWeight(KeyValue.Tangent.ArriveTangentWeight);
// 							Attributes.SetLeaveTangentWeight(KeyValue.Tangent.LeaveTangentWeight);
// 						}
// 					}
// 				}
// 			}
// 		}
// 	}
}

void FEventChannelCurveModel::SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes)
{
// 	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
// 	UMovieSceneSection*      Section = WeakSection.Get();
// 	if (Channel && Section)
// 	{
// 		bool bAutoSetTangents = false;
// 		Section->MarkAsChanged();
// 
// 		TMovieSceneChannelData<FMovieSceneEvent> ChannelData = Channel->GetData();
// 		TArrayView<FMovieSceneEvent> Values = ChannelData.GetValues();
// 
// 		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();
// 		float TimeInterval = TickResolution.AsInterval();
// 
// 		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
// 		{
// 			const int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
// 			if (KeyIndex != INDEX_NONE)
// 			{
// 				const FKeyAttributes&  Attributes = InAttributes[Index];
// 				FMovieSceneEvent& KeyValue   = Values[KeyIndex];
// 				if (Attributes.HasInterpMode())    { KeyValue.InterpMode  = Attributes.GetInterpMode();  bAutoSetTangents = true; }
// 				if (Attributes.HasTangentMode())
// 				{
// 					KeyValue.TangentMode = Attributes.GetTangentMode();
// 					if (KeyValue.TangentMode == RCTM_Auto)
// 					{
// 						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
// 					}
// 					bAutoSetTangents = true;
// 				}
// 				if (Attributes.HasTangentWeightMode()) 
// 				{ 
// 					if (KeyValue.Tangent.TangentWeightMode == RCTWM_WeightedNone) //set tangent weights to default use
// 					{
// 						TArrayView<const FFrameNumber> Times = Channel->GetTimes();
// 						const float OneThird = 1.0f / 3.0f;
// 
// 						//calculate a tangent weight based upon tangent and time difference
// 						//calculate arrive tangent weight
// 						if (KeyIndex > 0 )
// 						{
// 							const float X = TickResolution.AsSeconds(Times[KeyIndex].Value - Times[KeyIndex - 1].Value);
// 							const float ArriveTangentNormal = KeyValue.Tangent.ArriveTangent / (TimeInterval);
// 							const float Y = ArriveTangentNormal * X;
// 							KeyValue.Tangent.ArriveTangentWeight = FMath::Sqrt(X*X + Y * Y) * OneThird;
// 						}
// 						//calculate leave weight
// 						if(KeyIndex < ( Times.Num() - 1))
// 						{
// 							const float X = TickResolution.AsSeconds(Times[KeyIndex].Value - Times[KeyIndex + 1].Value);
// 							const float LeaveTangentNormal = KeyValue.Tangent.LeaveTangent / (TimeInterval);
// 							const float Y = LeaveTangentNormal * X;
// 							KeyValue.Tangent.LeaveTangentWeight = FMath::Sqrt(X*X + Y*Y) * OneThird;
// 						}
// 					}
// 					KeyValue.Tangent.TangentWeightMode = Attributes.GetTangentWeightMode();
// 
// 					if( KeyValue.Tangent.TangentWeightMode != RCTWM_WeightedNone )
// 					{
// 						if (KeyValue.TangentMode != RCTM_User && KeyValue.TangentMode != RCTM_Break)
// 						{
// 							KeyValue.TangentMode = RCTM_User;
// 						}
// 					}
// 				}
// 
// 				if (Attributes.HasArriveTangent())
// 				{
// 					if (KeyValue.TangentMode == RCTM_Auto)
// 					{
// 						KeyValue.TangentMode = RCTM_User;
// 						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
// 					}
// 
// 					KeyValue.Tangent.ArriveTangent = Attributes.GetArriveTangent() * TimeInterval;
// 					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
// 					{
// 						KeyValue.Tangent.LeaveTangent = KeyValue.Tangent.ArriveTangent;
// 					}
// 				}
// 
// 				if (Attributes.HasLeaveTangent())
// 				{
// 					if (KeyValue.TangentMode == RCTM_Auto)
// 					{
// 						KeyValue.TangentMode = RCTM_User;
// 						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
// 					}
// 
// 					KeyValue.Tangent.LeaveTangent = Attributes.GetLeaveTangent() * TimeInterval;
// 					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
// 					{
// 						KeyValue.Tangent.ArriveTangent = KeyValue.Tangent.LeaveTangent;
// 					}
// 				}
// 
// 				if (Attributes.HasArriveTangentWeight())
// 				{
// 					if (KeyValue.TangentMode == RCTM_Auto)
// 					{
// 						KeyValue.TangentMode = RCTM_User;
// 						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
// 					}
// 
// 					KeyValue.Tangent.ArriveTangentWeight = Attributes.GetArriveTangentWeight(); 
// 					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
// 					{
// 						KeyValue.Tangent.LeaveTangentWeight = KeyValue.Tangent.ArriveTangentWeight;
// 					}
// 				}
// 
// 				if (Attributes.HasLeaveTangentWeight())
// 				{
// 				
// 					if (KeyValue.TangentMode == RCTM_Auto)
// 					{
// 						KeyValue.TangentMode = RCTM_User;
// 						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
// 					}
// 
// 					KeyValue.Tangent.LeaveTangentWeight = Attributes.GetLeaveTangentWeight();
// 					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
// 					{
// 						KeyValue.Tangent.ArriveTangentWeight = KeyValue.Tangent.LeaveTangentWeight;
// 					}
// 				}
// 			}
// 		}
// 
// 		if (bAutoSetTangents)
// 		{
// 			Channel->AutoSetTangents();
// 		}
// 	}
}

void FEventChannelCurveModel::GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const
{
	// Event Channels have no Pre/Post Extrapolation
	OutCurveAttributes.SetPreExtrapolation(RCCE_None);
	OutCurveAttributes.SetPostExtrapolation(RCCE_None);
}

void FEventChannelCurveModel::SetCurveAttributes(const FCurveAttributes& InCurveAttributes)
{
	// Event Channels have no Pre/Post Extrapolation
}

void FEventChannelCurveModel::CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects)
{
	// for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	// {
	// 	UFloatChannelKeyProxy* NewProxy = NewObject<UFloatChannelKeyProxy>(GetTransientPackage(), NAME_None);
	// 
	// 	NewProxy->Initialize(InKeyHandles[Index], ChannelHandle, WeakSection);
	// 	OutObjects[Index] = NewProxy;
	// }
}

void FEventChannelCurveModel::GetTimeRange(double& MinTime, double& MaxTime) const
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();
	UMovieSceneSection*      Section = WeakSection.Get();

	if (Channel && Section)
	{
		TArrayView<const FFrameNumber> Times = Channel->GetData().GetTimes();
		if (Times.Num() == 0)
		{
			MinTime = 0.f;
			MaxTime = 0.f;
		}
		else
		{
			FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();
			double ToTime = TickResolution.AsInterval();
			MinTime = static_cast<double> (Times[0].Value) * ToTime;
			MaxTime = static_cast<double>(Times[Times.Num() - 1].Value) * ToTime;
		}
	}
}

/*	 Finds min/max for cubic curves:
Looks for feature points in the signal(determined by change in direction of local tangent), these locations are then re-examined in closer detail recursively
Similar to function in RichCurve but usees the Channel::Evaluate function, instead of CurveModel::Eval*/

void FEventChannelCurveModel::FeaturePointMethod(double StartTime, double EndTime, double StartValue, double Mu, int Depth, int MaxDepth, double& MaxV, double& MinVal) const
{
	if (Depth >= MaxDepth)
	{
		return;
	}
	double PrevValue = StartValue;
	double EvalValue;
	Evaluate(StartTime - Mu, EvalValue);
	double PrevTangent = StartValue - EvalValue;
	EndTime += Mu;
	for (double f = StartTime + Mu; f < EndTime; f += Mu)
	{
		double Value;
		Evaluate(f, Value);

		MaxV = FMath::Max(Value, MaxV);
		MinVal = FMath::Min(Value, MinVal);
		double CurTangent = Value - PrevValue;
		if (FMath::Sign(CurTangent) != FMath::Sign(PrevTangent))
		{
			//feature point centered around the previous tangent
			double FeaturePointTime = f - Mu * 2.0f;
			double NewVal;
			Evaluate(FeaturePointTime, NewVal);
			FeaturePointMethod(FeaturePointTime, f,NewVal, Mu*0.4f, Depth + 1, MaxDepth, MaxV, MinVal);
		}
		PrevTangent = CurTangent;
		PrevValue = Value;
	}
}

void FEventChannelCurveModel::GetValueRange(double& MinValue, double& MaxValue) const
{
	// Event Tracks have no value, thus their value range is just zero.
	MinValue = MaxValue = 0.0;
}

int32 FEventChannelCurveModel::GetNumKeys() const
{
	FMovieSceneEventChannel* Channel = ChannelHandle.Get();

	if (Channel)
	{
		TArrayView<const FFrameNumber> Times = Channel->GetData().GetTimes();
		return Times.Num();
	}

	return 0;
}
