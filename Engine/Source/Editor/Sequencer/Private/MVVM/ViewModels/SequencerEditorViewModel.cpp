// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/SequencerOutlinerViewModel.h"
#include "MVVM/ViewModels/SequencerTrackAreaViewModel.h"
#include "MVVM/CurveEditorExtension.h"
#include "ISequencerModule.h"
#include "Sequencer.h"
#include "MovieSceneSequenceID.h"

namespace UE
{
namespace Sequencer
{

FSequencerEditorViewModel::FSequencerEditorViewModel(TSharedRef<ISequencer> InSequencer, const FSequencerHostCapabilities& InHostCapabilities)
	: WeakSequencer(InSequencer)
	, bSupportsCurveEditor(InHostCapabilities.bSupportsCurveEditor)
{
}

void FSequencerEditorViewModel::PreInitializeEditorImpl()
{
	if (bSupportsCurveEditor)
	{
		AddDynamicExtension(FCurveEditorExtension::ID);
	}
}

TSharedPtr<FViewModel> FSequencerEditorViewModel::CreateRootModelImpl()
{
	TSharedPtr<FSequenceModel> RootSequenceModel = MakeShared<FSequenceModel>(SharedThis(this));
	RootSequenceModel->InitializeExtensions();
	return RootSequenceModel;
}

TSharedPtr<FOutlinerViewModel> FSequencerEditorViewModel::CreateOutlinerImpl()
{
	return MakeShared<FSequencerOutlinerViewModel>();
}

TSharedPtr<FTrackAreaViewModel> FSequencerEditorViewModel::CreateTrackAreaImpl()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer.IsValid());

	TSharedRef<FSequencerTrackAreaViewModel> NewTrackArea = MakeShared<FSequencerTrackAreaViewModel>(Sequencer.ToSharedRef());
	NewTrackArea->GetOnHotspotChangedDelegate().AddSP(SharedThis(this), &FSequencerEditorViewModel::OnTrackAreaHotspotChanged);
	return NewTrackArea;
}

void FSequencerEditorViewModel::InitializeEditorImpl()
{
	PinnedTrackArea = CreateTrackAreaImpl();
	PinnedTrackArea->GetOnHotspotChangedDelegate().AddSP(SharedThis(this), &FSequencerEditorViewModel::OnTrackAreaHotspotChanged);
	GetEditorPanels().AddChild(PinnedTrackArea);
}

TSharedPtr<FTrackAreaViewModel> FSequencerEditorViewModel::GetPinnedTrackArea() const
{
	return PinnedTrackArea;
}

TSharedPtr<ISequencer> FSequencerEditorViewModel::GetSequencer() const
{
	return WeakSequencer.Pin();
}

TSharedPtr<FSequencer> FSequencerEditorViewModel::GetSequencerImpl() const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	return StaticCastSharedPtr<FSequencer>(Sequencer);
}

void FSequencerEditorViewModel::SetSequence(UMovieSceneSequence* InRootSequence)
{
	TSharedPtr<FSequenceModel> SequenceModel = GetRootModel().ImplicitCast();
	SequenceModel->SetSequence(InRootSequence, MovieSceneSequenceID::Root);
}

bool FSequencerEditorViewModel::IsReadOnly() const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	return !Sequencer || Sequencer->IsReadOnly();
}

TSharedPtr<ITrackAreaHotspot> FSequencerEditorViewModel::GetHotspot() const
{
	return CurrentHotspot;
}

void FSequencerEditorViewModel::OnTrackAreaHotspotChanged(TSharedPtr<ITrackAreaHotspot> NewHotspot)
{
	CurrentHotspot = NewHotspot;
}

} // namespace Sequencer
} // namespace UE

