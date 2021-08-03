// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/FloatChannelCurveModel.h"
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
#include "ISequencer.h"
#include "Channels/MovieSceneChannelProxy.h"

/**
 * Buffered curve implementation for a float channel curve model, stores a copy of the float channel in order to draw itself.
 */
class FFloatChannelBufferedCurveModel : public IBufferedCurveModel
{
public:
	/** Create a copy of the float channel while keeping the reference to the section */
	FFloatChannelBufferedCurveModel(const FMovieSceneFloatChannel* InMovieSceneFloatChannel, TWeakObjectPtr<UMovieSceneSection> InWeakSection,
		TArray<FKeyPosition>&& InKeyPositions, TArray<FKeyAttributes>&& InKeyAttributes, const FString& InIntentionName, const double InValueMin, const double InValueMax)
		: IBufferedCurveModel(MoveTemp(InKeyPositions), MoveTemp(InKeyAttributes), InIntentionName, InValueMin, InValueMax)
		, Channel(*InMovieSceneFloatChannel)
		, WeakSection(InWeakSection)
	{}

	virtual void DrawCurve(const FCurveEditor& InCurveEditor, const FCurveEditorScreenSpace& InScreenSpace, TArray<TTuple<double, double>>& OutInterpolatingPoints) const override
	{
		UMovieSceneSection* Section = WeakSection.Get();

		if (Section && Section->GetTypedOuter<UMovieScene>())
		{
			FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

			const double StartTimeSeconds = InScreenSpace.GetInputMin();
			const double EndTimeSeconds = InScreenSpace.GetInputMax();
			const double TimeThreshold = FMath::Max(0.0001, 1.0 / InScreenSpace.PixelsPerInput());
			const double ValueThreshold = FMath::Max(0.0001, 1.0 / InScreenSpace.PixelsPerOutput());

			Channel.PopulateCurvePoints(StartTimeSeconds, EndTimeSeconds, TimeThreshold, ValueThreshold, TickResolution, OutInterpolatingPoints);
		}
	}

private:
	FMovieSceneFloatChannel Channel;
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
};

FFloatChannelCurveModel::FFloatChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneFloatChannel> InChannel, UMovieSceneSection* OwningSection, TWeakPtr<ISequencer> InWeakSequencer)
	: FChannelCurveModel<FMovieSceneFloatChannel, FMovieSceneFloatValue, float>(InChannel, OwningSection, InWeakSequencer)
{
	FMovieSceneFloatChannel* Channel = InChannel.Get();

	if (Channel && OwningSection && OwningSection->GetTypedOuter<UMovieScene>())
	{
		Channel->SetTickResolution(OwningSection->GetTypedOuter<UMovieScene>()->GetTickResolution());
	}
}

void FFloatChannelCurveModel::DrawCurve(const FCurveEditor& CurveEditor, const FCurveEditorScreenSpace& ScreenSpace, TArray<TTuple<double, double>>& InterpolatingPoints) const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	UMovieSceneSection*      Section = Cast<UMovieSceneSection>(GetOwningObject());

	if (Channel && Section && Section->GetTypedOuter<UMovieScene>())
	{
		FFrameRate   TickResolution   = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		const double DisplayOffset    = GetInputDisplayOffset();
		const double StartTimeSeconds = ScreenSpace.GetInputMin() - DisplayOffset;
		const double EndTimeSeconds   = ScreenSpace.GetInputMax() - DisplayOffset;
		const double TimeThreshold    = FMath::Max(0.0001, 1.0 / ScreenSpace.PixelsPerInput());
		const double ValueThreshold   = FMath::Max(0.0001, 1.0 / ScreenSpace.PixelsPerOutput());

		Channel->PopulateCurvePoints(StartTimeSeconds, EndTimeSeconds, TimeThreshold, ValueThreshold, TickResolution, InterpolatingPoints);
	}
}

