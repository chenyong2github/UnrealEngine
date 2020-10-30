// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerKeyRenderer.h"
#include "EditorStyleSet.h"
#include "CommonMovieSceneTools.h"
#include "MovieSceneTimeHelpers.h"
#include "SequencerSectionPainter.h"
#include "Sequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"

namespace UE
{
namespace Sequencer
{

FKeyRenderer::FPaintStyle::FPaintStyle(const FWidgetStyle& InWidgetStyle)
{
	static const FName SelectionColorName("SelectionColor");
	static const FName HighlightBrushName("Sequencer.AnimationOutliner.DefaultBorder");
	static const FName StripeOverlayBrushName("Sequencer.Section.StripeOverlay");
	static const FName SelectedTrackTintBrushName("Sequencer.Section.SelectedTrackTint");
	static const FName BackgroundTrackTintBrushName("Sequencer.Section.BackgroundTint");

	SelectionColor = FEditorStyle::GetSlateColor(SelectionColorName).GetColor(InWidgetStyle);

	BackgroundTrackTintBrush = FEditorStyle::GetBrush(BackgroundTrackTintBrushName);
	SelectedTrackTintBrush = FEditorStyle::GetBrush(SelectedTrackTintBrushName);
	StripeOverlayBrush = FEditorStyle::GetBrush(StripeOverlayBrushName);
	HighlightBrush = FEditorStyle::GetBrush(HighlightBrushName);
}

FKeyRenderer::FCachedState::FCachedState(const FSequencerSectionPainter& InPainter, FSequencer* Sequencer)
{
	const FTimeToPixel& TimeToPixelConverter = InPainter.GetTimeConverter();

	UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

	// Gather keys for a region larger than the view range to ensure we draw keys that are only just offscreen.
	// Compute visible range taking into account a half-frame offset for keys, plus half a key width for keys that are partially offscreen
	TRange<FFrameNumber> SectionRange   = InPainter.Section.GetRange();
	const double         HalfKeyWidth   = 0.5f * (TimeToPixelConverter.PixelToSeconds(SequencerSectionConstants::KeySize.X) - TimeToPixelConverter.PixelToSeconds(0));
	TRange<double>       VisibleRange   = UE::MovieScene::DilateRange(Sequencer->GetViewRange(), -HalfKeyWidth, HalfKeyWidth);
	TRange<FFrameNumber> ValidKeyRange  = Sequencer->GetSubSequenceRange().Get(MovieScene->GetPlaybackRange());

	ValidPlayRangeMin = UE::MovieScene::DiscreteInclusiveLower(ValidKeyRange);
	ValidPlayRangeMax = UE::MovieScene::DiscreteExclusiveUpper(ValidKeyRange);
	PaddedViewRange = TRange<double>::Intersection(SectionRange / MovieScene->GetTickResolution(), VisibleRange);
	SelectionSerial = Sequencer->GetSelection().GetSerialNumber();
	SelectionPreviewHash = Sequencer->GetSelectionPreview().GetSelectionHash();
}

FKeyRenderer::ECacheFlags FKeyRenderer::FCachedState::CompareTo(const FCachedState& Other) const
{
	ECacheFlags Flags = ECacheFlags::None;

	if (ValidPlayRangeMin != Other.ValidPlayRangeMin || ValidPlayRangeMax != Other.ValidPlayRangeMax)
	{
		// The valid key ranges for the data has changed
		Flags |= ECacheFlags::KeyStateChanged;
	}

	if (SelectionSerial != Other.SelectionSerial || SelectionPreviewHash != Other.SelectionPreviewHash)
	{
		// Selection states have changed
		Flags |= ECacheFlags::KeyStateChanged;
	}

	if (PaddedViewRange != Other.PaddedViewRange)
	{
		Flags |= ECacheFlags::ViewChanged;

		const double RangeSize = PaddedViewRange.Size<double>();
		const double OtherRangeSize = Other.PaddedViewRange.Size<double>();

		if (!FMath::IsNearlyEqual(RangeSize, OtherRangeSize, RangeSize * 0.001))
		{
			Flags |= ECacheFlags::ViewZoomed;
		}
	}

	return Flags;
}

FKeyRenderer::FKeyDrawBatch::FKeyDrawBatch(const FSectionLayoutElement& LayoutElement)
{
	for (TSharedRef<IKeyArea> KeyArea : LayoutElement.GetKeyAreas())
	{
		KeyDrawInfo.Emplace(KeyArea);
	}
}

FKeyRenderer::ECacheFlags FKeyRenderer::FCachedKeyDrawInformation::UpdateViewIndependentData(FFrameRate TickResolution)
{
	const bool bDataChanged = CachedKeyPositions.Update(TickResolution);

	return bDataChanged ? ECacheFlags::DataChanged : ECacheFlags::None;
}

void FKeyRenderer::FCachedKeyDrawInformation::CacheViewDependentData(const TRange<double>& VisibleRange, ECacheFlags CacheFlags)
{
	if (EnumHasAnyFlags(CacheFlags, FKeyRenderer::ECacheFlags::DataChanged | FKeyRenderer::ECacheFlags::ViewChanged | FKeyRenderer::ECacheFlags::ViewZoomed))
	{
		TArrayView<const FFrameNumber> OldFramesInRange = FramesInRange;

		// Gather all the key handles in this view range
		CachedKeyPositions.GetKeysInRange(VisibleRange, &TimesInRange, &FramesInRange, &HandlesInRange);

		bool bDrawnKeys = false;
		if (!EnumHasAnyFlags(CacheFlags, FKeyRenderer::ECacheFlags::DataChanged) && OldFramesInRange.Num() != 0 && FramesInRange.Num() != 0)
		{
			// Try and preserve draw params if possible
			const int32 PreserveStart = Algo::LowerBound(OldFramesInRange, FramesInRange[0]);
			const int32 PreserveEnd   = Algo::UpperBound(OldFramesInRange, FramesInRange.Last());

			const int32 PreserveNum   = PreserveEnd - PreserveStart;
			if (PreserveNum > 0)
			{
				TArray<FKeyDrawParams> NewDrawParams;

				const int32 HeadNum = Algo::LowerBound(FramesInRange, OldFramesInRange[PreserveStart]);
				if (HeadNum > 0)
				{
					NewDrawParams.SetNum(HeadNum);
					CachedKeyPositions.GetKeyArea()->DrawKeys(HandlesInRange.Slice(0, HeadNum), NewDrawParams);
				}

				NewDrawParams.Append(DrawParams.GetData() + PreserveStart, PreserveNum);

				const int32 TailStart = Algo::LowerBound(FramesInRange, OldFramesInRange[PreserveEnd-1]);
				const int32 TailNum = FramesInRange.Num() - TailStart;

				if (TailNum > 0)
				{
					NewDrawParams.SetNum(FramesInRange.Num());
					CachedKeyPositions.GetKeyArea()->DrawKeys(HandlesInRange.Slice(TailStart, TailNum), TArrayView<FKeyDrawParams>(NewDrawParams).Slice(TailStart, TailNum));
				}

				DrawParams = MoveTemp(NewDrawParams);
				bDrawnKeys = true;
			}
		}

		if (!bDrawnKeys)
		{
			DrawParams.SetNum(TimesInRange.Num());

			if (TimesInRange.Num())
			{
				// Draw these keys
				CachedKeyPositions.GetKeyArea()->DrawKeys(HandlesInRange, DrawParams);
			}
		}

		check(DrawParams.Num() == TimesInRange.Num() && TimesInRange.Num() == HandlesInRange.Num());
	}

	// Always reset the pointers to the current key that needs processing
	PreserveToIndex = TimesInRange.Num();
	NextUnhandledIndex = 0;
}

FKeyRenderer::ECacheFlags FKeyRenderer::FKeyDrawBatch::UpdateViewIndependentData(FFrameRate TickResolution)
{
	FKeyRenderer::ECacheFlags CacheState = FKeyRenderer::ECacheFlags::None;

	for (FCachedKeyDrawInformation& CachedKeyDrawInfo : KeyDrawInfo)
	{
		CacheState |= CachedKeyDrawInfo.UpdateViewIndependentData(TickResolution);
	}

	return CacheState;
}

void FKeyRenderer::FKeyDrawBatch::UpdateViewDependentData(FSequencer* Sequencer, const FSequencerSectionPainter& InPainter, const FKeyRenderer::FCachedState& InCachedState, ECacheFlags CacheFlags)
{
	if (CacheFlags == ECacheFlags::None)
	{
		// Cache is still hot - nothing to do
		return;
	}

	// ------------------------------------------------------------------------------
	// @todo: This function can still be pretty burdonsome for section layouts with
	// large numbers of nested key areas. In general we do not see more than ~10 key areas
	// but control rig sections can have many hundreds of key areas. More optimization
	// efforts may be required here - for the most part efforts have focused on reducing
	// the frequency of computation, rather than speeding up the algorithm or re-arranging
	// the cached data to make this faster (ie combining all keys into a single array / grid)
	// ------------------------------------------------------------------------------

	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FTimeToPixel& TimeToPixelConverter = InPainter.GetTimeConverter();

	TSharedPtr<ISequencerHotspot> Hotspot = Sequencer->GetHotspot();
	TArrayView<const FSequencerSelectedKey> HoveredKeys;

	if (Hotspot.IsValid() && Hotspot->GetType() == ESequencerHotspot::Key)
	{
		HoveredKeys = static_cast<FKeyHotspot*>(Hotspot.Get())->Keys;
	}

	const TSet<FSequencerSelectedKey>& SelectedKeys = Sequencer->GetSelection().GetSelectedKeys();
	const TMap<FSequencerSelectedKey, ESelectionPreviewState>& SelectionPreview = Sequencer->GetSelectionPreview().GetDefinedKeyStates();

	const bool bHasAnySelection = SelectedKeys.Num() != 0;
	const bool bHasAnySelectionPreview = SelectionPreview.Num() != 0;
	const bool bHasAnyHoveredKeys = HoveredKeys.Num() != 0;

	// ------------------------------------------------------------------------------
	// Update view-dependent data for each draw info
	for (FCachedKeyDrawInformation& Info : KeyDrawInfo)
	{
		Info.CacheViewDependentData(InCachedState.PaddedViewRange, CacheFlags);
	}

	// ------------------------------------------------------------------------------
	// If the data has changed, or key state has changed, or the view has been zoomed
	// we cannot preserve any keys (because we don't know whether they are still valid)
	const bool bCanPreserveKeys = !EnumHasAnyFlags(CacheFlags, FKeyRenderer::ECacheFlags::DataChanged | FKeyRenderer::ECacheFlags::ViewZoomed | FKeyRenderer::ECacheFlags::KeyStateChanged);

	FFrameNumber PreserveStartFrame = FFrameNumber(TNumericLimits<int32>::Max());
	TArray<FKey> PreservedKeys;

	// Attempt to preserve any previously computed key draw information
	if (bCanPreserveKeys && PrecomputedKeys.Num() != 0)
	{
		const FFrameNumber LowerBoundFrame = (InCachedState.PaddedViewRange.GetLowerBoundValue() * TickResolution).CeilToFrame();
		const FFrameNumber UpperBoundFrame = (InCachedState.PaddedViewRange.GetUpperBoundValue() * TickResolution).FloorToFrame();

		const int32 PreserveStartIndex = Algo::LowerBoundBy(PrecomputedKeys, LowerBoundFrame, &FKey::KeyTickStart);
		const int32 PreserveEndIndex   = Algo::UpperBoundBy(PrecomputedKeys, UpperBoundFrame, &FKey::KeyTickEnd);

		const int32 PreserveNum = PreserveEndIndex - PreserveStartIndex;
		if (PreserveNum > 0)
		{
			PreservedKeys = TArray<FKey>(PrecomputedKeys.GetData() + PreserveStartIndex, PreserveNum);
			PreserveStartFrame = PreservedKeys[0].KeyTickStart;

			FFrameNumber ActualPreserveEndFrame = PreservedKeys.Last().KeyTickEnd;
			for (FCachedKeyDrawInformation& Info : KeyDrawInfo)
			{
				Info.PreserveToIndex = Algo::UpperBound(Info.FramesInRange, ActualPreserveEndFrame);
			}
		}
	}

	// ------------------------------------------------------------------------------
	// Begin precomputation of keys to draw
	PrecomputedKeys.Reset();

	static float PixelOverlapThreshold = 3.f;
	const double TimeOverlapThreshold = TimeToPixelConverter.PixelToSeconds(PixelOverlapThreshold) - TimeToPixelConverter.PixelToSeconds(0.f);

	auto AnythingLeftToDraw = [](const FCachedKeyDrawInformation& In)
	{
		return In.NextUnhandledIndex < In.TimesInRange.Num();
	};

	// Keep iterating all the cached key positions until we've moved through everything
	// As stated above - this loop does not scale well for large numbers of KeyDrawInfo
	// Which is generally not a problem, but is troublesome for Control Rigs
	while (KeyDrawInfo.ContainsByPredicate(AnythingLeftToDraw))
	{
		// Determine the next key position to draw
		FFrameNumber CardinalKeyFrame = FFrameNumber(TNumericLimits<int32>::Max());
		for (const FCachedKeyDrawInformation& Info : KeyDrawInfo)
		{
			if (Info.NextUnhandledIndex < Info.TimesInRange.Num())
			{
				CardinalKeyFrame = FMath::Min(CardinalKeyFrame, Info.FramesInRange[Info.NextUnhandledIndex]);
			}
		}

		// If the cardinal time overlaps the preserved range, skip those keys
		if (CardinalKeyFrame >= PreserveStartFrame && PreservedKeys.Num() != 0)
		{
			PrecomputedKeys.Append(PreservedKeys);
			PreservedKeys.Empty();
			for (FCachedKeyDrawInformation& Info : KeyDrawInfo)
			{
				Info.NextUnhandledIndex = Info.PreserveToIndex;
			}
			continue;
		}

		double CardinalKeyTime = CardinalKeyFrame / TickResolution;

		// Start grouping keys at the current key time plus 99% of the threshold to ensure that we group at the center of keys
		// and that we avoid floating point precision issues where there is only one key [(KeyTime + TimeOverlapThreshold) - KeyTime != TimeOverlapThreshold] for some floats
		CardinalKeyTime += TimeOverlapThreshold*0.9994f;

		// Track whether all of the keys are within the valid range
		bool bIsInRange = true;

		const FFrameNumber ValidPlayRangeMin = InCachedState.ValidPlayRangeMin;
		const FFrameNumber ValidPlayRangeMax = InCachedState.ValidPlayRangeMax;

		double AverageKeyTime = 0.f;
		int32 NumKeyTimes = 0;

		FFrameNumber KeyTickStart = FFrameNumber(TNumericLimits<int32>::Max());
		FFrameNumber KeyTickEnd   = FFrameNumber(TNumericLimits<int32>::Lowest());

		auto HandleKey = [&bIsInRange, &AverageKeyTime, &NumKeyTimes, &KeyTickStart, &KeyTickEnd, ValidPlayRangeMin, ValidPlayRangeMax](FFrameNumber KeyFrame, double KeyTime)
		{
			if (bIsInRange && (KeyFrame < ValidPlayRangeMin || KeyFrame >= ValidPlayRangeMax))
			{
				bIsInRange = false;
			}

			KeyTickStart = FMath::Min(KeyFrame, KeyTickStart);
			KeyTickEnd   = FMath::Max(KeyFrame, KeyTickEnd);

			AverageKeyTime += KeyTime;
			++NumKeyTimes;
		};


		bool bFoundKey = false;
		FKey NewKey;

		int32 NumPreviewSelected = 0;
		int32 NumPreviewNotSelected = 0;
		int32 NumSelected = 0;
		int32 NumHovered = 0;
		int32 TotalNumKeys = 0;
		int32 NumOverlaps = 0;

		// Determine the ranges of keys considered to reside at this position
		for (int32 DrawIndex = 0; DrawIndex < KeyDrawInfo.Num(); ++DrawIndex)
		{
			FCachedKeyDrawInformation& Info = KeyDrawInfo[DrawIndex];
			if (Info.NextUnhandledIndex >= Info.TimesInRange.Num())
			{
				NewKey.Flags |= EKeyRenderingFlags::PartialKey;
				continue;
			}
			else if (!FMath::IsNearlyEqual(Info.TimesInRange[Info.NextUnhandledIndex], CardinalKeyTime, TimeOverlapThreshold))
			{
				NewKey.Flags |= EKeyRenderingFlags::PartialKey;
				continue;
			}

			int32 ThisNumOverlaps = -1;
			do
			{
				HandleKey(Info.FramesInRange[Info.NextUnhandledIndex], Info.TimesInRange[Info.NextUnhandledIndex]);

				if (!bFoundKey)
				{
					NewKey.Params = Info.DrawParams[Info.NextUnhandledIndex];
					bFoundKey = true;
				}
				else if (Info.DrawParams[Info.NextUnhandledIndex] != NewKey.Params)
				{
					NewKey.Flags |= EKeyRenderingFlags::PartialKey;
				}

				// Avoid creating FSequencerSelectedKeys unless absolutely necessary
				FKeyHandle ThisKeyHandle = Info.HandlesInRange[Info.NextUnhandledIndex];
				const TSharedPtr<IKeyArea>& ThisKeyArea = Info.CachedKeyPositions.GetKeyArea();

				if (bHasAnySelection)
				{
					FSequencerSelectedKey TestKey(InPainter.Section, ThisKeyArea, ThisKeyHandle);
					if (SelectedKeys.Contains(TestKey))
					{
						++NumSelected;
					}
				}
				if (bHasAnySelectionPreview)
				{
					FSequencerSelectedKey TestKey(InPainter.Section, ThisKeyArea, ThisKeyHandle);

					if (const ESelectionPreviewState* SelectionPreviewState = SelectionPreview.Find(TestKey))
					{
						NumPreviewSelected    += int32(*SelectionPreviewState == ESelectionPreviewState::Selected);
						NumPreviewNotSelected += int32(*SelectionPreviewState == ESelectionPreviewState::NotSelected);
					}
				}
				if (bHasAnyHoveredKeys)
				{
					FSequencerSelectedKey TestKey(InPainter.Section, ThisKeyArea, ThisKeyHandle);
					NumHovered += int32(HoveredKeys.Contains(TestKey));
				}

				++TotalNumKeys;
				++Info.NextUnhandledIndex;
				++ThisNumOverlaps;
			}
			while (Info.NextUnhandledIndex < Info.TimesInRange.Num() && FMath::IsNearlyEqual(Info.TimesInRange[Info.NextUnhandledIndex], CardinalKeyTime, TimeOverlapThreshold));

			NumOverlaps += ThisNumOverlaps;
		}

		if (NumKeyTimes == 0) //-V547
		{
			// This is not actually possible since HandleKey must have been called
			// at least once, but it needs to be here to avoid a static analysis warning
			break;
		}

		NewKey.FinalKeyPositionSeconds = AverageKeyTime / NumKeyTimes;
		NewKey.KeyTickStart = KeyTickStart;
		NewKey.KeyTickEnd = KeyTickEnd;

		if (EnumHasAnyFlags(NewKey.Flags, EKeyRenderingFlags::PartialKey))
		{
			static const FSlateBrush* PartialKeyBrush = FEditorStyle::GetBrush("Sequencer.PartialKey");
			NewKey.Params.FillBrush = NewKey.Params.BorderBrush = PartialKeyBrush;
		}

		// Determine the key color based on its selection/hover states
		if (NumPreviewSelected == TotalNumKeys)
		{
			NewKey.Flags |= EKeyRenderingFlags::PreviewSelected;
		}
		else if (NumPreviewNotSelected == TotalNumKeys)
		{
			NewKey.Flags |= EKeyRenderingFlags::PreviewNotSelected;
		}
		else if (NumSelected == TotalNumKeys)
		{
			NewKey.Flags |= EKeyRenderingFlags::Selected;
		}
		else if (NumSelected != 0)
		{
			NewKey.Flags |= EKeyRenderingFlags::AnySelected;
		}
		else if (NumHovered == TotalNumKeys)
		{
			NewKey.Flags |= EKeyRenderingFlags::Hovered;
		}

		if (NumOverlaps > 0)
		{
			NewKey.Flags |= EKeyRenderingFlags::Overlaps;
		}

		if (!bIsInRange)
		{
			NewKey.Flags |= EKeyRenderingFlags::OutOfRange;
		}

		PrecomputedKeys.Add(NewKey);
	}
}

void FKeyRenderer::FKeyDrawBatch::Draw(FSequencer* Sequencer, FSequencerSectionPainter& Painter, const FGeometry& KeyGeometry, const FPaintStyle& Style, const FKeyRendererPaintArgs& Args) const
{
	const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();

	TOptional<FSlateClippingState> PreviousClipState = Painter.DrawElements.GetClippingState();
	Painter.DrawElements.PopClip();

	const int32 KeyLayer = Painter.LayerId;

	const ESlateDrawEffect BaseDrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	for (const FKey& Key : PrecomputedKeys)
	{
		FKeyDrawParams Params = Key.Params;

		if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::PartialKey))
		{
			Params.FillOffset = FVector2D(0.f, 0.f);
			Params.FillTint   = Params.BorderTint  = FLinearColor::White;
		}

