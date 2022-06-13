// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSequencerSection.h"
#include "MVVM/ViewModels/SectionModel.h"
#include "MVVM/ViewModels/TrackModel.h"
#include "MVVM/ViewModels/CategoryModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/Views/ITrackAreaHotspot.h"
#include "MVVM/Views/STrackAreaView.h"
#include "MVVM/Extensions/IObjectBindingExtension.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "SequencerSelectionPreview.h"
#include "SequencerSettings.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Sequencer.h"
#include "SequencerSectionPainter.h"
#include "MovieSceneSequence.h"
#include "CommonMovieSceneTools.h"
#include "ISequencerEditTool.h"
#include "ISequencerSection.h"
#include "SequencerHotspots.h"
#include "Widgets/SOverlay.h"
#include "SequencerAddKeyOperation.h"
#include "MovieScene.h"
#include "Fonts/FontCache.h"
#include "Framework/Application/SlateApplication.h"
#include "KeyDrawParams.h"
#include "MovieSceneTimeHelpers.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Generators/MovieSceneEasingFunction.h"

namespace UE
{
namespace Sequencer
{

double SSequencerSection::SectionSelectionThrobEndTime = 0;
double SSequencerSection::KeySelectionThrobEndTime = 0;


/** A point on an easing curve used for rendering */
struct FEasingCurvePoint
{
	FEasingCurvePoint(FVector2D InLocation, const FLinearColor& InPointColor) : Location(InLocation), Color(InPointColor) {}

	/** The location of the point (x=time, y=easing value [0-1]) */
	FVector2D Location;
	/** The color of the point */
	FLinearColor Color;
};

FTimeToPixel ConstructTimeConverterForSection(const FGeometry& InSectionGeometry, const UMovieSceneSection& InSection, FSequencer& InSequencer)
{
	TRange<double> ViewRange       = InSequencer.GetViewRange();

	FFrameRate     TickResolution  = InSection.GetTypedOuter<UMovieScene>()->GetTickResolution();
	double         LowerTime       = InSection.HasStartFrame() ? InSection.GetInclusiveStartFrame() / TickResolution : ViewRange.GetLowerBoundValue();
	double         UpperTime       = InSection.HasEndFrame()   ? InSection.GetExclusiveEndFrame()   / TickResolution : ViewRange.GetUpperBoundValue();

	return FTimeToPixel(InSectionGeometry, TRange<double>(LowerTime, UpperTime), TickResolution);
}


struct FSequencerSectionPainterImpl : FSequencerSectionPainter
{
	FSequencerSectionPainterImpl(FSequencer& InSequencer, TSharedPtr<FSectionModel> InSection, FSlateWindowElementList& _OutDrawElements, const FGeometry& InSectionGeometry, const SSequencerSection& InSectionWidget)
		: FSequencerSectionPainter(_OutDrawElements, InSectionGeometry, InSection)
		, Sequencer(InSequencer)
		, SectionWidget(InSectionWidget)
		, TimeToPixelConverter(ConstructTimeConverterForSection(SectionGeometry, *InSection->GetSection(), Sequencer))
	{
		CalculateSelectionColor();

		const ISequencerEditTool* EditTool = InSequencer.GetViewModel()->GetTrackArea()->GetEditTool();
		Hotspot = EditTool ? EditTool->GetDragHotspot() : nullptr;
		if (!Hotspot)
		{
			Hotspot = InSequencer.GetViewModel()->GetTrackArea()->GetHotspot();
		}
	}

	FLinearColor GetFinalTintColor(const FLinearColor& Tint) const
	{
		FLinearColor FinalTint = STrackAreaView::BlendDefaultTrackColor(Tint);
		if (bIsHighlighted && SectionModel->GetRange() != TRange<FFrameNumber>::All())
		{
			float Lum = FinalTint.GetLuminance() * 0.2f;
			FinalTint = FinalTint + FLinearColor(Lum, Lum, Lum, 0.f);
		}

		FinalTint.A *= GhostAlpha;

		return FinalTint;
	}

	virtual int32 PaintSectionBackground(const FLinearColor& Tint) override
	{
		UMovieSceneSection* SectionObject = SectionModel->GetSection();

		const ESlateDrawEffect DrawEffects = bParentEnabled
			? ESlateDrawEffect::None
			: ESlateDrawEffect::DisabledEffect;

		static const FSlateBrush* SectionBackgroundBrush = FAppStyle::GetBrush("Sequencer.Section.Background");
		static const FSlateBrush* SectionBackgroundTintBrush = FAppStyle::GetBrush("Sequencer.Section.BackgroundTint");
		static const FSlateBrush* SelectedSectionOverlay = FAppStyle::GetBrush("Sequencer.Section.SelectedSectionOverlay");

		FLinearColor FinalTint = GetFinalTintColor(Tint);

		// Offset lower bounds and size for infinte sections so we don't draw the rounded border on the visible area
		const float InfiniteLowerOffset = SectionObject->HasStartFrame() ? 0.f : 100.f;
		const float InfiniteSizeOffset  = InfiniteLowerOffset + (SectionObject->HasEndFrame() ? 0.f : 100.f);

		FPaintGeometry PaintGeometry = SectionGeometry.ToPaintGeometry(
			SectionGeometry.GetLocalSize() + FVector2D(InfiniteSizeOffset, 0.f),
			FSlateLayoutTransform(FVector2D(-InfiniteLowerOffset, 0.f))
			);

		if (Sequencer.GetSequencerSettings()->ShouldShowPrePostRoll())
		{
			TOptional<FSlateClippingState> PreviousClipState = DrawElements.GetClippingState();
			DrawElements.PopClip();

			static const FSlateBrush* PreRollBrush = FAppStyle::GetBrush("Sequencer.Section.PreRoll");
			float BrushHeight = 16.f, BrushWidth = 10.f;

			if (SectionObject->HasStartFrame())
			{
				FFrameNumber SectionStartTime = SectionObject->GetInclusiveStartFrame();
				FFrameNumber PreRollStartTime = SectionStartTime - SectionObject->GetPreRollFrames();

				const float PreRollPx = TimeToPixelConverter.FrameToPixel(SectionStartTime) - TimeToPixelConverter.FrameToPixel(PreRollStartTime);
				if (PreRollPx > 0)
				{
					const float RoundedPreRollPx = (int(PreRollPx / BrushWidth)+1) * BrushWidth;

					// Round up to the nearest BrushWidth size
					FGeometry PreRollArea = SectionGeometry.MakeChild(
						FVector2D(RoundedPreRollPx, BrushHeight),
						FSlateLayoutTransform(FVector2D(-PreRollPx, (SectionGeometry.GetLocalSize().Y - BrushHeight)*.5f))
						);

					FSlateDrawElement::MakeBox(
						DrawElements,
						LayerId,
						PreRollArea.ToPaintGeometry(),
						PreRollBrush,
						DrawEffects
					);
				}
			}

			if (SectionObject->HasEndFrame())
			{
				FFrameNumber SectionEndTime  = SectionObject->GetExclusiveEndFrame();
				FFrameNumber PostRollEndTime = SectionEndTime + SectionObject->GetPostRollFrames();

				const float PostRollPx = TimeToPixelConverter.FrameToPixel(PostRollEndTime) - TimeToPixelConverter.FrameToPixel(SectionEndTime);
				if (PostRollPx > 0)
				{
					const float RoundedPostRollPx = (int(PostRollPx / BrushWidth)+1) * BrushWidth;
					const float Difference = RoundedPostRollPx - PostRollPx;

					// Slate border brushes tile UVs along +ve X, so we round the arrows to a multiple of the brush width, and offset, to ensure we don't have a partial tile visible at the end
					FGeometry PostRollArea = SectionGeometry.MakeChild(
						FVector2D(RoundedPostRollPx, BrushHeight),
						FSlateLayoutTransform(FVector2D(SectionGeometry.GetLocalSize().X - Difference, (SectionGeometry.GetLocalSize().Y - BrushHeight)*.5f))
						);

					FSlateDrawElement::MakeBox(
						DrawElements,
						LayerId,
						PostRollArea.ToPaintGeometry(),
						PreRollBrush,
						DrawEffects
					);
				}
			}

			if (PreviousClipState.IsSet())
			{
				DrawElements.GetClippingManager().PushClippingState(PreviousClipState.GetValue());
			}
		}


		{
			TOptional<FSlateClippingState> PreviousClipState = DrawElements.GetClippingState();
			DrawElements.PopClip();

			// Draw the section background
			FSlateDrawElement::MakeBox(
				DrawElements,
				LayerId,
				PaintGeometry,
				SectionBackgroundBrush,
				DrawEffects
			);
			++LayerId;

			if (PreviousClipState.IsSet())
			{
				DrawElements.GetClippingManager().PushClippingState(PreviousClipState.GetValue());
			}
		}

		// Draw the section background tint over the background
		FSlateDrawElement::MakeBox(
			DrawElements,
			LayerId,
			PaintGeometry,
			SectionBackgroundTintBrush,
			DrawEffects,
			FinalTint
		);
		++LayerId;

		// Draw underlapping sections
		DrawOverlaps(FinalTint);

		// Draw empty space
		DrawEmptySpace();

		// Draw the blend type text
		DrawBlendType();

		// Draw easing curves
		DrawEasing(FinalTint);

		// Draw the selection hash
		if (SelectionColor.IsSet())
		{
			FSlateDrawElement::MakeBox(
				DrawElements,
				LayerId,
				SectionGeometry.ToPaintGeometry(FVector2D(1, 1), SectionGeometry.GetLocalSize() - FVector2D(2,2)),
				SelectedSectionOverlay,
				DrawEffects,
				SelectionColor.GetValue().CopyWithNewOpacity(0.8f)
			);
		}

		return LayerId;
	}

	virtual const FTimeToPixel& GetTimeConverter() const
	{
		return TimeToPixelConverter;
	}

