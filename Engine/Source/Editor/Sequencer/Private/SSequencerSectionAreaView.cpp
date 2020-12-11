// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSequencerSectionAreaView.h"
#include "Sequencer.h"
#include "Types/PaintArgs.h"
#include "Layout/ArrangedChildren.h"
#include "CommonMovieSceneTools.h"
#include "MovieSceneTrack.h"
#include "MovieScene.h"

namespace SequencerSectionAreaConstants
{

	/** Background color of section areas */
	const FLinearColor BackgroundColor( .1f, .1f, .1f, 0.5f );
}

namespace SequencerSectionUtils
{
	/**
	 * Gets the geometry of a section, optionally inflated by some margin
	 *
	 * @param AllottedGeometry		The geometry of the area where sections are located
	 * @param NodeHeight			The height of the section area (and its children)
	 * @param SectionInterface		Interface to the section to get geometry for
	 * @param TimeToPixelConverter  Converts time to pixels and vice versa
	 */
	FGeometry GetSectionGeometry( const FGeometry& AllottedGeometry, int32 RowIndex, int32 MaxTracks, float NodeHeight, TSharedPtr<ISequencerSection> SectionInterface, const FTimeToPixel& TimeToPixelConverter )
	{
		const UMovieSceneSection* Section    = SectionInterface->GetSectionObject();
		const FFrameRate          Resolution = Section->GetTypedOuter<UMovieScene>()->GetTickResolution();

		// Sections with an infite (open) start bound use the currently visible geometry and time range
		float PixelStartX = Section->HasStartFrame() ? TimeToPixelConverter.FrameToPixel( Section->GetInclusiveStartFrame() )     : AllottedGeometry.Position.X;
		// Note the -1 pixel at the end is because the section does not actually end at the end time if there is a section starting at that same time.  It is more important that a section lines up correctly with it's true start time
		float PixelEndX   = Section->HasEndFrame()   ? TimeToPixelConverter.FrameToPixel( Section->GetExclusiveEndFrame() ) : AllottedGeometry.Position.X + AllottedGeometry.GetLocalSize().X;

		const float MinSectionWidth = 1.f;
		float SectionLength = FMath::Max(MinSectionWidth, PixelEndX-PixelStartX);

		float GripOffset = 0;
		if (Section->HasStartFrame() && Section->HasEndFrame())
		{
			float NewSectionLength = FMath::Max(SectionLength, MinSectionWidth + SectionInterface->GetSectionGripSize() * 2.f);

			GripOffset = (NewSectionLength - SectionLength) / 2.f;
			SectionLength = NewSectionLength;
		}

		float ActualHeight = NodeHeight / MaxTracks;

		// Compute allotted geometry area that can be used to draw the section
		return AllottedGeometry.MakeChild(
			FVector2D( PixelStartX-GripOffset, ActualHeight * RowIndex ),
			FVector2D( SectionLength, ActualHeight ) );
	}

}


void SSequencerSectionAreaView::Construct( const FArguments& InArgs, TSharedRef<FSequencerDisplayNode> Node )
{
	ViewRange = InArgs._ViewRange;

	check( Node->GetType() == ESequencerNode::Track );
	SectionAreaNode = StaticCastSharedRef<FSequencerTrackNode>( Node );

	// Generate widgets for sections in this view
	GenerateSectionWidgets();
}

FVector2D SSequencerSectionAreaView::ComputeDesiredSize(float) const
{
	// Note: X Size is not used
	FVector2D Size(100, 0.f);
	if (Children.Num())
	{
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			Size.Y = FMath::Max(Size.Y, Children[Index]->GetDesiredSize().Y);
		}
	}
	else
	{
		Size.Y = SectionAreaNode->GetNodeHeight();
	}
	return Size;
}

void SSequencerSectionAreaView::GenerateSectionWidgets()
{
	Children.Empty();

	if( SectionAreaNode.IsValid() )
	{
		TArray< TSharedRef<ISequencerSection> >& Sections = SectionAreaNode->GetSections();

		for ( int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex )
		{
			Children.Add( 
				SNew( SSequencerSection, SectionAreaNode.ToSharedRef(), SectionIndex ) 
				.Visibility( this, &SSequencerSectionAreaView::GetSectionVisibility, Sections[SectionIndex]->GetSectionObject() )
				.IsEnabled(this, &SSequencerSectionAreaView::GetSectionEnabled, Sections[SectionIndex])
				.ToolTipText(this, &SSequencerSectionAreaView::GetSectionToolTip, Sections[SectionIndex]));
		}
	}
}

EVisibility SSequencerSectionAreaView::GetSectionVisibility(UMovieSceneSection* SectionObject) const
{
	return EVisibility::Visible;
}

bool SSequencerSectionAreaView::GetSectionEnabled(TSharedRef<ISequencerSection> InSequencerSection) const
{
	return !InSequencerSection->IsReadOnly();
}