		const bool bSelected = EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::Selected);
		// Determine the key color based on its selection/hover states
		if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::PreviewSelected))
		{
			FLinearColor PreviewSelectionColor = Style.SelectionColor.LinearRGBToHSV();
			PreviewSelectionColor.R += 0.1f; // +10% hue
			PreviewSelectionColor.G = 0.6f; // 60% saturation
			Params.BorderTint = Params.FillTint = PreviewSelectionColor.HSVToLinearRGB();
		}
		else if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::PreviewNotSelected))
		{
			Params.BorderTint = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
		}
		else if (bSelected)
		{
			Params.BorderTint = Style.SelectionColor;
			Params.FillTint = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
		}
		else if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::AnySelected))
		{
			// partially selected
			Params.BorderTint = Style.SelectionColor.CopyWithNewOpacity(0.5f);
			Params.FillTint = FLinearColor(0.05f, 0.05f, 0.05f, 0.5f);
		}
		else if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::Hovered))
		{
			Params.BorderTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
			Params.FillTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			Params.BorderTint = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
		}

		// Color keys with overlaps with a red border
		if (EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::Overlaps))
		{
			Params.BorderTint = FLinearColor(0.83f, 0.12f, 0.12f, 1.0f); // Red
		}

		const ESlateDrawEffect KeyDrawEffects = EnumHasAnyFlags(Key.Flags, EKeyRenderingFlags::OutOfRange) ? ESlateDrawEffect::DisabledEffect : BaseDrawEffects;

		// draw border
		const FVector2D KeySize = bSelected ? SequencerSectionConstants::KeySize + Args.ThrobAmount * Args.KeyThrobValue : SequencerSectionConstants::KeySize;

		static const float BrushBorderWidth = 2.0f;

		const float KeyPositionPx = TimeToPixelConverter.SecondsToPixel(Key.FinalKeyPositionSeconds);

		FSlateDrawElement::MakeBox(
			Painter.DrawElements,
			// always draw selected keys on top of other keys
			bSelected ? KeyLayer + 1 : KeyLayer,
			// Center the key along Y.  Ensure the middle of the key is at the actual key time
			KeyGeometry.ToPaintGeometry(
				FVector2D(
					KeyPositionPx - FMath::CeilToFloat(KeySize.X / 2.0f),
					((KeyGeometry.GetLocalSize().Y / 2.0f) - (KeySize.Y / 2.0f))
				),
				KeySize
			),
			Params.BorderBrush,
			KeyDrawEffects,
			Params.BorderTint
		);

		// draw fill
		FSlateDrawElement::MakeBox(
			Painter.DrawElements,
			// always draw selected keys on top of other keys
			bSelected ? KeyLayer + 2 : KeyLayer + 1,
			// Center the key along Y.  Ensure the middle of the key is at the actual key time
			KeyGeometry.ToPaintGeometry(
				Params.FillOffset + 
				FVector2D(
					(KeyPositionPx - FMath::CeilToFloat((KeySize.X / 2.0f) - BrushBorderWidth)),
					((KeyGeometry.GetLocalSize().Y / 2.0f) - ((KeySize.Y / 2.0f) - BrushBorderWidth))
				),
				KeySize - 2.0f * BrushBorderWidth
			),
			Params.FillBrush,
			KeyDrawEffects,
			Params.FillTint
		);
	}

	Painter.LayerId = KeyLayer + 2;
	Painter.DrawElements.GetClippingManager().PushClippingState(PreviousClipState.GetValue());
}