	void CalculateSelectionColor()
	{
		FSequencerSelection& Selection = Sequencer.GetSelection();
		FSequencerSelectionPreview& SelectionPreview = Sequencer.GetSelectionPreview();

		ESelectionPreviewState SelectionPreviewState = SelectionPreview.GetSelectionState(SectionWidget.WeakSectionModel);

		if (SelectionPreviewState == ESelectionPreviewState::NotSelected)
		{
			// Explicitly not selected in the preview selection
			return;
		}
		
		if (SelectionPreviewState == ESelectionPreviewState::Undefined && !Selection.IsSelected(SectionModel))
		{
			// No preview selection for this section, and it's not selected
			return;
		}

		SelectionColor = FAppStyle::GetSlateColor(SequencerSectionConstants::SelectionColorName).GetColor(FWidgetStyle());

		// Use a muted selection color for selection previews
		if (SelectionPreviewState == ESelectionPreviewState::Selected)
		{
			SelectionColor.GetValue() = SelectionColor.GetValue().LinearRGBToHSV();
			SelectionColor.GetValue().R += 0.1f; // +10% hue
			SelectionColor.GetValue().G = 0.6f; // 60% saturation

			SelectionColor = SelectionColor.GetValue().HSVToLinearRGB();
		}

		SelectionColor->A *= GhostAlpha;
	}

	void DrawBlendType()
	{
		// Draw the blend type text if necessary
		UMovieSceneSection* SectionObject = SectionModel->GetSection();
		UMovieSceneTrack* Track = GetTrack();
		if (!Track || Track->GetSupportedBlendTypes().Num() <= 1 || !SectionObject->GetBlendType().IsValid() || !bIsHighlighted || SectionObject->GetBlendType().Get() == EMovieSceneBlendType::Absolute)
		{
			return;
		}

		TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();

		UEnum* Enum = FindObjectChecked<UEnum>(nullptr, TEXT("/Script/MovieScene.EMovieSceneBlendType"), true);
		FText DisplayText = Enum->GetDisplayNameTextByValue((int64)SectionObject->GetBlendType().Get());

		FSlateFontInfo FontInfo = FAppStyle::GetFontStyle("Sequencer.Section.BackgroundText");
		FontInfo.Size = 24;

		auto GetFontHeight = [&]
		{
			return FontCache->GetMaxCharacterHeight(FontInfo, 1.f) + FontCache->GetBaseline(FontInfo, 1.f);
		};
		while( GetFontHeight() > SectionGeometry.Size.Y && FontInfo.Size > 11 )
		{
			FontInfo.Size = FMath::Max(FMath::FloorToInt(FontInfo.Size - 6.f), 11);
		}

		FVector2D TextOffset = SectionModel->GetRange() == TRange<FFrameNumber>::All() ? FVector2D(0.f, -1.f) : FVector2D(1.f, -1.f);
		FVector2D BottomLeft = SectionGeometry.AbsoluteToLocal(SectionClippingRect.GetBottomLeft()) + TextOffset;

		FSlateDrawElement::MakeText(
			DrawElements,
			LayerId,
			SectionGeometry.MakeChild(
				FVector2D(SectionGeometry.Size.X, GetFontHeight()),
				FSlateLayoutTransform(BottomLeft - FVector2D(0.f, GetFontHeight()+1.f))
			).ToPaintGeometry(),
			DisplayText,
			FontInfo,
			bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
			FLinearColor(1.f,1.f,1.f,.2f)
		);
	}

	float GetEaseHighlightAmount(UMovieSceneSection* InSection, float EaseInInterp, float EaseOutInterp) const
	{
		using namespace UE::Sequencer;

		float EaseInScale = 0.f, EaseOutScale = 0.f;
		if (TSharedPtr<FSectionEasingHandleHotspot> EasingHandleHotspot = HotspotCast<FSectionEasingHandleHotspot>(Hotspot))
		{
			if (EasingHandleHotspot->GetSection() == InSection)
			{
				if (EasingHandleHotspot->HandleType == ESequencerEasingType::In)
				{
					EaseInScale = 1.f;
				}
				else
				{
					EaseOutScale = 1.f;
				}
			}
		}
		else if (TSharedPtr<FSectionEasingAreaHotspot> EasingAreaHotspot = HotspotCast<FSectionEasingAreaHotspot>(Hotspot))
		{
			for (const FEasingAreaHandle& Easing : EasingAreaHotspot->Easings)
			{
				if (Easing.WeakSectionModel.Pin()->GetSection() == InSection)
				{
					if (Easing.EasingType == ESequencerEasingType::In)
					{
						EaseInScale = 1.f;
					}
					else
					{
						EaseOutScale = 1.f;
					}
				}
			}
		}
		else
		{
			return 0.f;
		}

		const float TotalScale = EaseInScale + EaseOutScale;
		return TotalScale > 0.f ? EaseInInterp * (EaseInScale/TotalScale) + ((1.f-EaseOutInterp) * (EaseOutScale/TotalScale)) : 0.f;
	}

	FEasingCurvePoint MakeCurvePoint(UMovieSceneSection* InSection, FFrameTime Time, const FLinearColor& FinalTint, const FLinearColor& EaseSelectionColor) const
	{
		TOptional<float> EaseInValue, EaseOutValue;
		float EaseInInterp = 0.f, EaseOutInterp = 1.f;
		InSection->EvaluateEasing(Time, EaseInValue, EaseOutValue, &EaseInInterp, &EaseOutInterp);

		return FEasingCurvePoint(
			FVector2D(Time / TimeToPixelConverter.GetTickResolution(), EaseInValue.Get(1.f) * EaseOutValue.Get(1.f)),
			FMath::Lerp(FinalTint, EaseSelectionColor, GetEaseHighlightAmount(InSection, EaseInInterp, EaseOutInterp))
		);
	}

	/** Adds intermediate control points for the specified section's easing up to a given threshold */
	void RefineCurvePoints(UMovieSceneSection* SectionObject, const FLinearColor& FinalTint, const FLinearColor& EaseSelectionColor, TArray<FEasingCurvePoint>& InOutPoints)
	{
		static float GradientThreshold = .05f;
		static float ValueThreshold = .05f;

		float MinTimeSize = FMath::Max(0.0001, TimeToPixelConverter.PixelToSeconds(2.5) - TimeToPixelConverter.PixelToSeconds(0));

		for (int32 Index = 0; Index < InOutPoints.Num() - 1; ++Index)
		{
			const FEasingCurvePoint& Lower = InOutPoints[Index];
			const FEasingCurvePoint& Upper = InOutPoints[Index + 1];

			if ((Upper.Location.X - Lower.Location.X)*.5f > MinTimeSize)
			{
				FVector2D::FReal      NewPointTime  = (Upper.Location.X + Lower.Location.X)*.5f;
				FFrameTime FrameTime     = NewPointTime * TimeToPixelConverter.GetTickResolution();
				float      NewPointValue = SectionObject->EvaluateEasing(FrameTime);

				// Check that the gradient is changing significantly
				FVector2D::FReal LinearValue = (Upper.Location.Y + Lower.Location.Y) * .5f;
				FVector2D::FReal PointGradient = NewPointValue - SectionObject->EvaluateEasing(FMath::Lerp(Lower.Location.X, NewPointTime, 0.9f) * TimeToPixelConverter.GetTickResolution());
				FVector2D::FReal OuterGradient = Upper.Location.Y - Lower.Location.Y;
				if (!FMath::IsNearlyEqual(OuterGradient, PointGradient, GradientThreshold) ||
					!FMath::IsNearlyEqual(LinearValue, NewPointValue, ValueThreshold))
				{
					// Add the point
					InOutPoints.Insert(MakeCurvePoint(SectionObject, FrameTime, FinalTint, EaseSelectionColor), Index+1);
					--Index;
				}
			}
		}
	}

	void DrawEasingForSegment(const FOverlappingSections& Segment, const FGeometry& InnerSectionGeometry, const FLinearColor& FinalTint)
	{
		// @todo: sequencer-timecode: Test that start offset is not required here
		const float RangeStartPixel = TimeToPixelConverter.FrameToPixel(UE::MovieScene::DiscreteInclusiveLower(Segment.Range));
		const float RangeEndPixel = TimeToPixelConverter.FrameToPixel(UE::MovieScene::DiscreteExclusiveUpper(Segment.Range));
		const float RangeSizePixel = RangeEndPixel - RangeStartPixel;

		FGeometry RangeGeometry = InnerSectionGeometry.MakeChild(FVector2D(RangeSizePixel, InnerSectionGeometry.Size.Y), FSlateLayoutTransform(FVector2D(RangeStartPixel, 0.f)));
		if (!FSlateRect::DoRectanglesIntersect(RangeGeometry.GetLayoutBoundingRect(), ParentClippingRect))
		{
			return;
		}

		UMovieSceneTrack* Track = GetTrack();
		if (!Track)
		{
			return;
		}

		const FSlateBrush* MyBrush = FAppStyle::Get().GetBrush("Sequencer.Timeline.EaseInOut");

		FSlateResourceHandle ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*MyBrush);
		const FSlateShaderResourceProxy* ResourceProxy = ResourceHandle.GetResourceProxy();

		FVector2f AtlasOffset = ResourceProxy ? ResourceProxy->StartUV : FVector2f(0.f, 0.f);
		FVector2f AtlasUVSize = ResourceProxy ? ResourceProxy->SizeUV : FVector2f(1.f, 1.f);

		FSlateRenderTransform RenderTransform;

		const FVector2f Pos = FVector2f(RangeGeometry.GetAbsolutePosition());	// LWC_TODO: Precision loss
		const FVector2D Size = RangeGeometry.GetLocalSize();

		FLinearColor EaseSelectionColor = FAppStyle::GetSlateColor(SequencerSectionConstants::SelectionColorName).GetColor(FWidgetStyle());

		FColor FillColor(0,0,0,51);

		TArray<FEasingCurvePoint> CurvePoints;