void FFloatChannelCurveModel::GetKeyDrawInfo(ECurvePointType PointType, const FKeyHandle InKeyHandle, FKeyDrawInfo& OutDrawInfo) const
{
	if (PointType == ECurvePointType::ArriveTangent || PointType == ECurvePointType::LeaveTangent)
	{
		OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.TangentHandle");
		OutDrawInfo.ScreenSize = FVector2D(8, 8);
	}
	else
	{
		// All keys are the same size by default
		OutDrawInfo.ScreenSize = FVector2D(11, 11);

		ERichCurveInterpMode KeyType = RCIM_None;
		ERichCurveTangentWeightMode KeyTWType = RCTWM_WeightedNone;

		// Get the key type from the supplied key handle if it's valid
		FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
		if (Channel && InKeyHandle != FKeyHandle::Invalid())
		{
			TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();
			const int32 KeyIndex = ChannelData.GetIndex(InKeyHandle);
			if (KeyIndex != INDEX_NONE)
			{
				KeyType = ChannelData.GetValues()[KeyIndex].InterpMode;
				KeyTWType = ChannelData.GetValues()[KeyIndex].Tangent.TangentWeightMode;
			}
		}

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
			if (KeyTWType == ERichCurveTangentWeightMode::RCTWM_WeightedBoth)
			{
				OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.WeightedTangentCubicKey");
			}
			else
			{
				OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.CubicKey");
			}

			OutDrawInfo.Tint = FLinearColor::White;
			break;
		default:
			OutDrawInfo.Brush = FEditorStyle::GetBrush("GenericCurveEditor.Key");
			OutDrawInfo.Tint = FLinearColor::White;
			break;
		}

		if (IsReadOnly())
		{
			OutDrawInfo.Tint = OutDrawInfo.Tint * 0.5f;
		}
	}
}

void FFloatChannelCurveModel::GetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<FKeyAttributes> OutAttributes) const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	UMovieSceneSection*      Section = Cast<UMovieSceneSection>(GetOwningObject());
	if (Channel && Section && Section->GetTypedOuter<UMovieScene>())
	{
		TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();
		TArrayView<const FFrameNumber>    Times  = ChannelData.GetTimes();
		TArrayView<FMovieSceneFloatValue> Values = ChannelData.GetValues();

		float TimeInterval = Section->GetTypedOuter<UMovieScene>()->GetTickResolution().AsInterval();

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			const int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
			if (KeyIndex != INDEX_NONE)
			{
				const FMovieSceneFloatValue& KeyValue    = Values[KeyIndex];
				FKeyAttributes&              Attributes  = OutAttributes[Index];

				Attributes.SetInterpMode(KeyValue.InterpMode);

				if (KeyValue.InterpMode != RCIM_Constant && KeyValue.InterpMode != RCIM_Linear)
				{
					Attributes.SetTangentMode(KeyValue.TangentMode);
					if (KeyIndex != 0)
					{
						Attributes.SetArriveTangent(KeyValue.Tangent.ArriveTangent / TimeInterval);
					}

					if (KeyIndex != Times.Num()-1)
					{
						Attributes.SetLeaveTangent(KeyValue.Tangent.LeaveTangent / TimeInterval);
					}
					if (KeyValue.InterpMode == RCIM_Cubic)
					{
						Attributes.SetTangentWeightMode(KeyValue.Tangent.TangentWeightMode);
						if (KeyValue.Tangent.TangentWeightMode != RCTWM_WeightedNone)
						{
							Attributes.SetArriveTangentWeight(KeyValue.Tangent.ArriveTangentWeight);
							Attributes.SetLeaveTangentWeight(KeyValue.Tangent.LeaveTangentWeight);
						}
					}
				}
			}
		}
	}
}

