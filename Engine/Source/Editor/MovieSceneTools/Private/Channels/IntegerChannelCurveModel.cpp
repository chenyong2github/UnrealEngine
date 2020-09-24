// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/IntegerChannelCurveModel.h"
#include "Math/Vector2D.h"
#include "HAL/PlatformMath.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/IntegerChannelKeyProxy.h"
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
 * Buffered curve implementation for a integer channel curve model, stores a copy of the integer channel in order to draw itself.
 */
class FIntegerChannelBufferedCurveModel : public IBufferedCurveModel
{
public:
	/** Create a copy of the float channel while keeping the reference to the section */
	FIntegerChannelBufferedCurveModel(const FMovieSceneIntegerChannel* InMovieSceneIntegerChannel, TWeakObjectPtr<UMovieSceneSection> InWeakSection,
		TArray<FKeyPosition>&& InKeyPositions, TArray<FKeyAttributes>&& InKeyAttributes, const FString& InIntentionName, const double InValueMin, const double InValueMax)
		: IBufferedCurveModel(MoveTemp(InKeyPositions), MoveTemp(InKeyAttributes), InIntentionName, InValueMin, InValueMax)
		, Channel(*InMovieSceneIntegerChannel)
		, WeakSection(InWeakSection)
	{}

	virtual void DrawCurve(const FCurveEditor& InCurveEditor, const FCurveEditorScreenSpace& InScreenSpace, TArray<TTuple<double, double>>& OutInterpolatingPoints) const override
	{
		UMovieSceneSection* Section = WeakSection.Get();

		if (Section)
		{
			FFrameRate TickResolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

			TMovieSceneChannelData<const int32> ChannelData = Channel.GetData();
			TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
			TArrayView<const int32> Values = ChannelData.GetValues();

			const double StartTimeSeconds = InScreenSpace.GetInputMin();
			const double EndTimeSeconds = InScreenSpace.GetInputMax();

			const FFrameNumber StartFrame = (StartTimeSeconds * TickResolution).FloorToFrame();
			const FFrameNumber EndFrame = (EndTimeSeconds * TickResolution).CeilToFrame();

			const int32 StartingIndex = Algo::UpperBound(Times, StartFrame);
			const int32 EndingIndex = Algo::LowerBound(Times, EndFrame);

			for (int32 KeyIndex = StartingIndex; KeyIndex < EndingIndex; ++KeyIndex)
			{
				OutInterpolatingPoints.Add(MakeTuple(Times[KeyIndex] / TickResolution, double(Values[KeyIndex])));
			}
		}
	}

private:
	FMovieSceneIntegerChannel Channel;
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
};

FIntegerChannelCurveModel::FIntegerChannelCurveModel(TMovieSceneChannelHandle<FMovieSceneIntegerChannel> InChannel, UMovieSceneSection* OwningSection, TWeakPtr<ISequencer> InWeakSequencer)
	: FChannelCurveModel<FMovieSceneIntegerChannel, int32, int32>(InChannel, OwningSection, InWeakSequencer)
{
}

void FIntegerChannelCurveModel::CreateKeyProxies(TArrayView<const FKeyHandle> InKeyHandles, TArrayView<UObject*> OutObjects)
{
	for (int32 Index = 0; Index < InKeyHandles.Num(); ++Index)
	{
		UIntegerChannelKeyProxy* NewProxy = NewObject<UIntegerChannelKeyProxy>(GetTransientPackage(), NAME_None);

		NewProxy->Initialize(InKeyHandles[Index], GetChannelHandle(), Cast<UMovieSceneSection>(GetOwningObject()));
		OutObjects[Index] = NewProxy;
	}
}

TUniquePtr<IBufferedCurveModel> FIntegerChannelCurveModel::CreateBufferedCurveCopy() const
{
	FMovieSceneIntegerChannel* Channel = GetChannelHandle().Get();
	if (Channel)
	{
		TArray<FKeyHandle> TargetKeyHandles;
		TMovieSceneChannelData<int32> ChannelData = Channel->GetData();

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

		return MakeUnique<FIntegerChannelBufferedCurveModel>(Channel, Cast<UMovieSceneSection>(GetOwningObject()), MoveTemp(KeyPositions), MoveTemp(KeyAttributes), GetIntentionName(), ValueMin, ValueMax);
	}
	return nullptr;
}

double FIntegerChannelCurveModel::GetKeyValue(TArrayView<const int32> Values, int32 Index) const
{
	return (double)Values[Index];
}

void FIntegerChannelCurveModel::SetKeyValue(int32 Index, double KeyValue) const
{
	FMovieSceneIntegerChannel* Channel = GetChannelHandle().Get();

	if (Channel)
	{
		TMovieSceneChannelData<int32> ChannelData = Channel->GetData();
		ChannelData.GetValues()[Index] = (int32)KeyValue;
	}
}
