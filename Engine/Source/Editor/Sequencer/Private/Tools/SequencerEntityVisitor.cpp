// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tools/SequencerEntityVisitor.h"
#include "IKeyArea.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTimeHelpers.h"

FSequencerEntityRange::FSequencerEntityRange(const TRange<double>& InRange, FFrameRate InTickResolution)
	: TickResolution(InTickResolution)
	, Range(InRange)
{
}

FSequencerEntityRange::FSequencerEntityRange(FVector2D TopLeft, FVector2D BottomRight, FFrameRate InTickResolution)
	: TickResolution(InTickResolution)
	, Range(TopLeft.X, BottomRight.X)
	, VerticalTop(TopLeft.Y), VerticalBottom(BottomRight.Y)
{
}

bool FSequencerEntityRange::IntersectSection(const UMovieSceneSection* InSection) const
{
	// Test horizontal bounds
	return (InSection->GetRange() / TickResolution).Overlaps(Range);
}

bool FSequencerEntityRange::IntersectNode(TSharedRef<FSequencerDisplayNode> InNode) const
{
	if (VerticalTop.IsSet())
	{
		return InNode->GetVirtualTop() <= VerticalBottom.GetValue() && InNode->GetVirtualBottom() >= VerticalTop.GetValue();
	}
	return true;
}

bool FSequencerEntityRange::IntersectKeyArea(TSharedRef<FSequencerDisplayNode> InNode, float VirtualKeyHeight) const
{
	if (VerticalTop.IsSet())
	{
		const float NodeCenter = InNode->GetVirtualTop() + (InNode->GetVirtualBottom() - InNode->GetVirtualTop())/2;
		return NodeCenter + VirtualKeyHeight/2 > VerticalTop.GetValue() && NodeCenter - VirtualKeyHeight/2 < VerticalBottom.GetValue();
	}
	return true;
}

FSequencerEntityWalker::FSequencerEntityWalker(const FSequencerEntityRange& InRange, FVector2D InVirtualKeySize)
	: Range(InRange), VirtualKeySize(InVirtualKeySize)
{}

/* @todo: Could probably optimize this by not walking every single node, and binary searching the begin<->end ranges instead */
void FSequencerEntityWalker::Traverse(const ISequencerEntityVisitor& Visitor, const TArray< TSharedRef<FSequencerDisplayNode> >& Nodes)
{
	for (const TSharedRef<FSequencerDisplayNode>& Child : Nodes)
	{
		if (!Child->IsHidden())
		{
			ConditionallyIntersectNode(Visitor, Child);
		}
	}
}

void FSequencerEntityWalker::ConditionallyIntersectNode(const ISequencerEntityVisitor& Visitor, const TSharedRef<FSequencerDisplayNode>& InNode)
{
	if (Range.IntersectNode(InNode))
	{
		// Visit sections within this track
		if (InNode->GetType() == ESequencerNode::Track && Visitor.CheckEntityMask(ESequencerEntity::Section))
		{
			TSharedRef<FSequencerTrackNode> TrackNode = StaticCastSharedRef<FSequencerTrackNode>(InNode);

			for (TSharedRef<ISequencerSection> SectionInterface : TrackNode->GetSections())
			{
				UMovieSceneSection* Section = SectionInterface->GetSectionObject();
				if (Range.IntersectSection(Section))
				{
					Visitor.VisitSection(Section, InNode);
				}
			}
		}

		if (Range.IntersectKeyArea(InNode, VirtualKeySize.Y))
		{
			VisitKeyAnyAreas(Visitor, InNode);
		}
	}

	// Iterate into expanded nodes
	if (InNode->IsExpanded())
	{
		for (const TSharedRef<FSequencerDisplayNode>& Child : InNode->GetChildNodes())
		{
			// Do not visit nodes that are currently filtered out
			if (!Child->IsHidden())
			{
				ConditionallyIntersectNode(Visitor, Child);
			}
		}
	}
}

void FSequencerEntityWalker::VisitKeyAnyAreas(const ISequencerEntityVisitor& Visitor, const TSharedRef<FSequencerDisplayNode>& InNode)
{
	if (!Visitor.CheckEntityMask(ESequencerEntity::Key))
	{
		return;
	}

	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode;
	if (InNode->GetType() == ESequencerNode::KeyArea)
	{
		KeyAreaNode = StaticCastSharedRef<FSequencerSectionKeyAreaNode>(InNode);
	}
	else if (InNode->GetType() == ESequencerNode::Track)
	{
		KeyAreaNode = StaticCastSharedRef<FSequencerTrackNode>(InNode)->GetTopLevelKeyNode();
	}

	// If this node has or is a key area, visit all the keys on the track
	if (KeyAreaNode)
	{
		for (TSharedRef<IKeyArea> KeyArea : KeyAreaNode->GetAllKeyAreas())
		{
			UMovieSceneSection* Section = KeyArea->GetOwningSection();
			if (Section)
			{
				VisitKeyArea(Visitor, KeyArea, Section, InNode);
			}
		}
	}
	// Otherwise it might be a collapsed node that contains key areas as children. If so we visit them as if they were a part of this track so that key groupings are visited properly.
	else if (!InNode->IsExpanded())
	{
		for (TSharedRef<FSequencerDisplayNode> ChildNode : InNode->GetChildNodes())
		{
			VisitKeyAnyAreas(Visitor, ChildNode);
		}
	}
}

void FSequencerEntityWalker::VisitKeyArea(const ISequencerEntityVisitor& Visitor, const TSharedRef<IKeyArea>& KeyArea, UMovieSceneSection* Section, const TSharedRef<FSequencerDisplayNode>& InNode)
{
	TArray<FKeyHandle> Handles;
	TArray<FFrameNumber> Times;

	const FFrameTime HalfKeySizeFrames   = (VirtualKeySize.X*.5f) * Range.TickResolution;
	const FFrameTime RangeStartFrame     = Range.Range.GetLowerBoundValue() * Range.TickResolution;
	const FFrameTime RangeEndFrame       = Range.Range.GetUpperBoundValue() * Range.TickResolution;

	TRange<FFrameNumber> VisitRangeFrames( (RangeStartFrame-HalfKeySizeFrames).CeilToFrame(), (RangeEndFrame+HalfKeySizeFrames).FloorToFrame() );

	VisitRangeFrames = TRange<FFrameNumber>::Intersection(Section->GetRange(), VisitRangeFrames);

	KeyArea->GetKeyInfo(&Handles, &Times, VisitRangeFrames);

	for (int32 Index = 0; Index < Times.Num(); ++Index)
	{
		Visitor.VisitKey(Handles[Index], Times[Index], KeyArea, Section, InNode);
	}
}
