// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerSelectionPreview.h"
#include "MovieSceneSection.h"

void FSequencerSelectionPreview::SetSelectionState(FSequencerSelectedKey Key, ESelectionPreviewState InState)
{
	if (InState == ESelectionPreviewState::Undefined)
	{
		DefinedKeyStates.Remove(Key);
	}
	else
	{
		DefinedKeyStates.Add(Key, InState);
	}

	CachedSelectionHash.Reset();
}

void FSequencerSelectionPreview::SetSelectionState(UMovieSceneSection* Section, ESelectionPreviewState InState)
{
	if (InState == ESelectionPreviewState::Undefined)
	{
		DefinedSectionStates.Remove(Section);
	}
	else
	{
		DefinedSectionStates.Add(Section, InState);
	}

	CachedSelectionHash.Reset();
}

void FSequencerSelectionPreview::SetSelectionState(TSharedRef<FSequencerDisplayNode> OutlinerNode, ESelectionPreviewState InState)
{
	if (InState == ESelectionPreviewState::Undefined)
	{
		DefinedOutlinerNodeStates.Remove(OutlinerNode);
	}
	else
	{
		DefinedOutlinerNodeStates.Add(OutlinerNode, InState);
	}

	CachedSelectionHash.Reset();
}

ESelectionPreviewState FSequencerSelectionPreview::GetSelectionState(const FSequencerSelectedKey& Key) const
{
	if (auto* State = DefinedKeyStates.Find(Key))
	{
		return *State;
	}
	return ESelectionPreviewState::Undefined;
}

ESelectionPreviewState FSequencerSelectionPreview::GetSelectionState(UMovieSceneSection* Section) const
{
	if (auto* State = DefinedSectionStates.Find(Section))
	{
		return *State;
	}
	return ESelectionPreviewState::Undefined;
}

ESelectionPreviewState FSequencerSelectionPreview::GetSelectionState(TSharedRef<FSequencerDisplayNode> OutlinerNode) const
{
	if (auto* State = DefinedOutlinerNodeStates.Find(OutlinerNode))
	{
		return *State;
	}
	return ESelectionPreviewState::Undefined;
}

void FSequencerSelectionPreview::Empty()
{
	EmptyDefinedKeyStates();
	EmptyDefinedSectionStates();
	EmptyDefinedOutlinerNodeStates();
}

void FSequencerSelectionPreview::EmptyDefinedKeyStates()
{
	DefinedKeyStates.Reset();
	CachedSelectionHash.Reset();
}

void FSequencerSelectionPreview::EmptyDefinedSectionStates()
{
	DefinedSectionStates.Reset();
	CachedSelectionHash.Reset();
}

void FSequencerSelectionPreview::EmptyDefinedOutlinerNodeStates()
{
	DefinedOutlinerNodeStates.Reset();
	CachedSelectionHash.Reset();
}

uint32 FSequencerSelectionPreview::GetSelectionHash() const
{
	if (!CachedSelectionHash.IsSet())
	{
		uint32 NewHash = 0;

		for (TPair<FSequencerSelectedKey, ESelectionPreviewState> Pair : DefinedKeyStates)
		{
			NewHash = HashCombine(NewHash, HashCombine(GetTypeHash(Pair.Key), GetTypeHash(Pair.Value)));
		}
		for (TPair<TWeakObjectPtr<UMovieSceneSection>, ESelectionPreviewState> Pair : DefinedSectionStates)
		{
			NewHash = HashCombine(NewHash, HashCombine(GetTypeHash(Pair.Key), GetTypeHash(Pair.Value)));
		}
		for (TPair<TSharedRef<FSequencerDisplayNode>, ESelectionPreviewState> Pair : DefinedOutlinerNodeStates)
		{
			NewHash = HashCombine(NewHash, HashCombine(GetTypeHash(Pair.Key), GetTypeHash(Pair.Value)));
		}

		CachedSelectionHash = NewHash;
	}

	return CachedSelectionHash.GetValue();
}