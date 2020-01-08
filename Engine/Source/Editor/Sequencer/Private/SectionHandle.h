// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class ISequencerSection;
class UMovieSceneSection;
class FSequencerTrackNode;

/**
 * Structure used to encapsulate a specific section on a track node.
 * Authoritative mapping is stored in FSequencerNodeTree. FSectionHandle
 * should not be held persistently except in contexts that are forcibly destroyed when the tree is refreshed.
 */
struct FSectionHandle
{
	/**
	 * Construction from a track node, and the index of the section within it
	 */
	FSectionHandle(TSharedRef<FSequencerTrackNode> InTrackNode, int32 InSectionIndex)
		: SectionIndex(InSectionIndex), TrackNode(MoveTemp(InTrackNode))
	{ }

	/**
	 * Compare 2 handles for equality
	 */
	friend bool operator==(const FSectionHandle& A, const FSectionHandle& B)
	{
		return A.SectionIndex == B.SectionIndex && A.TrackNode == B.TrackNode;
	}

	/**
	 * Retrieve the Sequencer section interface implementation from this handle
	 */
	TSharedRef<ISequencerSection> GetSectionInterface() const;

	/**
	 * Retrieve the actual section that this handle represents
	 */
	UMovieSceneSection* GetSectionObject() const;

	/**
	 * Access the track node that this section currently lives within (could be a sub track node)
	 */
	TSharedRef<FSequencerTrackNode> GetTrackNode() const
	{
		return TrackNode;
	}

	/**
	 * Get the index of this section within its FSequencerTrackNode::GetSections array
	 */
	int32 GetSectionIndex() const
	{
		return SectionIndex;
	}

private:

	int32 SectionIndex;
	TSharedRef<FSequencerTrackNode> TrackNode;
};