void FKeyRenderer::DrawLayoutElement(FSequencer* Sequencer, const FSequencerSectionPainter& SectionPainter, const FSectionLayoutElement& LayoutElement, const FPaintStyle& Style, const FKeyRendererPaintArgs& Args) const
{
	FGeometry KeyAreaGeometry = LayoutElement.ComputeGeometry(SectionPainter.SectionGeometry);

	TArrayView<const TSharedRef<IKeyArea>> KeyAreas = LayoutElement.GetKeyAreas();

	TOptional<FLinearColor> ChannelColor;
	if (KeyAreas.Num() == 1 && Sequencer->GetSequencerSettings()->GetShowChannelColors())
	{
		ChannelColor = KeyAreas[0]->GetColor();
	}

	FSequencerSelection& Selection = Sequencer->GetSelection();

	const ESlateDrawEffect DrawEffects = SectionPainter.bParentEnabled
		? ESlateDrawEffect::None
		: ESlateDrawEffect::DisabledEffect;

	// --------------------------------------------
	// Draw the channel strip if necessary
	if (ChannelColor.IsSet())
	{
		static float BoxThickness = 5.f;
		static const FSlateBrush* const StripeOverlayBrush = FEditorStyle::GetBrush("Sequencer.Section.StripeOverlay");

		FVector2D KeyAreaSize = KeyAreaGeometry.GetLocalSize();
		FSlateDrawElement::MakeBox( 
			SectionPainter.DrawElements,
			SectionPainter.LayerId,
			KeyAreaGeometry.ToPaintGeometry(FVector2D(KeyAreaSize.X, BoxThickness), FSlateLayoutTransform(FVector2D(0.f, KeyAreaSize.Y*.5f - BoxThickness*.5f))),
			StripeOverlayBrush,
			DrawEffects,
			ChannelColor.GetValue()
		); 
	}

	TSharedPtr<FSequencerDisplayNode> DisplayNode = LayoutElement.GetDisplayNode();
	if (DisplayNode.IsValid())
	{
		TSharedRef<FSequencerDisplayNode> DisplayNodeRef = DisplayNode.ToSharedRef();

		FLinearColor HighlightColor;
		bool bDrawHighlight = false;
		if (Sequencer->GetSelection().NodeHasSelectedKeysOrSections(DisplayNodeRef))
		{
			bDrawHighlight = true;
			HighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.15f);
		}
		else if (DisplayNode->IsHovered())
		{
			bDrawHighlight = true;
			HighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.05f);
		}

		// --------------------------------------------
		// Draw hover or selection highlight
		if (bDrawHighlight)
		{
			FSlateDrawElement::MakeBox(
				SectionPainter.DrawElements,
				SectionPainter.LayerId,
				KeyAreaGeometry.ToPaintGeometry(),
				Style.HighlightBrush,
				DrawEffects,
				HighlightColor
			);
		}

		// --------------------------------------------
		// Draw display node selection tint
		if (Selection.IsSelected(DisplayNodeRef))
		{
			FSlateDrawElement::MakeBox(
				SectionPainter.DrawElements,
				SectionPainter.LayerId,
				KeyAreaGeometry.ToPaintGeometry(),
				Style.SelectedTrackTintBrush,
				DrawEffects,
				Style.SelectionColor
			);
		}
	}

	// --------------------------------------------
	// Draw section selection tint
	const bool bSectionSelected = Selection.IsSelected(&SectionPainter.Section);
	if (bSectionSelected && Args.SectionThrobValue != 0.f)
	{
		FSlateDrawElement::MakeBox(
			SectionPainter.DrawElements,
			SectionPainter.LayerId,
			KeyAreaGeometry.ToPaintGeometry(),
			Style.BackgroundTrackTintBrush,
			DrawEffects,
			Style.SelectionColor.CopyWithNewOpacity(Args.SectionThrobValue)
		);
	}
}

