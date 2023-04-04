// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/EditorViewModel.h"

struct FMovieSceneSequenceID;

class ISequencer;
class FSequencer;
class UMovieSceneSequence;
struct FSequencerHostCapabilities;

namespace UE
{
namespace Sequencer
{

class FSequenceModel;
class STrackAreaView;
struct ITrackAreaHotspot;

/**
 * Main view-model for the Sequencer editor.
 */
class FSequencerEditorViewModel : public FEditorViewModel
{
public:

	UE_SEQUENCER_DECLARE_CASTABLE(FSequencerEditorViewModel, FEditorViewModel);

	FSequencerEditorViewModel(TSharedRef<ISequencer> InSequencer, const FSequencerHostCapabilities& InHostCapabilities);

	// @todo_sequencer_mvvm remove this later
	TSharedPtr<ISequencer> GetSequencer() const;
	// @todo_sequencer_mvvm remove this ASAP
	TSharedPtr<FSequencer> GetSequencerImpl() const;

	// @todo_sequencer_mvvm move this to the root view-model
	void SetSequence(UMovieSceneSequence* InRootSequence);

	/** Gets the pinned track area view-model. */
	TSharedPtr<FTrackAreaViewModel> GetPinnedTrackArea() const;

	/** Gets the current hotspots across any of our track areas */
	TSharedPtr<ITrackAreaHotspot> GetHotspot() const;

	void HandleDataHierarchyChanged();

protected:

	virtual void PreInitializeEditorImpl() override;
	virtual void InitializeEditorImpl() override;
	virtual TSharedPtr<FViewModel> CreateRootModelImpl() override;
	virtual TSharedPtr<FOutlinerViewModel> CreateOutlinerImpl() override;
	virtual TSharedPtr<FTrackAreaViewModel> CreateTrackAreaImpl() override;
	virtual bool IsReadOnly() const override;

	void OnTrackAreaHotspotChanged(TSharedPtr<ITrackAreaHotspot> NewHotspot);

protected:

	TWeakPtr<ISequencer> WeakSequencer;
	TSharedPtr<FTrackAreaViewModel> PinnedTrackArea;
	bool bSupportsCurveEditor;

	/** The current hotspot, from any of our track areas */
	TSharedPtr<ITrackAreaHotspot> CurrentHotspot;

	// Cached node paths to be used to compare when the hierarchy changes
	TMap<TWeakPtr<FViewModel>, FString> NodePaths;
};

} // namespace Sequencer
} // namespace UE

