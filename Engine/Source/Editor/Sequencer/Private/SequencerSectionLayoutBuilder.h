// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "ISectionLayoutBuilder.h"

class FSequencerSectionLayoutBuilder
	: public ISectionLayoutBuilder
{
public:
	FSequencerSectionLayoutBuilder(TSharedRef<FSequencerTrackNode> InRootTrackNode, TSharedRef<ISequencerSection> InSection);

public:

	// ISectionLayoutBuilder interface

	virtual void PushCategory( FName CategoryName, const FText& DisplayLabel ) override;
	virtual void SetTopLevelChannel( const FMovieSceneChannelHandle& Channel ) override;
	virtual void AddChannel( const FMovieSceneChannelHandle& Channel ) override;
	virtual void PopCategory() override;

	/** Check whether this section layout builder has been given any layout or not */
	bool HasAnyLayout() const
	{
		return bHasAnyLayout;
	}

private:

	void AddOrUpdateChannel(TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode, const FMovieSceneChannelHandle& Channel);

	/** Root node of the tree */
	TSharedRef<FSequencerTrackNode> RootNode;

	/** The current node that other nodes are added to */
	TSharedRef<FSequencerDisplayNode> CurrentNode;

	/** The section that we are building a layout for */
	TSharedRef<ISequencerSection> Section;

	/** Boolean indicating whether this section layout builder has been given any layout or not */
	bool bHasAnyLayout;

	/** Stack of insertion indices for the current category level that define what child index the next node should be added as. */
	TArray<int32, TInlineAllocator<1>> InsertIndexStack;
};
