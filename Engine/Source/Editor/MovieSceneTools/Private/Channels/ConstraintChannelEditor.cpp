// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannelEditor.h"
#include "Channels/ConstraintChannelCurveModel.h"
#include "ConstraintChannel.h"

#include "KeyBarCurveModel.h"
#include "SequencerSectionPainter.h"
#include "TimeToPixel.h"
#include "Fonts/FontMeasure.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	using IndexRange = ::TRange<int32>;
	IndexRange GetRange(const TArrayView<const bool>& Values, const int32 Offset) 
	{
		IndexRange Range;
			
		int32 FirstActive = INDEX_NONE;
		for (int32 Index = Offset; Index < Values.Num(); ++Index)
		{
			if (Values[Index])
			{
				FirstActive = Index;
				break;
			}
		}

		if (FirstActive == INDEX_NONE)
		{
			return Range;
		}

		Range.SetLowerBound(TRangeBound<int32>(FirstActive));
		Range.SetUpperBound(TRangeBound<int32>(FirstActive));

		for (int32 NextInactive = FirstActive+1; NextInactive < Values.Num(); ++NextInactive)
		{
			if (Values[NextInactive] == false)
			{
				Range.SetUpperBound(TRangeBound<int32>(NextInactive));
				return Range;
			}
		}
			
		return Range;
	};

	TArray<IndexRange> GetIndexRanges(const FMovieSceneConstraintChannel* Channel)
	{
		TArray<IndexRange> Ranges;

		const TMovieSceneChannelData<const bool> ChannelData = Channel->GetData();
		const TArrayView<const bool> Values = ChannelData.GetValues();
		if (Values.Num() == 1)
		{
			if (Values[0] == true)
			{
				Ranges.Emplace(0);
			}
			return Ranges;
		}
		
		int32 Offset = 0;
		while (Values.IsValidIndex(Offset))
		{
			TRange<int32> Range = GetRange(Values, Offset);
			if (!Range.IsEmpty())
			{
				Ranges.Emplace(Range);
				Offset = Range.GetUpperBoundValue()+1;
			}
			else
			{
				Offset = INDEX_NONE;
			}
		}
			
		return Ranges;
	}
}

void DrawKeys(
	FMovieSceneConstraintChannel* Channel,
	TArrayView<const FKeyHandle> InKeyHandles,
	const UMovieSceneSection* InOwner,
	TArrayView<FKeyDrawParams> OutKeyDrawParams)
{
	if (!InOwner)
	{
		return;
	}
	
	static const FName SquareKeyBrushName("Sequencer.KeySquare");
	static const FName FilledBorderBrushName("FilledBorder");

	static FKeyDrawParams Params;
	Params.FillBrush = FAppStyle::GetBrush(FilledBorderBrushName);
	Params.BorderBrush = FAppStyle::GetBrush(SquareKeyBrushName);
	
	for (FKeyDrawParams& Param : OutKeyDrawParams)
	{
		Param = Params;
	}
}

namespace
{

struct FConstraintChannelLabelBuilder
{
public:
	FConstraintChannelLabelBuilder(const UMovieSceneSection* InOwner, const FMovieSceneConstraintChannel* InChannel)
		: Owner(InOwner)
		, Channel(InChannel)
	{
		ConstructLabel();
	}

	FText Label;
	bool bValid = false;
	
private:
	void ConstructLabel() 
	{
		if (!Channel || !Owner)
		{
			return;
		}

		// get the stored extra label
		const FString ExtraLabel = Channel->ExtraLabel ? Channel->ExtraLabel() : FString();
		if (!ExtraLabel.IsEmpty())
		{
			Label = FText::FromString(ExtraLabel);
			bValid = true;
			return;
		}

		// fallback to meta data
		const FMovieSceneChannelProxy& Proxy = Owner->GetChannelProxy();
		const TArrayView<FMovieSceneConstraintChannel*> ConstraintChannels = Proxy.GetChannels<FMovieSceneConstraintChannel>();
		const TArrayView<const FMovieSceneChannelMetaData> MetaData = Proxy.GetMetaData<FMovieSceneConstraintChannel>();
		
		const int32 ChannelIndex = ConstraintChannels.IndexOfByKey(Channel);
		if (!ConstraintChannels.IsValidIndex(ChannelIndex) || !MetaData.IsValidIndex(ChannelIndex))
		{
			return;
		}

		const FName& ConstraintName = MetaData[ChannelIndex].Name;
		if (ConstraintName != NAME_None)
		{
			Label = FText::FromName(ConstraintName);
			bValid = true;
		}
	}

	const UMovieSceneSection* Owner = nullptr;
	const FMovieSceneConstraintChannel* Channel = nullptr;
};

}