		// Segment.Impls are already sorted bottom to top
		for (int32 CurveIndex = 0; CurveIndex < Segment.Sections.Num(); ++CurveIndex)
		{
			UMovieSceneSection* CurveSection = Segment.Sections[CurveIndex].Pin()->GetSection();

			// Make the points for the curve
			CurvePoints.Reset(20);
			{
				CurvePoints.Add(MakeCurvePoint(CurveSection, Segment.Range.GetLowerBoundValue(), FinalTint, EaseSelectionColor));
				CurvePoints.Add(MakeCurvePoint(CurveSection, Segment.Range.GetUpperBoundValue(), FinalTint, EaseSelectionColor));

				// Refine the control points
				int32 LastNumPoints;
				do
				{
					LastNumPoints = CurvePoints.Num();
					RefineCurvePoints(CurveSection, FinalTint, EaseSelectionColor, CurvePoints);
				} while(LastNumPoints != CurvePoints.Num());
			}

			TArray<SlateIndex> Indices;
			TArray<FSlateVertex> Verts;
			TArray<FVector2D> BorderPoints;
			TArray<FLinearColor> BorderPointColors;

			Indices.Reserve(CurvePoints.Num()*6);
			Verts.Reserve(CurvePoints.Num()*2);

			const FVector2f SizeAsFloatVec = FVector2f(Size);	// LWC_TODO: Precision loss

			for (const FEasingCurvePoint& Point : CurvePoints)
			{
				float SegmentStartTime = UE::MovieScene::DiscreteInclusiveLower(Segment.Range) / TimeToPixelConverter.GetTickResolution();
				float U = (Point.Location.X - SegmentStartTime) / ( FFrameNumber(UE::MovieScene::DiscreteSize(Segment.Range)) / TimeToPixelConverter.GetTickResolution() );

				// Add verts top->bottom
				FVector2f UV(U, 0.f);
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, (Pos + UV*SizeAsFloatVec*RangeGeometry.Scale), AtlasOffset + UV*AtlasUVSize, FillColor));	// LWC_TODO: Precision loss

				UV.Y = 1.f - Point.Location.Y;
				BorderPoints.Add(FVector2D(UV)*Size);
				BorderPointColors.Add(Point.Color);
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, (Pos + UV*SizeAsFloatVec*RangeGeometry.Scale), AtlasOffset + FVector2f(UV.X, 0.5f)*AtlasUVSize, FillColor));	// LWC_TODO: Precision loss

				if (Verts.Num() >= 4)
				{
					int32 Index0 = Verts.Num()-4, Index1 = Verts.Num()-3, Index2 = Verts.Num()-2, Index3 = Verts.Num()-1;
					Indices.Add(Index0);
					Indices.Add(Index1);
					Indices.Add(Index2);

					Indices.Add(Index1);
					Indices.Add(Index2);
					Indices.Add(Index3);
				}
			}

			if (Indices.Num())
			{
				FSlateDrawElement::MakeCustomVerts(
					DrawElements,
					LayerId,
					ResourceHandle,
					Verts,
					Indices,
					nullptr,
					0,
					0, ESlateDrawEffect::PreMultipliedAlpha);

				const ESlateDrawEffect DrawEffects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
				FSlateDrawElement::MakeLines(
					DrawElements,
					LayerId + 1,
					RangeGeometry.ToPaintGeometry(),
					BorderPoints,
					BorderPointColors,
					DrawEffects | ESlateDrawEffect::PreMultipliedAlpha,
					FLinearColor::White,
					true);
			}
		}

		++LayerId;
	}

	void DrawEasing(const FLinearColor& FinalTint)
	{
		if (!SectionModel->GetSection()->GetBlendType().IsValid())
		{
			return;
		}

		// Compute easing geometry by insetting from the current section geometry by 1px
		FGeometry InnerSectionGeometry = SectionGeometry.MakeChild(SectionGeometry.Size - FVector2D(2.f, 2.f), FSlateLayoutTransform(FVector2D(1.f, 1.f)));
		for (const FOverlappingSections& Segment : SectionWidget.UnderlappingEasingSegments)
		{
			DrawEasingForSegment(Segment, InnerSectionGeometry, FinalTint);
		}

		++LayerId;
	}

	void DrawOverlaps(const FLinearColor& FinalTint)
	{
		using namespace UE::Sequencer;

		FGeometry InnerSectionGeometry = SectionGeometry.MakeChild(SectionGeometry.Size - FVector2D(2.f, 2.f), FSlateLayoutTransform(FVector2D(1.f, 1.f)));

		UMovieSceneTrack* Track = GetTrack();
		if (!Track)
		{
			return;
		}

		static const FSlateBrush* PinCusionBrush = FAppStyle::GetBrush("Sequencer.Section.PinCusion");
		static const FSlateBrush* OverlapBorderBrush = FAppStyle::GetBrush("Sequencer.Section.OverlapBorder");

		const ESlateDrawEffect DrawEffects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		const float StartTimePixel = SectionModel->GetSection()->HasStartFrame() ? TimeToPixelConverter.FrameToPixel(SectionModel->GetSection()->GetInclusiveStartFrame()) : 0.f;

		for (int32 SegmentIndex = 0; SegmentIndex < SectionWidget.UnderlappingSegments.Num(); ++SegmentIndex)
		{
			const FOverlappingSections& Segment = SectionWidget.UnderlappingSegments[SegmentIndex];

			const float RangeStartPixel	= Segment.Range.GetLowerBound().IsOpen() ? 0.f							: TimeToPixelConverter.FrameToPixel(UE::MovieScene::DiscreteInclusiveLower(Segment.Range));
			const float RangeEndPixel	= Segment.Range.GetUpperBound().IsOpen() ? InnerSectionGeometry.Size.X	: TimeToPixelConverter.FrameToPixel(UE::MovieScene::DiscreteExclusiveUpper(Segment.Range));
			const float RangeSizePixel	= RangeEndPixel - RangeStartPixel;

			FGeometry RangeGeometry = InnerSectionGeometry.MakeChild(FVector2D(RangeSizePixel, InnerSectionGeometry.Size.Y), FSlateLayoutTransform(FVector2D(RangeStartPixel - StartTimePixel, 0.f)));
			if (!FSlateRect::DoRectanglesIntersect(RangeGeometry.GetLayoutBoundingRect(), ParentClippingRect))
			{
				continue;
			}

			const FOverlappingSections* NextSegment = SegmentIndex < SectionWidget.UnderlappingSegments.Num() - 1 ? &SectionWidget.UnderlappingSegments[SegmentIndex+1] : nullptr;
			const bool bDrawRightMostBound = !NextSegment || !Segment.Range.Adjoins(NextSegment->Range);

			FSlateDrawElement::MakeBox(
				DrawElements,
				LayerId,
				RangeGeometry.ToPaintGeometry(),
				PinCusionBrush,
				DrawEffects,
				FinalTint
			);

			FPaintGeometry PaintGeometry = bDrawRightMostBound ? RangeGeometry.ToPaintGeometry() : RangeGeometry.ToPaintGeometry(FVector2D(RangeGeometry.Size) + FVector2D(10.f, 0.f), FSlateLayoutTransform(FVector2D::ZeroVector));
			FSlateDrawElement::MakeBox(
				DrawElements,
				LayerId,
				PaintGeometry,
				OverlapBorderBrush,
				DrawEffects,
				FLinearColor(1.f,1.f,1.f,.3f)
			);
		}

		++LayerId;
	}

	void DrawEmptySpace()
	{
		using namespace UE::Sequencer;

		const ESlateDrawEffect DrawEffects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
		static const FSlateBrush* EmptySpaceBrush = FAppStyle::GetBrush("Sequencer.Section.EmptySpace");

		// Attach contiguous regions together
		TOptional<FSlateRect> CurrentArea;

		for (const FSectionLayoutElement& Element : SectionWidget.Layout->GetElements())
		{
			const bool bIsEmptySpace = Element.GetModel()->IsA<FChannelModel>() && Element.GetChannels().Num() == 0;
			const bool bExistingEmptySpace = CurrentArea.IsSet();

			if (bIsEmptySpace && bExistingEmptySpace && FMath::IsNearlyEqual(CurrentArea->Bottom, Element.GetOffset()))
			{
				CurrentArea->Bottom = Element.GetOffset() + Element.GetHeight();
				continue;
			}

			if (bExistingEmptySpace)
			{
				FPaintGeometry PaintGeom = SectionGeometry.MakeChild(CurrentArea->GetSize(), FSlateLayoutTransform(CurrentArea->GetTopLeft2f())).ToPaintGeometry();
				FSlateDrawElement::MakeBox(DrawElements, LayerId, PaintGeom, EmptySpaceBrush, DrawEffects);
				CurrentArea.Reset();
			}

			if (bIsEmptySpace)
			{
				CurrentArea = FSlateRect::FromPointAndExtent(FVector2D(0.f, Element.GetOffset()), FVector2D(SectionGeometry.Size.X, Element.GetHeight()));
			}
		}

		if (CurrentArea.IsSet())
		{
			FPaintGeometry PaintGeom = SectionGeometry.MakeChild(CurrentArea->GetSize(), FSlateLayoutTransform(CurrentArea->GetTopLeft2f())).ToPaintGeometry();
			FSlateDrawElement::MakeBox(DrawElements, LayerId, PaintGeom, EmptySpaceBrush, DrawEffects);
		}

		++LayerId;
	}

	TOptional<FLinearColor> SelectionColor;
	FSequencer& Sequencer;
	const SSequencerSection& SectionWidget;
	FTimeToPixel TimeToPixelConverter;
	TSharedPtr<ITrackAreaHotspot> Hotspot;

	/** The clipping rectangle of the parent widget */
	FSlateRect ParentClippingRect;
};

void SSequencerSection::Construct( const FArguments& InArgs, TSharedPtr<FSequencer> InSequencer, TSharedPtr<FSectionModel> InSectionModel)
{
	using namespace UE::MovieScene;

	Sequencer = InSequencer;
	WeakSectionModel = InSectionModel;
	SectionInterface = InSectionModel->GetSectionInterface();
	Layout = FSectionLayout(InSectionModel);
	HandleOffsetPx = 0.f;

	SetEnabled(MakeAttributeSP(this, &SSequencerSection::IsEnabled));
	SetToolTipText(MakeAttributeSP(this, &SSequencerSection::GetToolTipText));

	UMovieSceneSection* Section = InSectionModel->GetSection();
	UMovieSceneTrack*   Track   = Section ? Section->GetTypedOuter<UMovieSceneTrack>() : nullptr;
	if (ensure(Track))
	{
		Track->EventHandlers.Link(TrackModifiedBinding, this);
	}

	UpdateUnderlappingSegments();

	ChildSlot
	[
		SectionInterface->GenerateSectionWidget()
	];
}