void FKeyRenderer::UpdateKeyLayouts(FSequencer* Sequencer, const FSequencerSectionPainter& InPainter, const FSectionLayout& InSectionLayout) const
{
	// Update the cache
	FCachedState NewCachedState(InPainter, Sequencer);
	ECacheFlags CacheFlags = ECacheFlags::All;

	if (CachedState.IsSet())
	{
		CacheFlags = CachedState->CompareTo(NewCachedState);
	}

	CachedState = NewCachedState;

	if (CachedState->PaddedViewRange.IsEmpty())
	{
		CachedKeyLayouts.Reset();
		return;
	}

	// Update key layouts by retaining existing pre-computed layouts where possible
	TMap<FSectionLayoutElement, FKeyDrawBatch, FDefaultSetAllocator, FLayoutElementKeyFuncs> OldKeyLayouts;
	Swap(OldKeyLayouts, CachedKeyLayouts);

	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	FVector2D ClipTopLeft     = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetTopLeft());
	FVector2D ClipBottomRight = InPainter.SectionGeometry.AbsoluteToLocal(InPainter.SectionClippingRect.GetBottomRight());

	// Section layouts are always ordered top to bottom - skip over any that are not in the current view
	for (const FSectionLayoutElement& LayoutElement : InSectionLayout.GetElements())
	{
		if (LayoutElement.GetOffset() + LayoutElement.GetHeight() < ClipTopLeft.Y)
		{
			continue;
		}
		if (LayoutElement.GetOffset() > ClipBottomRight.Y)
		{
			break;
		}
		if (LayoutElement.GetKeyAreas().Num() == 0)
		{
			continue;
		}

		FKeyDrawBatch* ExistingBatch = OldKeyLayouts.Find(LayoutElement);
		if (!ExistingBatch)
		{
			// A new cache needs to be created
			FKeyDrawBatch NewBatch(LayoutElement);
			NewBatch.UpdateViewIndependentData(TickResolution);
			NewBatch.UpdateViewDependentData(Sequencer, InPainter, NewCachedState, ECacheFlags::All);
			CachedKeyLayouts.Add(LayoutElement, MoveTemp(NewBatch));
		}
		else
		{
			// This is the common path - we already have a cached key batch, we just need to check whether we need to re-generate it
			ECacheFlags ThisCacheFlags = CacheFlags | ExistingBatch->UpdateViewIndependentData(TickResolution);

			// We can reuse this key layout - update all the cached key positions
			ExistingBatch->UpdateViewDependentData(Sequencer, InPainter, NewCachedState, ThisCacheFlags);
			CachedKeyLayouts.Add(LayoutElement, MoveTemp(*ExistingBatch));
		}
	}
}

void FKeyRenderer::Paint(const FSectionLayout& InSectionLayout, const FWidgetStyle& InWidgetStyle, const FKeyRendererPaintArgs& Args, FSequencer* Sequencer, FSequencerSectionPainter& InPainter) const
{
	FPaintStyle Style(InWidgetStyle);

	UpdateKeyLayouts(Sequencer, InPainter, InSectionLayout);

	for (const FSectionLayoutElement& LayoutElement : InSectionLayout.GetElements())
	{
		DrawLayoutElement(Sequencer, InPainter, LayoutElement, Style, Args);

		if (const FKeyDrawBatch* KeyDrawBatch = CachedKeyLayouts.Find(LayoutElement))
		{
			FGeometry KeyGeometry = LayoutElement.ComputeGeometry(InPainter.SectionGeometry);
			KeyDrawBatch->Draw(Sequencer, InPainter, KeyGeometry, Style, Args);
		}
	}
}


} // namespace Sequencer
} // namespace UE

