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

void FSequencerSelectionPreview::SetSelectionState(TWeakPtr<UE::Sequencer::FViewModel> InModel, ESelectionPreviewState InState)
{
	if (InState == ESelectionPreviewState::Undefined)
	{
		DefinedModelStates.Remove(InModel);
	}
	else
	{
		DefinedModelStates.Add(InModel, InState);
	}
	CachedSelectionHash.Reset();
}

ESelectionPreviewState FSequencerSelectionPreview::GetSelectionState(const FSequencerSelectedKey& Key) const
{
	if (const ESelectionPreviewState* State = DefinedKeyStates.Find(Key))
	{
		return *State;
	}
	return ESelectionPreviewState::Undefined;
}

ESelectionPreviewState FSequencerSelectionPreview::GetSelectionState(TWeakPtr<UE::Sequencer::FViewModel> InModel) const
{
	if (const ESelectionPreviewState* State = DefinedModelStates.Find(InModel))
	{
		return *State;
	}
	return ESelectionPreviewState::Undefined;
}

void FSequencerSelectionPreview::Empty()
{
	EmptyDefinedKeyStates();
	EmptyDefinedModelStates();
}

void FSequencerSelectionPreview::EmptyDefinedKeyStates()
{
	DefinedKeyStates.Reset();
	CachedSelectionHash.Reset();
}

void FSequencerSelectionPreview::EmptyDefinedModelStates()
{
	DefinedModelStates.Reset();
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
		for (TPair<TWeakPtr<UE::Sequencer::FViewModel>, ESelectionPreviewState> Pair : DefinedModelStates)
		{
			NewHash = HashCombine(NewHash, HashCombine(GetTypeHash(Pair.Key), GetTypeHash(Pair.Value)));
		}

		CachedSelectionHash = NewHash;
	}

	return CachedSelectionHash.GetValue();
}