SSequencerSection::~SSequencerSection()
{
}

FText SSequencerSection::GetToolTipText() const
{
	const UMovieSceneSection* SectionObject = SectionInterface->GetSectionObject();
	const UMovieScene* MovieScene = SectionObject ? SectionObject->GetTypedOuter<UMovieScene>() : nullptr;

	// Optional section specific content to add to tooltip
	FText SectionToolTipContent = SectionInterface->GetSectionToolTip();

	FText SectionTitleText = SectionInterface->GetSectionTitle();
	if (!SectionTitleText.IsEmpty())
	{
		SectionTitleText = FText::Format(FText::FromString(TEXT("{0}\n")), SectionTitleText);
	}

	// If the objects are valid and the section is not unbounded, add frame information to the tooltip
	if (SectionObject && MovieScene && SectionObject->HasStartFrame() && SectionObject->HasEndFrame())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		int32 StartFrame = ConvertFrameTime(SectionObject->GetInclusiveStartFrame(), MovieScene->GetTickResolution(), MovieScene->GetDisplayRate()).RoundToFrame().Value;
		int32 EndFrame = ConvertFrameTime(SectionObject->GetExclusiveEndFrame(), MovieScene->GetTickResolution(), MovieScene->GetDisplayRate()).RoundToFrame().Value;
	
		FText SectionToolTip;
		if (SectionToolTipContent.IsEmpty())
		{
			SectionToolTip = FText::Format(NSLOCTEXT("SequencerSection", "TooltipFormat", "{0}{1} - {2} ({3} frames)"), SectionTitleText,
				StartFrame,
				EndFrame,
				EndFrame - StartFrame);
		}
		else
		{
			SectionToolTip = FText::Format(NSLOCTEXT("SequencerSection", "TooltipFormatWithSectionContent", "{0}{1} - {2} ({3} frames)\n{4}"), SectionTitleText,
				StartFrame,
				EndFrame,
				EndFrame - StartFrame,
				SectionToolTipContent);
		}
	
		if (SectionObject->Easing.EaseIn.GetObject() && SectionObject->Easing.GetEaseInDuration() > 0)
		{
			int32 EaseInFrames = ConvertFrameTime(SectionObject->Easing.GetEaseInDuration(), TickResolution, DisplayRate).RoundToFrame().Value;
			FText EaseInText = FText::Format(NSLOCTEXT("SequencerSection", "EaseInFormat", "Ease In: {0} ({1} frames)"), SectionObject->Easing.EaseIn->GetDisplayName(), EaseInFrames);
			SectionToolTip = FText::Join(FText::FromString("\n"), SectionToolTip, EaseInText);
		}

		if (SectionObject->Easing.EaseOut.GetObject() && SectionObject->Easing.GetEaseOutDuration() > 0)
		{
			int32 EaseOutFrames = ConvertFrameTime(SectionObject->Easing.GetEaseOutDuration(), TickResolution, DisplayRate).RoundToFrame().Value;
			FText EaseOutText = FText::Format(NSLOCTEXT("SequencerSection", "EaseOutFormat", "Ease Out: {0} ({1} frames)"), SectionObject->Easing.EaseOut->GetDisplayName(), EaseOutFrames);
			SectionToolTip = FText::Join(FText::FromString("\n"), SectionToolTip, EaseOutText);
		}
		
		return SectionToolTip;
	}
	else
	{
		if (SectionToolTipContent.IsEmpty())
		{
			return SectionInterface->GetSectionTitle();
		}
		else
		{
			return FText::Format(NSLOCTEXT("SequencerSection", "TooltipSectionContentFormat", "{0}{1}"), SectionTitleText, SectionToolTipContent);
		}
	}
}

bool SSequencerSection::IsEnabled() const
{
	return !SectionInterface->IsReadOnly();
}

FVector2D SSequencerSection::ComputeDesiredSize(float) const
{
	return FVector2D(0.f, 0.f);
}

void SSequencerSection::ReportParentGeometry(const FGeometry& InParentGeometry)
{
	ParentGeometry = InParentGeometry;
}

FTrackLaneScreenAlignment SSequencerSection::GetAlignment(const FTimeToPixel& InTimeToPixel, const FGeometry& InParentGeometry) const
{
	using namespace UE::MovieScene;
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	if (!SectionModel)
	{
		return FTrackLaneScreenAlignment();
	}

	FTrackLaneVirtualAlignment VirtualAlignment = SectionModel->ArrangeVirtualTrackLaneView();
	FTrackLaneScreenAlignment  ScreenAlignment  = VirtualAlignment.ToScreen(InTimeToPixel, InParentGeometry);

	if (TOptional<FFrameNumber> FiniteLength = VirtualAlignment.GetFiniteLength())
	{
		constexpr float MinSectionWidth = 1.f;

		const float FinalSectionWidth = FMath::Max(MinSectionWidth, ScreenAlignment.WidthPx);
		const float GripOffset        = (FinalSectionWidth - ScreenAlignment.WidthPx) / 2.f;

		ScreenAlignment.LeftPosPx -= GripOffset;
		ScreenAlignment.WidthPx    = FMath::Max(FinalSectionWidth, MinSectionWidth + SectionInterface->GetSectionGripSize() * 2.f);
	}

	return ScreenAlignment;
}

int32 SSequencerSection::GetOverlapPriority() const
{
	if (TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin())
	{
		if (UMovieSceneSection* Section = SectionModel->GetSection())
		{
			return Section->GetOverlapPriority();
		}
	}
	return 0;
}

void SSequencerSection::GetKeysUnderMouse( const FVector2D& MousePosition, const FGeometry& AllottedGeometry, TArray<FSequencerSelectedKey>& OutKeys, float KeyHeightFraction ) const
{
	FGeometry SectionGeometry = MakeSectionGeometryWithoutHandles( AllottedGeometry, SectionInterface );

	UMovieSceneSection& Section = *SectionInterface->GetSectionObject();
	FTimeToPixel TimeToPixelConverter = ConstructTimeConverterForSection(SectionGeometry, Section, GetSequencer());
	const FVector2D MousePixel  = SectionGeometry.AbsoluteToLocal( MousePosition );

	// HitTest 
	const FFrameTime HalfKeySizeFrames = TimeToPixelConverter.PixelDeltaToFrame( SequencerSectionConstants::KeySize.X*.5f );
	const FFrameTime MouseFrameTime    = TimeToPixelConverter.PixelToFrame( MousePixel.X );

	TRange<FFrameNumber> HitTestRange = TRange<FFrameNumber>( (MouseFrameTime-HalfKeySizeFrames).CeilToFrame(), (MouseFrameTime+HalfKeySizeFrames).CeilToFrame());
	
	if (HitTestRange.IsEmpty())
	{
		return;
	}

	// Search every key area until we find the one under the mouse
	for (const FSectionLayoutElement& Element : Layout->GetElements())
	{
		const FGeometry KeyAreaGeometry = Element.ComputeGeometry(AllottedGeometry);
		const FVector2D LocalMousePixel = KeyAreaGeometry.AbsoluteToLocal(MousePosition);
		const float LocalKeyPosY = KeyAreaGeometry.GetLocalSize().Y * .5f;

		// Check that this section is under our mouse, and discard it from potential selection if the mouse is higher than the key's height. We have to
		// check keys on a per-section basis (and not for the overall SectionGeometry) because keys are offset on tracks that have expandable 
		// ranges (ie: Audio, Animation) which otherwise makes them fail the height-threshold check.
		if (!KeyAreaGeometry.IsUnderLocation(MousePosition) || FMath::Abs(LocalKeyPosY - LocalMousePixel.Y) > SequencerSectionConstants::KeySize.Y * KeyHeightFraction)
		{
			continue;
		}

		for (TWeakPtr<FChannelModel> WeakChannel : Element.GetChannels())
		{
			if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
			{
				TArray<FKeyHandle> KeyHandles;
				Channel->GetKeyArea()->GetKeyHandles(KeyHandles, HitTestRange);

				// Only ever select one key from any given key area
				if (KeyHandles.Num())
				{
					OutKeys.Add( FSequencerSelectedKey( Section, Channel, KeyHandles[0] ) );
				}
			}
		}

		// The mouse is in this key area so it cannot possibly be in any other key area
		return;
	}
}