void FFloatChannelCurveModel::SetKeyAttributes(TArrayView<const FKeyHandle> InKeys, TArrayView<const FKeyAttributes> InAttributes, EPropertyChangeType::Type ChangeType)
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	UMovieSceneSection*      Section = Cast<UMovieSceneSection>(GetOwningObject());
	if (Channel && Section && Section->GetTypedOuter<UMovieScene>() && !IsReadOnly())
	{
		bool bAutoSetTangents = false;
		Section->MarkAsChanged();

		TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();
		TArrayView<FMovieSceneFloatValue> Values = ChannelData.GetValues();

		FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();
		float TimeInterval = TickResolution.AsInterval();

		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			const int32 KeyIndex = ChannelData.GetIndex(InKeys[Index]);
			if (KeyIndex != INDEX_NONE)
			{
				const FKeyAttributes&  Attributes = InAttributes[Index];
				FMovieSceneFloatValue& KeyValue   = Values[KeyIndex];
				if (Attributes.HasInterpMode())    { KeyValue.InterpMode  = Attributes.GetInterpMode();  bAutoSetTangents = true; }
				if (Attributes.HasTangentMode())
				{
					KeyValue.TangentMode = Attributes.GetTangentMode();
					if (KeyValue.TangentMode == RCTM_Auto)
					{
						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
					}
					bAutoSetTangents = true;
				}
				if (Attributes.HasTangentWeightMode()) 
				{ 
					if (KeyValue.Tangent.TangentWeightMode == RCTWM_WeightedNone) //set tangent weights to default use
					{
						TArrayView<const FFrameNumber> Times = Channel->GetTimes();
						const float OneThird = 1.0f / 3.0f;

						//calculate a tangent weight based upon tangent and time difference
						//calculate arrive tangent weight
						if (KeyIndex > 0 )
						{
							const float X = TickResolution.AsSeconds(Times[KeyIndex].Value - Times[KeyIndex - 1].Value);
							const float ArriveTangentNormal = KeyValue.Tangent.ArriveTangent / (TimeInterval);
							const float Y = ArriveTangentNormal * X;
							KeyValue.Tangent.ArriveTangentWeight = FMath::Sqrt(X*X + Y * Y) * OneThird;
						}
						//calculate leave weight
						if(KeyIndex < ( Times.Num() - 1))
						{
							const float X = TickResolution.AsSeconds(Times[KeyIndex].Value - Times[KeyIndex + 1].Value);
							const float LeaveTangentNormal = KeyValue.Tangent.LeaveTangent / (TimeInterval);
							const float Y = LeaveTangentNormal * X;
							KeyValue.Tangent.LeaveTangentWeight = FMath::Sqrt(X*X + Y*Y) * OneThird;
						}
					}
					KeyValue.Tangent.TangentWeightMode = Attributes.GetTangentWeightMode();

					if( KeyValue.Tangent.TangentWeightMode != RCTWM_WeightedNone )
					{
						if (KeyValue.TangentMode != RCTM_User && KeyValue.TangentMode != RCTM_Break)
						{
							KeyValue.TangentMode = RCTM_User;
						}
					}
				}

				if (Attributes.HasArriveTangent())
				{
					if (KeyValue.TangentMode == RCTM_Auto)
					{
						KeyValue.TangentMode = RCTM_User;
						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
					}

					KeyValue.Tangent.ArriveTangent = Attributes.GetArriveTangent() * TimeInterval;
					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
					{
						KeyValue.Tangent.LeaveTangent = KeyValue.Tangent.ArriveTangent;
					}
				}

				if (Attributes.HasLeaveTangent())
				{
					if (KeyValue.TangentMode == RCTM_Auto)
					{
						KeyValue.TangentMode = RCTM_User;
						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
					}

					KeyValue.Tangent.LeaveTangent = Attributes.GetLeaveTangent() * TimeInterval;
					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
					{
						KeyValue.Tangent.ArriveTangent = KeyValue.Tangent.LeaveTangent;
					}
				}

				if (Attributes.HasArriveTangentWeight())
				{
					if (KeyValue.TangentMode == RCTM_Auto)
					{
						KeyValue.TangentMode = RCTM_User;
						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
					}

					KeyValue.Tangent.ArriveTangentWeight = Attributes.GetArriveTangentWeight(); 
					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
					{
						KeyValue.Tangent.LeaveTangentWeight = KeyValue.Tangent.ArriveTangentWeight;
					}
				}

				if (Attributes.HasLeaveTangentWeight())
				{
				
					if (KeyValue.TangentMode == RCTM_Auto)
					{
						KeyValue.TangentMode = RCTM_User;
						KeyValue.Tangent.TangentWeightMode = RCTWM_WeightedNone;
					}

					KeyValue.Tangent.LeaveTangentWeight = Attributes.GetLeaveTangentWeight();
					if (KeyValue.InterpMode == RCIM_Cubic && KeyValue.TangentMode != RCTM_Break)
					{
						KeyValue.Tangent.ArriveTangentWeight = KeyValue.Tangent.LeaveTangentWeight;
					}
				}
			}
		}

		if (bAutoSetTangents)
		{
			Channel->AutoSetTangents();
		}

		CurveModifiedDelegate.Broadcast();
	}
}

void FFloatChannelCurveModel::GetCurveAttributes(FCurveAttributes& OutCurveAttributes) const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	if (Channel)
	{
		OutCurveAttributes.SetPreExtrapolation(Channel->PreInfinityExtrap);
		OutCurveAttributes.SetPostExtrapolation(Channel->PostInfinityExtrap);
	}
}

void FFloatChannelCurveModel::SetCurveAttributes(const FCurveAttributes& InCurveAttributes)
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	UMovieSceneSection*      Section = Cast<UMovieSceneSection>(GetOwningObject());
	if (Channel && Section && !IsReadOnly())
	{
		Section->MarkAsChanged();

		if (InCurveAttributes.HasPreExtrapolation())
		{
			Channel->PreInfinityExtrap = InCurveAttributes.GetPreExtrapolation();
		}

		if (InCurveAttributes.HasPostExtrapolation())
		{
			Channel->PostInfinityExtrap = InCurveAttributes.GetPostExtrapolation();
		}

		CurveModifiedDelegate.Broadcast();
	}
}