FText SSequencerSectionAreaView::GetSectionToolTip(TSharedRef<ISequencerSection> InSequencerSection) const
{
	const UMovieSceneSection* SectionObject = InSequencerSection->GetSectionObject();
	const UMovieScene* MovieScene = SectionObject ? SectionObject->GetTypedOuter<UMovieScene>() : nullptr;

	// Optional section specific content to add to tooltip
	FText SectionToolTipContent = InSequencerSection->GetSectionToolTip();

	FText SectionTitleText = InSequencerSection->GetSectionTitle();
	if (!SectionTitleText.IsEmpty())
	{
		SectionTitleText = FText::Format(FText::FromString(TEXT("{0}\n")), SectionTitleText);
	}

	// If the objects are valid and the section is not unbounded, add frame information to the tooltip
	if (SectionObject && MovieScene && SectionObject->HasStartFrame() && SectionObject->HasEndFrame())
	{
		int32 StartFrame = ConvertFrameTime(SectionObject->GetInclusiveStartFrame(), MovieScene->GetTickResolution(), MovieScene->GetDisplayRate()).RoundToFrame().Value;
		int32 EndFrame = ConvertFrameTime(SectionObject->GetExclusiveEndFrame(), MovieScene->GetTickResolution(), MovieScene->GetDisplayRate()).RoundToFrame().Value;
	
		if (SectionToolTipContent.IsEmpty())
		{
			return FText::Format(NSLOCTEXT("SequencerSection", "TooltipFormat", "{0}{1} - {2} ({3} frames)"), SectionTitleText,
				StartFrame,
				EndFrame,
				EndFrame - StartFrame);
		}
		else
		{
			return FText::Format(NSLOCTEXT("SequencerSection", "TooltipFormatWithSectionContent", "{0}{1} - {2} ({3} frames)\n{4}"), SectionTitleText,
				StartFrame,
				EndFrame,
				EndFrame - StartFrame,
				SectionToolTipContent);
		}
	}
	else
	{
		if (SectionToolTipContent.IsEmpty())
		{
			return InSequencerSection->GetSectionTitle();
		}
		else
		{
			return FText::Format(NSLOCTEXT("SequencerSection", "TooltipSectionContentFormat", "{0}{1}"), SectionTitleText, SectionToolTipContent);
		}
	}
}


/** SWidget Interface */
int32 SSequencerSectionAreaView::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, ArrangedChildren);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];
		FSlateRect ChildClipRect = MyCullingRect.IntersectionWith( CurWidget.Geometry.GetLayoutBoundingRect() );
		LayerId = CurWidget.Widget->Paint( Args.WithNewParent(this), CurWidget.Geometry, ChildClipRect, OutDrawElements, LayerId, InWidgetStyle, ShouldBeEnabled( bParentEnabled ) );
	}

	return LayerId + 1;
}

void SSequencerSectionAreaView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (Children.Num())
	{
		StableSort(&Children[0], Children.Num(), [](const TSharedRef<SSequencerSection>& A, const TSharedRef<SSequencerSection>& B){
			UMovieSceneSection* SectionA = A->GetSectionInterface()->GetSectionObject();
			UMovieSceneSection* SectionB = B->GetSectionInterface()->GetSectionObject();

			return SectionA && SectionB && SectionA->GetOverlapPriority() < SectionB->GetOverlapPriority();
		});

		for( int32 WidgetIndex = 0; WidgetIndex < Children.Num(); ++WidgetIndex )
		{
			Children[WidgetIndex]->CacheParentGeometry(AllottedGeometry);
		}
	}
}

void SSequencerSectionAreaView::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	int32 MaxRowIndex = 0;
	if (SectionAreaNode->GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::None)
	{
		for (int32 WidgetIndex = 0; WidgetIndex < Children.Num(); ++WidgetIndex)
		{
			const TSharedRef<SSequencerSection>& Widget = Children[WidgetIndex];

			TSharedPtr<ISequencerSection> SectionInterface = Widget->GetSectionInterface();

			if (SectionInterface->GetSectionObject())
			{
				MaxRowIndex = FMath::Max(MaxRowIndex, SectionInterface->GetSectionObject()->GetRowIndex());
			}
		}
	}
	int32 MaxTracks = MaxRowIndex + 1;

	FTimeToPixel TimeToPixelConverter = GetTimeToPixel( AllottedGeometry );

	for( int32 WidgetIndex = 0; WidgetIndex < Children.Num(); ++WidgetIndex )
	{
		const TSharedRef<SSequencerSection>& Widget = Children[WidgetIndex];

		TSharedPtr<ISequencerSection> SectionInterface = Widget->GetSectionInterface();

		if (!SectionInterface->GetSectionObject())
		{
			continue;
		}

		int32 RowIndex = SectionAreaNode->GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::None ? SectionInterface->GetSectionObject()->GetRowIndex() : 0;

		EVisibility WidgetVisibility = Widget->GetVisibility();
		if( ArrangedChildren.Accepts( WidgetVisibility ) )
		{
			FGeometry SectionGeometry = SequencerSectionUtils::GetSectionGeometry( AllottedGeometry, RowIndex, MaxTracks, Widget->GetDesiredSize().Y, SectionInterface, TimeToPixelConverter );
			
			ArrangedChildren.AddWidget( 
				WidgetVisibility, 
				AllottedGeometry.MakeChild( Widget, SectionGeometry.Position, SectionGeometry.GetLocalSize() )
				);
		}
	}
}

FTimeToPixel SSequencerSectionAreaView::GetTimeToPixel( const FGeometry& AllottedGeometry ) const
{
	const UMovieSceneTrack* Track      = SectionAreaNode->GetTrack();
	if (!Track)
	{
		return FTimeToPixel(AllottedGeometry, ViewRange.Get(), FFrameRate());
	}

	const FFrameRate        Resolution = Track->GetTypedOuter<UMovieScene>()->GetTickResolution();

	return FTimeToPixel( AllottedGeometry, ViewRange.Get(), Resolution );
}


FSequencer& SSequencerSectionAreaView::GetSequencer() const
{
	return SectionAreaNode->GetSequencer();
}