void SSequencerSection::CreateKeysUnderMouse( const FVector2D& MousePosition, const FGeometry& AllottedGeometry, TArrayView<const FSequencerSelectedKey> InPressedKeys, TArray<FSequencerSelectedKey>& OutKeys )
{
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	if (!SectionModel)
	{
		return;
	}

	UMovieSceneSection& Section = *SectionInterface->GetSectionObject();

	if (Section.IsReadOnly())
	{
		return;
	}

	FGeometry SectionGeometry = MakeSectionGeometryWithoutHandles( AllottedGeometry, SectionInterface );
	FTimeToPixel TimeToPixelConverter = ConstructTimeConverterForSection(SectionGeometry, Section, GetSequencer());

	// If the pressed key exists, offset the new key and look for it in the newly laid out key areas
	if (InPressedKeys.Num())
	{
		Section.Modify();

		// Offset by 1 pixel worth of time if possible
		const FFrameTime TimeFuzz = TimeToPixelConverter.PixelDeltaToFrame(1.f);

		for (const FSequencerSelectedKey& PressedKey : InPressedKeys)
		{
			const FFrameNumber CurrentTime = PressedKey.WeakChannel.Pin()->GetKeyArea()->GetKeyTime(PressedKey.KeyHandle);
			const FKeyHandle NewHandle = PressedKey.WeakChannel.Pin()->GetKeyArea()->DuplicateKey(PressedKey.KeyHandle);

			PressedKey.WeakChannel.Pin()->GetKeyArea()->SetKeyTime(NewHandle, CurrentTime + TimeFuzz.FrameNumber);
			OutKeys.Add(FSequencerSelectedKey(Section, PressedKey.WeakChannel, NewHandle));
		}
	}
	else
	{
		TSharedPtr<FTrackModel>             TrackModel             = SectionModel->FindAncestorOfType<FTrackModel>();
		TSharedPtr<IObjectBindingExtension> ObjectBindingExtension = SectionModel->FindAncestorOfType<IObjectBindingExtension>();

		FGuid ObjectBinding = ObjectBindingExtension ? ObjectBindingExtension->GetObjectGuid() : FGuid();

		FVector2D LocalSpaceMousePosition = SectionGeometry.AbsoluteToLocal( MousePosition );
		const FFrameTime CurrentTime = TimeToPixelConverter.PixelToFrame(LocalSpaceMousePosition.X);

		TArray<TSharedRef<IKeyArea>> ValidKeyAreasUnderCursor;

		// Search every key area until we find the one under the mouse
		for (const FSectionLayoutElement& Element : Layout->GetElements())
		{
			// Compute the current key area geometry
			FGeometry KeyAreaGeometryPadded = Element.ComputeGeometry(AllottedGeometry);

			// Is the key area under the mouse
			if( !KeyAreaGeometryPadded.IsUnderLocation( MousePosition ) )
			{
				continue;
			}

			for (TWeakPtr<FChannelModel> WeakChannel : Element.GetChannels())
			{
				if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
				{
					ValidKeyAreasUnderCursor.Add(Channel->GetKeyArea().ToSharedRef());
				}
			}
		}

		FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "CreateKeysUnderMouse", "Create keys under mouse"));
		FAddKeyOperation::FromKeyAreas(TrackModel->GetTrackEditor().Get(), ValidKeyAreasUnderCursor).Commit(CurrentTime.FrameNumber, GetSequencer());

		// Get the keys under the mouse as the newly created keys. Check with the full height of the key track area.
		const float KeyHeightFraction = 1.f;
		GetKeysUnderMouse(MousePosition, AllottedGeometry, OutKeys, KeyHeightFraction);
	}

	if (OutKeys.Num())
	{
		Layout = FSectionLayout(SectionModel);
	}
}


bool SSequencerSection::CheckForEasingHandleInteraction( const FPointerEvent& MouseEvent, const FGeometry& SectionGeometry )
{
	UMovieSceneSection* ThisSection = SectionInterface->GetSectionObject();
	if (!ThisSection)
	{
		return false;
	}

	UMovieSceneTrack* Track = ThisSection->GetTypedOuter<UMovieSceneTrack>();
	if (!Track || Track->GetSupportedBlendTypes().Num() == 0)
	{
		return false;
	}

	FMovieSceneSupportsEasingParams SupportsEasingParams(ThisSection);
	EMovieSceneTrackEasingSupportFlags EasingFlags = Track->SupportsEasing(SupportsEasingParams);
	if (!EnumHasAnyFlags(EasingFlags, EMovieSceneTrackEasingSupportFlags::ManualEasing))
	{
		return false;
	}

	FTimeToPixel TimeToPixelConverter = ConstructTimeConverterForSection(MakeSectionGeometryWithoutHandles(SectionGeometry, SectionInterface), *ThisSection, GetSequencer());

	const double MouseTime = TimeToPixelConverter.PixelToSeconds(SectionGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
	// We intentionally give the handles a little more hit-test area than is visible as they are quite small
	const double HalfHandleSizeX = TimeToPixelConverter.PixelToSeconds(8.f) - TimeToPixelConverter.PixelToSeconds(0.f);

	// Now test individual easing handles if we're at the correct vertical position
	float LocalMouseY = SectionGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).Y;
	if (LocalMouseY < 0.f || LocalMouseY > 5.f)
	{
		return false;
	}

	// Gather all underlapping sections
	TArray<TSharedPtr<FSectionModel>> AllUnderlappingSections;
	AllUnderlappingSections.Add(WeakSectionModel.Pin());
	for (const FOverlappingSections& Segment : UnderlappingSegments)
	{
		for (const TWeakPtr<FSectionModel>& Section : Segment.Sections)
		{
			AllUnderlappingSections.AddUnique(Section.Pin());
		}
	}

	for (const TSharedPtr<FSectionModel>& SectionModel : AllUnderlappingSections)
	{
		UMovieSceneSection* EasingSectionObj = SectionModel->GetSection();

		if (EasingSectionObj->HasStartFrame() && EnumHasAllFlags(EasingFlags, EMovieSceneTrackEasingSupportFlags::ManualEaseIn))
		{
			TRange<FFrameNumber> EaseInRange      = EasingSectionObj->GetEaseInRange();
			double               HandlePositionIn = ( EaseInRange.IsEmpty() ? EasingSectionObj->GetInclusiveStartFrame() : EaseInRange.GetUpperBoundValue() ) / TimeToPixelConverter.GetTickResolution();

			if (FMath::IsNearlyEqual(MouseTime, HandlePositionIn, HalfHandleSizeX))
			{
				GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(MakeShared<FSectionEasingHandleHotspot>(ESequencerEasingType::In, SectionModel, Sequencer));
				return true;
			}
		}

		if (EasingSectionObj->HasEndFrame() && EnumHasAllFlags(EasingFlags, EMovieSceneTrackEasingSupportFlags::ManualEaseOut))
		{
			TRange<FFrameNumber> EaseOutRange      = EasingSectionObj->GetEaseOutRange();
			double               HandlePositionOut = (EaseOutRange.IsEmpty() ? EasingSectionObj->GetExclusiveEndFrame() : EaseOutRange.GetLowerBoundValue() ) / TimeToPixelConverter.GetTickResolution();

			if (FMath::IsNearlyEqual(MouseTime, HandlePositionOut, HalfHandleSizeX))
			{
				GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(MakeShared<FSectionEasingHandleHotspot>(ESequencerEasingType::Out, SectionModel, Sequencer));
				return true;
			}
		}
	}

	return false;
}


bool SSequencerSection::CheckForEdgeInteraction( const FPointerEvent& MouseEvent, const FGeometry& SectionGeometry )
{
	UMovieSceneSection* ThisSection = SectionInterface->GetSectionObject();
	if (!ThisSection)
	{
		return false;
	}

	TArray<TSharedPtr<FSectionModel>> AllUnderlappingSections;
	AllUnderlappingSections.Add(WeakSectionModel.Pin());
	for (const FOverlappingSections& Segment : UnderlappingSegments)
	{
		for (const TWeakPtr<FSectionModel>& Section : Segment.Sections)
		{
			AllUnderlappingSections.AddUnique(Section.Pin());
		}
	}

	FGeometry SectionGeometryWithoutHandles = MakeSectionGeometryWithoutHandles(SectionGeometry, SectionInterface);
	FTimeToPixel TimeToPixelConverter       = ConstructTimeConverterForSection(SectionGeometryWithoutHandles, *ThisSection, GetSequencer());

	for (const TSharedPtr<FSectionModel>& UnderlappingSection : AllUnderlappingSections)
	{
		UMovieSceneSection*           UnderlappingSectionObj       = UnderlappingSection->GetSection();
		TSharedPtr<ISequencerSection> UnderlappingSectionInterface = UnderlappingSection->GetSectionInterface();
		if (!UnderlappingSectionInterface->SectionIsResizable())
		{
			continue;
		}

		const float ThisHandleOffset = UnderlappingSectionObj == ThisSection ? HandleOffsetPx : 0.f;
		FVector2D GripSize( UnderlappingSectionInterface->GetSectionGripSize(), SectionGeometry.Size.Y );

		if (UnderlappingSectionObj->HasStartFrame())
		{
			// Make areas to the left and right of the geometry.  We will use these areas to determine if someone dragged the left or right edge of a section
			FGeometry SectionRectLeft = SectionGeometryWithoutHandles.MakeChild(
				FVector2D( TimeToPixelConverter.FrameToPixel(UnderlappingSectionObj->GetInclusiveStartFrame()) - ThisHandleOffset, 0.f ),
				GripSize
			);

			if( SectionRectLeft.IsUnderLocation( MouseEvent.GetScreenSpacePosition() ) )
			{
				GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(MakeShareable( new FSectionResizeHotspot(FSectionResizeHotspot::Left, UnderlappingSection, Sequencer)) );
				return true;
			}
		}

		if (UnderlappingSectionObj->HasEndFrame())
		{
			FGeometry SectionRectRight = SectionGeometryWithoutHandles.MakeChild(
				FVector2D( TimeToPixelConverter.FrameToPixel(UnderlappingSectionObj->GetExclusiveEndFrame()) - UnderlappingSectionInterface->GetSectionGripSize() + ThisHandleOffset, 0 ), 
				GripSize
			);

			if( SectionRectRight.IsUnderLocation( MouseEvent.GetScreenSpacePosition() ) )
			{
				GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(MakeShareable( new FSectionResizeHotspot(FSectionResizeHotspot::Right, UnderlappingSection, Sequencer)) );
				return true;
			}
		}
	}
	return false;
}

bool SSequencerSection::CheckForEasingAreaInteraction( const FPointerEvent& MouseEvent, const FGeometry& SectionGeometry )
{
	UMovieSceneSection* ThisSection = SectionInterface->GetSectionObject();
	FTimeToPixel TimeToPixelConverter = ConstructTimeConverterForSection(MakeSectionGeometryWithoutHandles(SectionGeometry, SectionInterface), *ThisSection, GetSequencer());
	const FFrameNumber MouseTime = TimeToPixelConverter.PixelToFrame(SectionGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X).FrameNumber;

	// First off, set the hotspot to an easing area if necessary
	for (const FOverlappingSections& Segment : UnderlappingEasingSegments)
	{
		if (!Segment.Range.Contains(MouseTime))
		{
			continue;
		}

		TArray<FEasingAreaHandle> EasingAreas;
		for (const TWeakPtr<FSectionModel>& SectionModel : Segment.Sections)
		{
			UMovieSceneSection* Section = SectionModel.Pin()->GetSection();
			if (Section->GetEaseInRange().Contains(MouseTime))
			{
				EasingAreas.Add(FEasingAreaHandle{ SectionModel, ESequencerEasingType::In });
			}
			if (Section->GetEaseOutRange().Contains(MouseTime))
			{
				EasingAreas.Add(FEasingAreaHandle{ SectionModel, ESequencerEasingType::Out });
			}
		}

		if (EasingAreas.Num())
		{
			GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(MakeShared<FSectionEasingAreaHotspot>(EasingAreas, WeakSectionModel, Sequencer));
			return true;
		}
	}
	return false;
}