void FFloatChannelCurveModel::CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects)
{
	for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	{
		UFloatChannelKeyProxy* NewProxy = NewObject<UFloatChannelKeyProxy>(GetTransientPackage(), NAME_None);

		NewProxy->Initialize(InKeyHandles[Index], GetChannelHandle(), Cast<UMovieSceneSection>(GetOwningObject()));
		OutObjects[Index] = NewProxy;
	}
}

TUniquePtr<IBufferedCurveModel> FFloatChannelCurveModel::CreateBufferedCurveCopy() const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	if (Channel)
	{
		TArray<FKeyHandle> TargetKeyHandles;
		TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();

		TRange<FFrameNumber> TotalRange = ChannelData.GetTotalRange();
		ChannelData.GetKeys(TotalRange, nullptr, &TargetKeyHandles);

		TArray<FKeyPosition> KeyPositions;
		KeyPositions.SetNumUninitialized(GetNumKeys());
		TArray<FKeyAttributes> KeyAttributes;
		KeyAttributes.SetNumUninitialized(GetNumKeys());
		GetKeyPositions(TargetKeyHandles, KeyPositions);
		GetKeyAttributes(TargetKeyHandles, KeyAttributes);

		double ValueMin = 0.f, ValueMax = 1.f;
		GetValueRange(ValueMin, ValueMax);

		return MakeUnique<FFloatChannelBufferedCurveModel>(Channel, Cast<UMovieSceneSection>(GetOwningObject()), MoveTemp(KeyPositions), MoveTemp(KeyAttributes), GetIntentionName(), ValueMin, ValueMax);
	}
	return nullptr;
}

/*	 Finds min/max for cubic curves:
Looks for feature points in the signal(determined by change in direction of local tangent), these locations are then re-examined in closer detail recursively
Similar to function in RichCurve but usees the Channel::Evaluate function, instead of CurveModel::Eval*/

void FFloatChannelCurveModel::FeaturePointMethod(double StartTime, double EndTime, double StartValue, double Mu, int Depth, int MaxDepth, double& MaxV, double& MinVal) const
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

void FFloatChannelCurveModel::GetValueRange(double& MinValue, double& MaxValue) const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();
	UMovieSceneSection*      Section = Cast<UMovieSceneSection>(GetOwningObject());

	if (Channel && Section && Section->GetTypedOuter<UMovieScene>())
	{
		TArrayView<const FFrameNumber> Times = Channel->GetData().GetTimes();
		TArrayView<const FMovieSceneFloatValue>   Values = Channel->GetData().GetValues();

		if (Times.Num() == 0)
		{
			// If there are no keys we just use the default value for the channel, defaulting to zero if there is no default.
			MinValue = MaxValue = Channel->GetDefault().Get(0.f);
		}
		else
		{
			FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();
			double ToTime = TickResolution.AsInterval();
			int32 LastKeyIndex = Values.Num() - 1;
			MinValue = MaxValue = Values[0].Value;

			for (int32 i = 0; i < Values.Num(); i++)
			{
				const FMovieSceneFloatValue& Key = Values[i];

				MinValue = FMath::Min(MinValue,(double) Key.Value);
				MaxValue = FMath::Max(MaxValue, (double)Key.Value);

				if (Key.InterpMode == RCIM_Cubic && i != LastKeyIndex)
				{
					const FMovieSceneFloatValue& NextKey = Values[i + 1];
					double KeyTime = static_cast<double>(Times[i].Value) *ToTime;
					double NextTime = static_cast<double>(Times[i +1].Value) *ToTime;
					double TimeStep = (NextTime - KeyTime) * 0.2f;
					FeaturePointMethod(KeyTime, NextTime, Key.Value, TimeStep, 0, 3, MaxValue, MinValue);
				}
			}
		}
	}
}

double FFloatChannelCurveModel::GetKeyValue(TArrayView<const FMovieSceneFloatValue> Values, int32 Index) const
{
	return Values[Index].Value;
}

void FFloatChannelCurveModel::SetKeyValue(int32 Index, double KeyValue) const
{
	FMovieSceneFloatChannel* Channel = GetChannelHandle().Get();

	if (Channel)
	{
		TMovieSceneChannelData<FMovieSceneFloatValue> ChannelData = Channel->GetData();
		ChannelData.GetValues()[Index].Value = KeyValue;
	}
}
