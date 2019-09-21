// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/KeyHandle.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "ISequencerSection.h"

class FSequencerTrackNode;
class IKeyArea;

/** Enum of different types of entities that are available in the sequencer */
namespace ESequencerEntity
{
	enum Type
	{
		Key			= 1<<0,
		Section		= 1<<1,
	};

	static const uint32 Everything = (uint32)-1;
}

/** Visitor class used to handle specific sequencer entities */
struct ISequencerEntityVisitor
{
	ISequencerEntityVisitor(uint32 InEntityMask = ESequencerEntity::Everything) : EntityMask(InEntityMask) {}

	virtual void VisitKey(FKeyHandle KeyHandle, FFrameNumber KeyTime, const TSharedPtr<IKeyArea>& KeyArea, UMovieSceneSection* Section, TSharedRef<FSequencerDisplayNode>) const { }
	virtual void VisitSection(UMovieSceneSection* Section, TSharedRef<FSequencerDisplayNode>) const { }
	
	/** Check if the specified type of entity is applicable to this visitor */
	bool CheckEntityMask(ESequencerEntity::Type Type) const { return (EntityMask & Type) != 0; }

protected:
	virtual ~ISequencerEntityVisitor() { }

	/** Bitmask of allowable entities */
	uint32 EntityMask;
};

/** A range specifying time (and possibly vertical) bounds in the sequencer */
struct FSequencerEntityRange
{
	FSequencerEntityRange(const TRange<double>& InRange, FFrameRate InTickResolution);
	FSequencerEntityRange(FVector2D TopLeft, FVector2D BottomRight, FFrameRate InTickResolution);

	/** Check whether the specified section intersects the horizontal range */
	bool IntersectSection(const UMovieSceneSection* InSection) const;

	/** Check whether the specified node intersects the vertical range */
	bool IntersectNode(TSharedRef<FSequencerDisplayNode> InNode) const;

	/** Check whether the specified node's key area intersects this range */
	bool IntersectKeyArea(TSharedRef<FSequencerDisplayNode> InNode, float VirtualKeyHeight) const;

	/** tick resolution of the current time-base */
	FFrameRate TickResolution;

	/** Start/end times */
	TRange<double> Range;

	/** Optional vertical bounds */
	TOptional<float> VerticalTop, VerticalBottom;
};

/** Struct used to iterate a two dimensional *visible* range with a user-supplied visitor */
struct FSequencerEntityWalker
{
	/** Construction from the range itself, and an optional virtual key size, where key bounds must be taken into consideration */
	FSequencerEntityWalker(const FSequencerEntityRange& InRange, FVector2D InVirtualKeySize);

	/** Visit the specified nodes (recursively) with this range and a user-supplied visitor */
	void Traverse(const ISequencerEntityVisitor& Visitor, const TArray< TSharedRef<FSequencerDisplayNode> >& Nodes);

private:

	/** Check whether the specified node intersects the range's vertical space, and visit any keys within it if so */
	void ConditionallyIntersectNode(const ISequencerEntityVisitor& Visitor, const TSharedRef<FSequencerDisplayNode>& InNode);
	/** Visit any keys within the specified track node that overlap the range's horizontal space */
	void VisitTrackNode(const ISequencerEntityVisitor& Visitor, const TSharedRef<FSequencerTrackNode>& InNode);
	/** Visit any keys within any key area nodes that belong to the specified node that overlap the range's horizontal space */
	void VisitKeyAnyAreas(const ISequencerEntityVisitor& Visitor, const TSharedRef<FSequencerDisplayNode>& InNode, bool bAnyParentCollapsed);
	/** Visit any keys within the specified key area that overlap the range's horizontal space */
	void VisitKeyArea(const ISequencerEntityVisitor& Visitor, const TSharedRef<IKeyArea>& KeyArea, UMovieSceneSection* Section, const TSharedRef<FSequencerDisplayNode>& InNode);

	/** The bounds of the range */
	FSequencerEntityRange Range;

	/** Key size in virtual space */
	FVector2D VirtualKeySize;
};