FSequencer& SSequencerSection::GetSequencer() const
{
	return *Sequencer.Pin().Get();
}

int32 SSequencerSection::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	UMovieSceneSection* SectionObject = SectionModel ? SectionModel->GetSection() : nullptr;
	if (!SectionModel || !SectionObject)
	{
		return LayerId;
	}

	const ISequencerEditTool* EditTool = GetSequencer().GetViewModel()->GetTrackArea()->GetEditTool();
	TSharedPtr<ITrackAreaHotspot> Hotspot = EditTool ? EditTool->GetDragHotspot() : nullptr;
	if (!Hotspot)
	{
		Hotspot = GetSequencer().GetViewModel()->GetTrackArea()->GetHotspot();
	}

	UMovieSceneTrack* Track = SectionObject->GetTypedOuter<UMovieSceneTrack>();
	const bool bTrackDisabled = Track && (Track->IsEvalDisabled() || Track->IsRowEvalDisabled(SectionObject->GetRowIndex()));
	const bool bEnabled = bParentEnabled && SectionObject->IsActive() && !(bTrackDisabled);
	const bool bLocked = SectionObject->IsLocked() || SectionObject->IsReadOnly();

	bool bSetSectionToKey = false;
    // Only show section to key border if we have more than one section
	if (Track && Track->GetAllSections().Num() > 1 && Track->GetSectionToKey() == SectionObject)
	{
		bSetSectionToKey = true;
	}

	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	FGeometry SectionGeometry = MakeSectionGeometryWithoutHandles( AllottedGeometry, SectionInterface );

	FSequencerSectionPainterImpl Painter(GetSequencer(), SectionModel, OutDrawElements, SectionGeometry, *this);

	FGeometry PaintSpaceParentGeometry = ParentGeometry;
	PaintSpaceParentGeometry.AppendTransform(FSlateLayoutTransform(Inverse(Args.GetWindowToDesktopTransform())));

	Painter.ParentClippingRect = PaintSpaceParentGeometry.GetLayoutBoundingRect();

	// Clip vertically
	Painter.ParentClippingRect.Top = FMath::Max(Painter.ParentClippingRect.Top, MyCullingRect.Top);
	Painter.ParentClippingRect.Bottom = FMath::Min(Painter.ParentClippingRect.Bottom, MyCullingRect.Bottom);

	Painter.SectionClippingRect = Painter.SectionGeometry.GetLayoutBoundingRect().InsetBy(FMargin(1.f)).IntersectionWith(Painter.ParentClippingRect);

	Painter.LayerId = LayerId;
	Painter.bParentEnabled = bEnabled;
	Painter.bIsHighlighted = IsSectionHighlighted(SectionObject, Hotspot);
	if (UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(SectionObject))
	{
		if (( SubSection->GetNetworkMask() & GetSequencer().GetEvaluationTemplate().GetEmulatedNetworkMask() ) == EMovieSceneServerClientMask::None)
		{
			Painter.GhostAlpha = .3f;
		}
	}

	Painter.bIsSelected = GetSequencer().GetSelection().IsSelected(SectionModel);

	for (const FSectionLayoutElement& Element : Layout->GetElements())
	{
		TSharedPtr<FViewModel> Model = Element.GetModel();
		if (FCategoryModel* Category = Model->CastThis<FCategoryModel>())
		{
			TArray<TSharedRef<IKeyArea>> ChildKeyAreas;

			for (TSharedPtr<FChannelModel> Channel : Category->GetDescendantsOfType<FChannelModel>())
			{
				if (TSharedPtr<IKeyArea> KeyArea = Channel->GetKeyArea())
				{
					ChildKeyAreas.Add(KeyArea.ToSharedRef());
				}
			}

			FKeyAreaElement KeyAreaElement;
			KeyAreaElement.KeyAreas = ChildKeyAreas;
			KeyAreaElement.KeyAreaGeometry = Element.ComputeGeometry(AllottedGeometry);
			KeyAreaElement.Type = (FKeyAreaElement::EType)Element.GetType();
			Painter.KeyAreaElements.Add(KeyAreaElement);
		}
		else
		{
			FKeyAreaElement KeyAreaElement;
			for (TWeakPtr<FChannelModel> WeakChannel : Element.GetChannels())
			{
				if (TSharedPtr<FChannelModel> Channel = WeakChannel.Pin())
				{
					KeyAreaElement.KeyAreas.Add(Channel->GetKeyArea().ToSharedRef());
				}
			}
			KeyAreaElement.KeyAreaGeometry = Element.ComputeGeometry(AllottedGeometry);
			KeyAreaElement.Type = (FKeyAreaElement::EType)Element.GetType();
			Painter.KeyAreaElements.Add(KeyAreaElement);
		}
	}

	FSlateClippingZone ClippingZone(Painter.SectionClippingRect);
	OutDrawElements.PushClip(ClippingZone);

	// Ask the interface to draw the section
	LayerId = SectionInterface->OnPaintSection(Painter);

	LayerId = SCompoundWidget::OnPaint( Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled );

	FLinearColor SelectionColor = FAppStyle::GetSlateColor(SequencerSectionConstants::SelectionColorName).GetColor(FWidgetStyle());
	DrawSectionHandles(AllottedGeometry, OutDrawElements, LayerId, DrawEffects, SelectionColor, Hotspot);

	Painter.LayerId = LayerId;
	PaintEasingHandles( Painter, SelectionColor, Hotspot );

	{
		FKeyRendererPaintArgs KeyRenderArgs;
		KeyRenderArgs.KeyThrobValue = GetKeySelectionThrobValue();
		KeyRenderArgs.SectionThrobValue = GetSectionSelectionThrobValue();

		KeyRenderer.Paint(Layout.GetValue(), InWidgetStyle, KeyRenderArgs, &GetSequencer(), Painter);
	}

	LayerId = Painter.LayerId;
	if (bLocked)
	{
		static const FName SelectionBorder("Sequencer.Section.LockedBorder");

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FAppStyle::GetBrush(SelectionBorder),
			DrawEffects,
			FLinearColor::Red
		);
	}
	else if (bSetSectionToKey)
	{
		static const FName SelectionBorder("Sequencer.Section.LockedBorder");

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FAppStyle::GetBrush(SelectionBorder),
			DrawEffects,
			FLinearColor::Green
		);
	}

	// Section name with drop shadow
	FText SectionTitle = SectionInterface->GetSectionTitle();
	FMargin ContentPadding = SectionInterface->GetContentPadding();

	const int32 EaseInAmount = SectionObject->Easing.GetEaseInDuration();
	if (EaseInAmount > 0)
	{
		ContentPadding.Left += Painter.GetTimeConverter().FrameToPixel(EaseInAmount) - Painter.GetTimeConverter().FrameToPixel(0);
	}

	if (!SectionTitle.IsEmpty())
	{
		FVector2D TopLeft = SectionGeometry.AbsoluteToLocal(Painter.SectionClippingRect.GetTopLeft()) + FVector2D(1.f, -1.f);

		FSlateFontInfo FontInfo = FAppStyle::GetFontStyle("NormalFont");

		TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();

		auto GetFontHeight = [&]
		{
			return FontCache->GetMaxCharacterHeight(FontInfo, 1.f) + FontCache->GetBaseline(FontInfo, 1.f);
		};
		while (GetFontHeight() > SectionGeometry.Size.Y && FontInfo.Size > 11)
		{
			FontInfo.Size = FMath::Max(FMath::FloorToInt(FontInfo.Size - 6.f), 11);
		}

		// Drop shadow
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			SectionGeometry.MakeChild(
				FVector2D(SectionGeometry.Size.X, GetFontHeight()),
				FSlateLayoutTransform(TopLeft + FVector2D(ContentPadding.Left, ContentPadding.Top) + FVector2D(1.f, 1.f))
			).ToPaintGeometry(),
			SectionTitle,
			FontInfo,
			DrawEffects,
			FLinearColor(0,0,0,.5f * Painter.GhostAlpha)
		);

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			SectionGeometry.MakeChild(
				FVector2D(SectionGeometry.Size.X, GetFontHeight()),
				FSlateLayoutTransform(TopLeft + FVector2D(ContentPadding.Left, ContentPadding.Top))
			).ToPaintGeometry(),
			SectionTitle,
			FontInfo,
			DrawEffects,
			FColor(200, 200, 200, static_cast<uint8>(Painter.GhostAlpha * 255))
		);
	}

	OutDrawElements.PopClip();
	return LayerId + 1;
}


