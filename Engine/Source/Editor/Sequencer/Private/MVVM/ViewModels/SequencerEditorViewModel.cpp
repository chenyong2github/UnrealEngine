// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/SequencerOutlinerViewModel.h"
#include "MVVM/ViewModels/SequencerTrackAreaViewModel.h"
#include "MVVM/CurveEditorExtension.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/SharedViewModelData.h"
#include "ISequencerModule.h"
#include "Sequencer.h"
#include "MovieSceneSequenceID.h"

namespace UE
{
namespace Sequencer
{

TMap<TWeakPtr<FViewModel>, FString> GetNodePaths(FViewModelPtr RootModel)
{
	TMap<TWeakPtr<FViewModel>, FString> NodePaths;
	constexpr bool bIncludeThis = true;
	for (FParentFirstChildIterator ChildIt(RootModel, bIncludeThis); ChildIt; ++ChildIt)
	{
		FViewModelPtr CurrentViewModel = *ChildIt;
		if (const IOutlinerExtension* TreeItem = CurrentViewModel->CastThis<IOutlinerExtension>())
		{
			const FString NodePath = IOutlinerExtension::GetPathName(CurrentViewModel);

			NodePaths.Add(FWeakViewModelPtr(CurrentViewModel), NodePath);
		}
	}

	return NodePaths;
}
	
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

	if (FViewModelPtr RootModel = GetRootModel())
	{
		TSharedPtr<FSharedViewModelData> RootSharedData = RootModel->GetSharedData();
		RootSharedData->SubscribeToHierarchyChanged(RootModel)
			.AddSP(this, &FSequencerEditorViewModel::HandleDataHierarchyChanged);

		NodePaths = GetNodePaths(RootModel);
	}
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

void FSequencerEditorViewModel::HandleDataHierarchyChanged()
{
	if (FViewModelPtr RootModel = GetRootModel())
	{
		TMap<TWeakPtr<FViewModel>, FString> NewNodePaths = GetNodePaths(RootModel);

		TSharedPtr<FSequencer> Sequencer = GetSequencerImpl();
		if (Sequencer)
		{
			for (TMap<TWeakPtr<FViewModel>, FString>::TConstIterator It = NewNodePaths.CreateConstIterator(); It; ++It)
			{
				if (NodePaths.Contains(It.Key()))
				{
					FString OldPath = NodePaths[It.Key()];
					FString NewPath = It.Value();
					Sequencer->OnNodePathChanged(OldPath, NewPath);
				}
			}
		}

		NodePaths = NewNodePaths;
	}
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