TArray<FKeyBarCurveModel::FBarRange> FConstraintChannelEditor::GetBarRanges(
	FMovieSceneConstraintChannel* Channel,
	const UMovieSceneSection* Owner)
{
	TArray<FKeyBarCurveModel::FBarRange> Ranges;

	TArray<IndexRange> IndexRanges = GetIndexRanges(Channel);
	if (IndexRanges.IsEmpty())
	{
		return Ranges;
	}

	// convert to bar range
	const TMovieSceneChannelData<const bool> ChannelData = Channel->GetData();
	const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
	const FConstraintChannelLabelBuilder LabelBuilder(Owner, Channel);
	const FText& Label = LabelBuilder.Label;
	for (const IndexRange& ActiveRange : IndexRanges)
	{
		FKeyBarCurveModel::FBarRange BarRange;
		FFrameRate TickResolution = Owner->GetTypedOuter<UMovieScene>()->GetTickResolution();
		double LowerValue = Times[ActiveRange.GetLowerBoundValue()] / TickResolution;
		double UpperValue = Times[ActiveRange.GetUpperBoundValue()] / TickResolution;

		BarRange.Range.SetLowerBound(TRangeBound<double>(LowerValue));
		BarRange.Range.SetUpperBound(TRangeBound<double>(UpperValue));

		BarRange.Name = FName(*Label.ToString());
		BarRange.Color = FLinearColor(.2, .5, .1);
		static FLinearColor ZebraTint = FLinearColor::White.CopyWithNewOpacity(0.01f);
		BarRange.Color = BarRange.Color * (1.f - ZebraTint.A) + ZebraTint * ZebraTint.A;
		BarRange.bRangeIsInfinite = false;
		Ranges.Add(BarRange);
	}

	return Ranges;
}

void DrawExtra(
	FMovieSceneConstraintChannel* Channel,
	const UMovieSceneSection* Owner,
	const FGeometry& AllottedGeometry,
	FSequencerSectionPainter& Painter)
{
	if (!Owner || !Channel)
	{
		return;
	}
	TArray<FKeyBarCurveModel::FBarRange> Ranges = FConstraintChannelEditor::GetBarRanges(Channel,Owner);

	if (Ranges.IsEmpty())
	{
		return;
	}
	
	// draw bars + labels
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	static constexpr ESlateDrawEffect DrawEffects = ESlateDrawEffect::None;

	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont");
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D& LocalSize = AllottedGeometry.GetLocalSize();
	static constexpr float LaneTop = 0;

	const FTimeToPixel& TimeToPixel = Painter.GetTimeConverter();
	const double InputMin = TimeToPixel.PixelToSeconds(0.f);
	const double InputMax = TimeToPixel.PixelToSeconds(Painter.SectionGeometry.GetLocalSize().X);

	// label data
	const FConstraintChannelLabelBuilder LabelBuilder(Owner, Channel);
	const FText& Label = LabelBuilder.Label;
	const FVector2D TextSize = LabelBuilder.bValid ? FontMeasure->Measure(Label, FontInfo) : FVector2D::ZeroVector;
	static constexpr double LabelPixelOffset = 10.0;  
	
	for (int32 Index = 0; Index < Ranges.Num(); ++Index)
	{
		const FKeyBarCurveModel::FBarRange& Range = Ranges[Index];

		// draw bar
		double LowerSeconds = Range.Range.GetLowerBoundValue();
		double UpperSeconds = Range.Range.GetUpperBoundValue();
		if (UpperSeconds == Range.Range.GetLowerBoundValue())
		{
			UpperSeconds = InputMax;
		}

		const double BoxStart = TimeToPixel.SecondsToPixel(LowerSeconds);
		const double BoxEnd = TimeToPixel.SecondsToPixel(UpperSeconds);
		const double BoxSize = BoxEnd - BoxStart;

		const FVector2D Size = FVector2D(BoxSize, LocalSize.Y);
		const FVector2D Translation = FVector2D(BoxStart, LaneTop);
		const FPaintGeometry BoxGeometry = AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Translation));
		
		FSlateDrawElement::MakeBox(Painter.DrawElements, Painter.LayerId, BoxGeometry, WhiteBrush, DrawEffects, Range.Color);

		// draw label
		if (LabelBuilder.bValid)
		{
			const double LabelPos = BoxStart + LabelPixelOffset;
			const FVector2D Position(LabelPos, LaneTop + (LocalSize.Y - TextSize.Y) * .5f);
			const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(Position));

			const double LabelMaxSize = BoxSize-LabelPixelOffset;
			if (TextSize.X < LabelMaxSize)
			{
				FSlateDrawElement::MakeText(Painter.DrawElements, Painter.LayerId, LabelGeometry, Label, FontInfo);				
			}
			else
			{
				// crop
				const int32 End = FontMeasure->FindLastWholeCharacterIndexBeforeOffset(Label, FontInfo, FMath::RoundToInt(LabelMaxSize));
				if (End >= 0)
				{
					FSlateDrawElement::MakeText(Painter.DrawElements, Painter.LayerId, LabelGeometry, Label.ToString(), 0, End, FontInfo);
				}
			}
		}
	}
}

FKeyHandle AddOrUpdateKey(
	FMovieSceneConstraintChannel* Channel,
	UMovieSceneSection* SectionToKey,
	FFrameNumber Time,
	ISequencer& Sequencer,
	const FGuid& ObjectBindingID,
	FTrackInstancePropertyBindings* PropertyBindings)
{
	return FKeyHandle::Invalid();
}

bool CanCreateKeyEditor(const FMovieSceneConstraintChannel* InChannel)
{
	return false;
}

TSharedRef<SWidget> CreateKeyEditor(
	const TMovieSceneChannelHandle<FMovieSceneConstraintChannel>& InChannel,
	UMovieSceneSection* InSection,
	const FGuid& InObjectBindingID,
	TWeakPtr<FTrackInstancePropertyBindings> PropertyBindings,
	TWeakPtr<ISequencer> InSequencer)
{
	return SNullWidget::NullWidget;
}

TUniquePtr<FCurveModel> CreateCurveEditorModel(const TMovieSceneChannelHandle<FMovieSceneConstraintChannel>& Channel, UMovieSceneSection* OwningSection, TSharedRef<ISequencer> InSequencer)
{
	return MakeUnique<FConstraintChannelCurveModel>(Channel, OwningSection, InSequencer);
}