void SSequencerSection::PaintEasingHandles( FSequencerSectionPainter& InPainter, FLinearColor SelectionColor, TSharedPtr<ITrackAreaHotspot> Hotspot ) const
{
	if (!SectionInterface->GetSectionObject()->GetBlendType().IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FSectionModel>> AllUnderlappingSections;
	if (IsSectionHighlighted(SectionInterface->GetSectionObject(), Hotspot))
	{
		AllUnderlappingSections.Add(WeakSectionModel.Pin());
	}

	for (const FOverlappingSections& Segment : UnderlappingSegments)
	{
		for (const TWeakPtr<FSectionModel>& Section : Segment.Sections)
		{
			UMovieSceneSection* SectionObject = Section.Pin()->GetSection();
			if (IsSectionHighlighted(SectionObject, Hotspot))
			{
				AllUnderlappingSections.AddUnique(Section.Pin());
			}
		}
	}

	FTimeToPixel TimeToPixelConverter = InPainter.GetTimeConverter();
	for (const TSharedPtr<FSectionModel>& SectionModel : AllUnderlappingSections)
	{
		UMovieSceneSection* UnderlappingSectionObj = SectionModel->GetSection();
		if (UnderlappingSectionObj->GetRange() == TRange<FFrameNumber>::All())
		{
			continue;
		}

		bool bDrawThisSectionsHandles = true;
		bool bLeftHandleActive = false;
		bool bRightHandleActive = false;

		// Get the hovered/selected state for the section handles from the hotspot
		if (Hotspot)
		{
			if (const FSectionEasingHandleHotspot* EasingHotspot = Hotspot->CastThis<FSectionEasingHandleHotspot>())
			{
				bDrawThisSectionsHandles = (EasingHotspot->WeakSectionModel.Pin() == SectionModel);
				bLeftHandleActive = EasingHotspot->HandleType == ESequencerEasingType::In;
				bRightHandleActive = EasingHotspot->HandleType == ESequencerEasingType::Out;
			}
			else if (const FSectionEasingAreaHotspot* EasingAreaHotspot = Hotspot->CastThis<FSectionEasingAreaHotspot>())
			{
				for (const FEasingAreaHandle& Easing : EasingAreaHotspot->Easings)
				{
					if (Easing.WeakSectionModel.Pin()->GetSection() == UnderlappingSectionObj)
					{
						if (Easing.EasingType == ESequencerEasingType::In)
						{
							bLeftHandleActive = true;
						}
						else
						{
							bRightHandleActive = true;
						}

						if (bLeftHandleActive && bRightHandleActive)
						{
							break;
						}
					}
				}
			}
		}

		const UMovieSceneTrack* Track = UnderlappingSectionObj->GetTypedOuter<UMovieSceneTrack>();
		FMovieSceneSupportsEasingParams SupportsEasingParams(UnderlappingSectionObj);
		EMovieSceneTrackEasingSupportFlags EasingSupportFlags = Track->SupportsEasing(SupportsEasingParams);

		if (!bDrawThisSectionsHandles || !EnumHasAnyFlags(EasingSupportFlags, EMovieSceneTrackEasingSupportFlags::ManualEasing))
		{
			continue;
		}

		const ESlateDrawEffect DrawEffects = InPainter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		const FSlateBrush* EasingHandle = FAppStyle::GetBrush("Sequencer.Section.EasingHandle");
		FVector2D HandleSize(10.f, 10.f);

		if (UnderlappingSectionObj->HasStartFrame() && EnumHasAllFlags(EasingSupportFlags, EMovieSceneTrackEasingSupportFlags::ManualEaseIn))
		{
			TRange<FFrameNumber> EaseInRange = UnderlappingSectionObj->GetEaseInRange();
			// Always draw handles if the section is highlighted, even if there is no range (to allow manual adjustment)
			FFrameNumber HandleFrame = EaseInRange.IsEmpty() ? UnderlappingSectionObj->GetInclusiveStartFrame() : UE::MovieScene::DiscreteExclusiveUpper(EaseInRange);
			FVector2D HandlePos(TimeToPixelConverter.FrameToPixel(HandleFrame), 0.f);
			FSlateDrawElement::MakeBox(
				InPainter.DrawElements,
				// always draw selected keys on top of other keys
				InPainter.LayerId,
				// Center the key along X.  Ensure the middle of the key is at the actual key time
				InPainter.SectionGeometry.ToPaintGeometry(
					HandlePos - FVector2D(HandleSize.X*0.5f, 0.f),
					HandleSize
				),
				EasingHandle,
				DrawEffects,
				(bLeftHandleActive ? SelectionColor : EasingHandle->GetTint(FWidgetStyle()))
			);
		}

		if (UnderlappingSectionObj->HasEndFrame() && EnumHasAllFlags(EasingSupportFlags, EMovieSceneTrackEasingSupportFlags::ManualEaseOut))
		{
			TRange<FFrameNumber> EaseOutRange = UnderlappingSectionObj->GetEaseOutRange();

			// Always draw handles if the section is highlighted, even if there is no range (to allow manual adjustment)
			FFrameNumber HandleFrame = EaseOutRange.IsEmpty() ? UnderlappingSectionObj->GetExclusiveEndFrame() : UE::MovieScene::DiscreteInclusiveLower(EaseOutRange);
			FVector2D    HandlePos   = FVector2D(TimeToPixelConverter.FrameToPixel(HandleFrame), 0.f);

			FSlateDrawElement::MakeBox(
				InPainter.DrawElements,
				// always draw selected keys on top of other keys
				InPainter.LayerId,
				// Center the key along X.  Ensure the middle of the key is at the actual key time
				InPainter.SectionGeometry.ToPaintGeometry(
					HandlePos - FVector2D(HandleSize.X*0.5f, 0.f),
					HandleSize
				),
				EasingHandle,
				DrawEffects,
				(bRightHandleActive ? SelectionColor : EasingHandle->GetTint(FWidgetStyle()))
			);
		}
	}
}


void SSequencerSection::DrawSectionHandles( const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, ESlateDrawEffect DrawEffects, FLinearColor SelectionColor, TSharedPtr<ITrackAreaHotspot> Hotspot ) const
{
	UMovieSceneSection* ThisSection = SectionInterface->GetSectionObject();
	if (!ThisSection)
	{
		return;
	}

	TOptional<FSlateClippingState> PreviousClipState = OutDrawElements.GetClippingState();
	OutDrawElements.PopClip();

	OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.GetLayoutBoundingRect()));

	TArray<TSharedPtr<FSectionModel>> AllUnderlappingSections;
	if (IsSectionHighlighted(SectionInterface->GetSectionObject(), Hotspot))
	{
		AllUnderlappingSections.Add(WeakSectionModel.Pin());
	}

	for (const FOverlappingSections& Segment : UnderlappingSegments)
	{
		for (const TWeakPtr<FSectionModel>& Section : Segment.Sections)
		{
			UMovieSceneSection* SectionObject = Section.Pin()->GetSection();
			if (IsSectionHighlighted(SectionObject, Hotspot))
			{
				AllUnderlappingSections.AddUnique(Section.Pin());
			}
		}
	}

	FGeometry SectionGeometryWithoutHandles = MakeSectionGeometryWithoutHandles(AllottedGeometry, SectionInterface);
	FTimeToPixel TimeToPixelConverter = ConstructTimeConverterForSection(SectionGeometryWithoutHandles, *ThisSection, GetSequencer());

	for (TSharedPtr<FSectionModel> SectionModel : AllUnderlappingSections)
	{
		UMovieSceneSection*           UnderlappingSectionObj = SectionModel->GetSection();
		TSharedPtr<ISequencerSection> UnderlappingSection    = SectionModel->GetSectionInterface();
		if (!UnderlappingSection->SectionIsResizable() || UnderlappingSectionObj->GetRange() == TRange<FFrameNumber>::All())
		{
			continue;
		}

		bool bDrawThisSectionsHandles = (UnderlappingSectionObj == ThisSection && HandleOffsetPx != 0) || IsSectionHighlighted(UnderlappingSectionObj, Hotspot);
		bool bLeftHandleActive = false;
		bool bRightHandleActive = false;

		// Get the hovered/selected state for the section handles from the hotspot
		if (TSharedPtr<FSectionResizeHotspot> ResizeHotspot = HotspotCast<FSectionResizeHotspot>(Hotspot))
		{
			if (ResizeHotspot->WeakSectionModel.Pin() == SectionModel)
			{
				bDrawThisSectionsHandles = true;
				bLeftHandleActive = ResizeHotspot->HandleType == FSectionResizeHotspot::Left;
				bRightHandleActive = ResizeHotspot->HandleType == FSectionResizeHotspot::Right;
			}
			else
			{
				bDrawThisSectionsHandles = false;
			}
		}

		if (!bDrawThisSectionsHandles)
		{
			continue;
		}

		const float ThisHandleOffset = UnderlappingSectionObj == ThisSection ? HandleOffsetPx : 0.f;
		FVector2D GripSize( UnderlappingSection->GetSectionGripSize(), AllottedGeometry.Size.Y );

		float Opacity = 0.5f;
		if (ThisHandleOffset != 0)
		{
			Opacity = FMath::Clamp(.5f + ThisHandleOffset / GripSize.X * .5f, .5f, 1.f);
		}

		const FSlateBrush* LeftGripBrush = FAppStyle::GetBrush("Sequencer.Section.GripLeft");
		const FSlateBrush* RightGripBrush = FAppStyle::GetBrush("Sequencer.Section.GripRight");

		// Left Grip
		if (UnderlappingSectionObj->HasStartFrame())
		{
			FGeometry SectionRectLeft = SectionGeometryWithoutHandles.MakeChild(
				FVector2D( TimeToPixelConverter.FrameToPixel(UnderlappingSectionObj->GetInclusiveStartFrame()) - ThisHandleOffset, 0.f ),
				GripSize
			);
			FSlateDrawElement::MakeBox
			(
				OutDrawElements,
				LayerId,
				SectionRectLeft.ToPaintGeometry(),
				LeftGripBrush,
				DrawEffects,
				(bLeftHandleActive ? SelectionColor : LeftGripBrush->GetTint(FWidgetStyle())).CopyWithNewOpacity(Opacity)
			);
		}

		// Right Grip
		if (UnderlappingSectionObj->HasEndFrame())
		{
			FGeometry SectionRectRight = SectionGeometryWithoutHandles.MakeChild(
				FVector2D( TimeToPixelConverter.FrameToPixel(UnderlappingSectionObj->GetExclusiveEndFrame()) - UnderlappingSection->GetSectionGripSize() + ThisHandleOffset, 0 ), 
				GripSize
			);
			FSlateDrawElement::MakeBox
			(
				OutDrawElements,
				LayerId,
				SectionRectRight.ToPaintGeometry(),
				RightGripBrush,
				DrawEffects,
				(bRightHandleActive ? SelectionColor : RightGripBrush->GetTint(FWidgetStyle())).CopyWithNewOpacity(Opacity)
			);
		}
	}

	OutDrawElements.PopClip();
	if (PreviousClipState.IsSet())
	{
		OutDrawElements.GetClippingManager().PushClippingState(PreviousClipState.GetValue());
	}
}

