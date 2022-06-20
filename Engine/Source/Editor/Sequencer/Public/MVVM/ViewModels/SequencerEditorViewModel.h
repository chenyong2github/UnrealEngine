// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/EditorViewModel.h"

struct FMovieSceneSequenceID;

class ISequencer;
class FSequencer;
class UMovieSceneSequence;

namespace UE
{
namespace Sequencer
{

class FSequenceModel;

/**
 * Main view-model for the Sequencer editor.
 */
class FSequencerEditorViewModel : public FEditorViewModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FSequencerEditorViewModel, FEditorViewModel);

	FSequencerEditorViewModel(TSharedRef<ISequencer> InSequencer);

	// @todo_sequencer_mvvm remove this later
	TSharedPtr<ISequencer> GetSequencer() const;
	// @todo_sequencer_mvvm remove this ASAP
	TSharedPtr<FSequencer> GetSequencerImpl() const;

	// @todo_sequencer_mvvm move this to the root view-model
	void SetSequence(UMovieSceneSequence* InRootSequence);

protected:

	virtual TSharedPtr<FViewModel> CreateRootModelImpl() override;
	virtual TSharedPtr<FOutlinerViewModel> CreateOutlinerImpl() override;
	virtual TSharedPtr<FTrackAreaViewModel> CreateTrackAreaImpl() override;
	virtual void InitializeEditorImpl() override;

protected:

	TWeakPtr<ISequencer> WeakSequencer;
};

} // namespace Sequencer
} // namespace UE