void SSequencerSection::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if( GetVisibility() == EVisibility::Visible )
	{
		Layout = FSectionLayout(WeakSectionModel.Pin());

		UMovieSceneSection* Section = SectionInterface->GetSectionObject();
		if (Section && Section->HasStartFrame() && Section->HasEndFrame())
		{
			FTimeToPixel TimeToPixelConverter(ParentGeometry, GetSequencer().GetViewRange(), Section->GetTypedOuter<UMovieScene>()->GetTickResolution());

			const int32 SectionLengthPx = FMath::Max(0,
				FMath::RoundToInt(
					TimeToPixelConverter.FrameToPixel(Section->GetExclusiveEndFrame())) - FMath::RoundToInt(TimeToPixelConverter.FrameToPixel(Section->GetInclusiveStartFrame())
				)
			);

			const float SectionGripSize = SectionInterface->GetSectionGripSize();
			HandleOffsetPx = FMath::Max(FMath::RoundToFloat((2*SectionGripSize - SectionLengthPx) * .5f), 0.f);
		}
		else
		{
			HandleOffsetPx = 0;
		}

		FGeometry SectionGeometry = MakeSectionGeometryWithoutHandles( AllottedGeometry, SectionInterface );
		SectionInterface->Tick(SectionGeometry, ParentGeometry, InCurrentTime, InDeltaTime);
	}
}

void SSequencerSection::AddChildLane(TSharedPtr<ITrackLaneWidget> ChildWidget)
{
	
}

void SSequencerSection::OnModifiedIndirectly(UMovieSceneSignedObject* Object)
{
	if (Object->IsA<UMovieSceneSection>())
	{
		UpdateUnderlappingSegments();
	}
}

FReply SSequencerSection::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	TArrayView<const FSequencerSelectedKey> HoveredKeys;
	
	// The hovered key is defined from the sequencer hotspot
	if (TSharedPtr<FKeyHotspot> KeyHotspot = HotspotCast<FKeyHotspot>(GetSequencer().GetViewModel()->GetTrackArea()->GetHotspot()))
	{
		HoveredKeys = KeyHotspot->Keys;
	}

	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		GEditor->BeginTransaction(NSLOCTEXT("Sequencer", "CreateKey_Transaction", "Create Key"));

		// Generate a key and set it as the PressedKey
		TArray<FSequencerSelectedKey> NewKeys;
		CreateKeysUnderMouse(MouseEvent.GetScreenSpacePosition(), MyGeometry, HoveredKeys, NewKeys);

		if (NewKeys.Num())
		{
			GetSequencer().GetSelection().EmptySelectedKeys();
			for (const FSequencerSelectedKey& NewKey : NewKeys)
			{
				GetSequencer().GetSelection().AddToSelection(NewKey);
			}

			// Pass the event to the tool to copy the hovered key and move it
			GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot( MakeShared<FKeyHotspot>(MoveTemp(NewKeys), Sequencer) );

			// Return unhandled so that the EditTool can handle the mouse down based on the newly created keyframe and prepare to move it
			return FReply::Unhandled();
		}
	}

	return FReply::Unhandled();
}


FGeometry SSequencerSection::MakeSectionGeometryWithoutHandles( const FGeometry& AllottedGeometry, const TSharedPtr<ISequencerSection>& InSectionInterface ) const
{
	return AllottedGeometry.MakeChild(
		AllottedGeometry.GetLocalSize() - FVector2D( HandleOffsetPx*2, 0.0f ),
		FSlateLayoutTransform(FVector2D(HandleOffsetPx, 0 ))
	);
}

void SSequencerSection::UpdateUnderlappingSegments()
{
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	if (SectionModel)
	{
		UnderlappingSegments = SectionModel->GetUnderlappingSections();
		UnderlappingEasingSegments = SectionModel->GetEasingSegments();
	}
	else
	{
		UnderlappingSegments.Reset();
		UnderlappingEasingSegments.Reset();
	}
}

FReply SSequencerSection::OnMouseButtonDoubleClick( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	if (!SectionModel)
	{
		return FReply::Handled();
	}

	if( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		TArray<FSequencerSelectedKey> Keys;
		GetKeysUnderMouse(MouseEvent.GetScreenSpacePosition(), MyGeometry, Keys);
		TArray<FKeyHandle> KeyHandles;
		for (FSequencerSelectedKey Key : Keys)
		{
			KeyHandles.Add(Key.KeyHandle);
		}
		if (KeyHandles.Num() > 0)
		{
			return SectionInterface->OnKeyDoubleClicked(KeyHandles);
		}

		FReply Reply = SectionInterface->OnSectionDoubleClicked( MyGeometry, MouseEvent );
		if (!Reply.IsEventHandled())
		{
			// Find the object binding this node is underneath
			FGuid ObjectBinding;
			if (TSharedPtr<IObjectBindingExtension> ObjectBindingExtension = SectionModel->FindAncestorOfType<IObjectBindingExtension>())
			{
				ObjectBinding = ObjectBindingExtension->GetObjectGuid();
			}

			Reply = SectionInterface->OnSectionDoubleClicked(MyGeometry, MouseEvent, ObjectBinding);
		}

		if (Reply.IsEventHandled())
		{
			return Reply;
		}

		GetSequencer().ZoomToFit();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}


FReply SSequencerSection::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	// Checked for hovered key
	TArray<FSequencerSelectedKey> KeysUnderMouse;
	GetKeysUnderMouse( MouseEvent.GetScreenSpacePosition(), MyGeometry, KeysUnderMouse );
	if ( KeysUnderMouse.Num() )
	{
		GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot( MakeShared<FKeyHotspot>( MoveTemp(KeysUnderMouse), Sequencer ) );
	}
	// Check other interaction points in order of importance
	else if (
		!CheckForEasingHandleInteraction(MouseEvent, MyGeometry) &&
		!CheckForEdgeInteraction(MouseEvent, MyGeometry) &&
		!CheckForEasingAreaInteraction(MouseEvent, MyGeometry))
	{
		// If nothing was hit, we just hit the section
		GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot( MakeShareable( new FSectionHotspot(WeakSectionModel, Sequencer)) );
	}

	return FReply::Unhandled();
}
	
FReply SSequencerSection::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		// Snap keys on mouse up since we want to create keys at the exact mouse position (ie. to keep the newly created keys under the mouse 
		// while dragging) but obey snapping rules if necessary
		if (GetSequencer().GetSequencerSettings()->GetIsSnapEnabled() && GetSequencer().GetSequencerSettings()->GetSnapKeyTimesToInterval())
		{
			GetSequencer().SnapToFrame();

			for (const FSequencerSelectedKey& SelectedKey : GetSequencer().GetSelection().GetSelectedKeys())
			{
				const FFrameNumber CurrentTime = SelectedKey.WeakChannel.Pin()->GetKeyArea()->GetKeyTime(SelectedKey.KeyHandle);
				GetSequencer().SetLocalTime(CurrentTime, ESnapTimeMode::STM_Interval);
				break;
			}
		}
		GEditor->EndTransaction();

		// Return unhandled so that the EditTool can handle the mouse up based on the newly created keyframe and finish moving it
		return FReply::Unhandled();
	}
	return FReply::Unhandled();
}

void SSequencerSection::OnMouseLeave( const FPointerEvent& MouseEvent )
{
	SCompoundWidget::OnMouseLeave( MouseEvent );
	GetSequencer().GetViewModel()->GetTrackArea()->SetHotspot(nullptr);
}

static float SectionThrobDurationSeconds = 1.f;
void SSequencerSection::ThrobSectionSelection(int32 ThrobCount)
{
	SectionSelectionThrobEndTime = FPlatformTime::Seconds() + ThrobCount*SectionThrobDurationSeconds;
}

static float KeyThrobDurationSeconds = .5f;
void SSequencerSection::ThrobKeySelection(int32 ThrobCount)
{
	KeySelectionThrobEndTime = FPlatformTime::Seconds() + ThrobCount*KeyThrobDurationSeconds;
}

float EvaluateThrob(float Alpha)
{
	return .5f - FMath::Cos(FMath::Pow(Alpha, 0.5f) * 2 * PI) * .5f;
}

float SSequencerSection::GetSectionSelectionThrobValue()
{
	double CurrentTime = FPlatformTime::Seconds();

	if (SectionSelectionThrobEndTime > CurrentTime)
	{
		float Difference = SectionSelectionThrobEndTime - CurrentTime;
		return EvaluateThrob(1.f - FMath::Fmod(Difference, SectionThrobDurationSeconds));
	}

	return 0.f;
}

float SSequencerSection::GetKeySelectionThrobValue()
{
	double CurrentTime = FPlatformTime::Seconds();

	if (KeySelectionThrobEndTime > CurrentTime)
	{
		float Difference = KeySelectionThrobEndTime - CurrentTime;
		return EvaluateThrob(1.f - FMath::Fmod(Difference, KeyThrobDurationSeconds));
	}

	return 0.f;
}

bool SSequencerSection::IsSectionHighlighted(UMovieSceneSection* InSection, TSharedPtr<ITrackAreaHotspot> Hotspot)
{
	if (!Hotspot)
	{
		return false;
	}

	if (FKeyHotspot* KeyHotspot = Hotspot->CastThis<FKeyHotspot>())
	{
		return KeyHotspot->Keys.ContainsByPredicate([InSection](const FSequencerSelectedKey& Key){ return Key.Section == InSection; });
	}
	else if (FSectionEasingAreaHotspot* EasingAreaHotspot = Hotspot->CastThis<FSectionEasingAreaHotspot>())
	{
		return EasingAreaHotspot->Contains(InSection);
	}
	else if (FSectionHotspotBase* SectionHotspot = Hotspot->CastThis<FSectionHotspotBase>())
	{
		if (TSharedPtr<FSectionModel> SectionModel = SectionHotspot->WeakSectionModel.Pin())
		{
			return SectionModel->GetSection() == InSection;
		}
	}

	return false;
}

} // namespace Sequencer
} // namespace UE
