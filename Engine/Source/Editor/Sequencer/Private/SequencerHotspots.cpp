// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerHotspots.h"
#include "MVVM/Extensions/IObjectBindingExtension.h"
#include "SequencerCommonHelpers.h"
#include "SSequencer.h"
#include "Tools/EditToolDragOperations.h"
#include "SequencerContextMenus.h"
#include "MVVM/Views/STrackAreaView.h"
#include "Tools/SequencerEditTool_Movement.h"
#include "Tools/SequencerEditTool_Selection.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/AppStyle.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneTimeHelpers.h"
#include "SequencerCommonHelpers.h"

#include "MVVM/ViewModels/SectionModel.h"

#define LOCTEXT_NAMESPACE "SequencerHotspots"

namespace UE
{
namespace Sequencer
{

UE_SEQUENCER_DEFINE_CASTABLE(FKeyHotspot);
UE_SEQUENCER_DEFINE_CASTABLE(FSectionEasingAreaHotspot);
UE_SEQUENCER_DEFINE_CASTABLE(FSectionEasingHandleHotspot);
UE_SEQUENCER_DEFINE_CASTABLE(FSectionHotspot);
UE_SEQUENCER_DEFINE_CASTABLE(FSectionHotspotBase);
UE_SEQUENCER_DEFINE_CASTABLE(FSectionResizeHotspot);

UE_SEQUENCER_DEFINE_VIEW_MODEL_TYPE_ID(IMouseHandlerHotspot);

FHotspotSelectionManager::FHotspotSelectionManager(const FPointerEvent* InMouseEvent, FSequencer* InSequencer)
	: MouseEvent(InMouseEvent)
	, Selection(&InSequencer->GetSelection())
	, Sequencer(InSequencer)
{
	Selection->SuspendBroadcast();

	bForceSelect = !MouseEvent->IsControlDown();
	bAddingToSelection = MouseEvent->IsShiftDown() || MouseEvent->IsControlDown();

	if (MouseEvent->GetEffectingButton() != EKeys::RightMouseButton)
	{
		// When single-clicking without the RMB, we always wipe the current selection
		ConditionallyClearSelection();
	}
}

FHotspotSelectionManager::~FHotspotSelectionManager()
{
	Selection->ResumeBroadcast();
}

void FHotspotSelectionManager::ConditionallyClearSelection()
{
	if (!bAddingToSelection)
	{
		Selection->EmptySelectedTrackAreaItems();
		Selection->EmptySelectedKeys();

		bAddingToSelection = true;
	}
}

void FHotspotSelectionManager::ToggleKeys(TArrayView<const FSequencerSelectedKey> InKeys)
{
	for (const FSequencerSelectedKey& Key : InKeys)
	{
		const bool bIsSelected = Selection->IsSelected(Key);
		if (bIsSelected && bForceSelect)
		{
			continue;
		}

		if (!bIsSelected)
		{
			Selection->AddToSelection(Key);
		}
		else
		{
			Selection->RemoveFromSelection(Key);
		}
	}
}

void FHotspotSelectionManager::ToggleModel(TSharedPtr<FViewModel> InModel)
{
	const bool bIsSelected = Selection->IsSelected(InModel);
	if (bIsSelected && bForceSelect)
	{
		return;
	}

	TSharedPtr<ISelectableExtension> Selectable = InModel->CastThisShared<ISelectableExtension>();
	if (!Selectable)
	{
		return;
	}
	else if (MouseEvent->GetEffectingButton() == EKeys::RightMouseButton && !EnumHasAnyFlags(Selectable->IsSelectable(), ESelectionIntent::ContextMenu))
	{
		return;
	}
	else if (MouseEvent->GetEffectingButton() == EKeys::LeftMouseButton && !EnumHasAnyFlags(Selectable->IsSelectable(), ESelectionIntent::PersistentSelection))
	{
		return;
	}

	if (!bIsSelected)
	{
		Selection->AddToSelection(InModel);
	}
	else
	{
		Selection->RemoveFromSelection(InModel);
	}
}

void FHotspotSelectionManager::SelectKeysExclusive(TArrayView<const FSequencerSelectedKey> InKeys)
{
	for (const FSequencerSelectedKey& Key : InKeys)
	{
		if (!Selection->IsSelected(Key))
		{
			ConditionallyClearSelection();
			Selection->AddToSelection(Key);
		}
	}
}

void FHotspotSelectionManager::SelectModelExclusive(TSharedPtr<FViewModel> InModel)
{
	if (!Selection->IsSelected(InModel))
	{
		ConditionallyClearSelection();
		Selection->AddToSelection(InModel);
	}
}

void FKeyHotspot::HandleMouseSelection(FHotspotSelectionManager& SelectionManager)
{
	if (SelectionManager.MouseEvent->GetEffectingButton() == EKeys::RightMouseButton)
	{
		SelectionManager.SelectKeysExclusive(Keys);
	}
	else
	{
		SelectionManager.ToggleKeys(Keys);
	}
}

void FKeyHotspot::UpdateOnHover(FTrackAreaViewModel& InTrackArea) const
{
	InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
}

TOptional<FFrameNumber> FKeyHotspot::GetTime() const
{
	FFrameNumber Time = 0;

	if (Keys.Num())
	{
		TArrayView<const FSequencerSelectedKey> FirstKey(&Keys[0], 1);
		TArrayView<FFrameNumber> FirstKeyTime(&Time, 1);
		GetKeyTimes(FirstKey, FirstKeyTime);
	}

	return Time;
}

bool FKeyHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, FFrameTime MouseDownTime)
{
	if (TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin())
	{
		FKeyContextMenu::BuildMenu(MenuBuilder, *Sequencer);
	}
	return true;
}

UMovieSceneSection* FSectionHotspotBase::GetSection() const
{
	if (TSharedPtr<FSectionModel> Model = WeakSectionModel.Pin())
	{
		return Model->GetSection();
	}
	return nullptr;
}

void FSectionHotspotBase::HandleMouseSelection(FHotspotSelectionManager& SelectionManager)
{
	// Base-class only handles RMB selection so that the other handles and interactive controls
	// that act as hotspots and still operate correctly with Left click
	TSharedPtr<FSectionModel> Section = WeakSectionModel.Pin();
	if (Section && SelectionManager.MouseEvent->GetEffectingButton() == EKeys::RightMouseButton)
	{
		SelectionManager.SelectModelExclusive(Section);
	}
}

TOptional<FFrameNumber> FSectionHotspotBase::GetTime() const
{
	UMovieSceneSection* ThisSection = GetSection();
	return ThisSection && ThisSection->HasStartFrame() ? ThisSection->GetInclusiveStartFrame() : TOptional<FFrameNumber>();
}

TOptional<FFrameTime> FSectionHotspotBase::GetOffsetTime() const
{
	UMovieSceneSection* ThisSection = GetSection();
	return ThisSection ? ThisSection->GetOffsetTime() : TOptional<FFrameTime>();
}

void FSectionHotspotBase::UpdateOnHover(FTrackAreaViewModel& InTrackArea) const
{
	UMovieSceneSection* ThisSection = GetSection();

	// Move sections if they are selected
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer->GetSelection().IsSelected(WeakSectionModel))
	{
		InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
	}
	else if (ThisSection)
	{
		// Activate selection mode if the section has keys
		for (const FMovieSceneChannelEntry& Entry : ThisSection->GetChannelProxy().GetAllEntries())
		{
			for (const FMovieSceneChannel* Channel : Entry.GetChannels())
			{
				if (Channel && Channel->GetNumKeys() != 0)
				{
					InTrackArea.AttemptToActivateTool(FSequencerEditTool_Selection::Identifier);
					return;
				}
			}
		}

		// Activate selection mode if the section is infinite, otherwise just move it
		if (ThisSection->GetRange() == TRange<FFrameNumber>::All())
		{
			InTrackArea.AttemptToActivateTool(FSequencerEditTool_Selection::Identifier);
		}
		else
		{
			InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
		}
	}
}

bool FSectionHotspotBase::PopulateContextMenu(FMenuBuilder& MenuBuilder, FFrameTime MouseDownTime)
{
	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	UMovieSceneSection*       ThisSection  = SectionModel ? SectionModel->GetSection() : nullptr;
	if (ThisSection)
	{
		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		FSectionContextMenu::BuildMenu(MenuBuilder, *Sequencer, MouseDownTime);

		TSharedPtr<IObjectBindingExtension> ObjectBinding = SectionModel->FindAncestorOfType<IObjectBindingExtension>();
		SectionModel->GetSectionInterface()->BuildSectionContextMenu(MenuBuilder, ObjectBinding ? ObjectBinding->GetObjectGuid() : FGuid());
	}

	return true;
}

void FSectionHotspot::HandleMouseSelection(FHotspotSelectionManager& SelectionManager)
{
	TSharedPtr<FSectionModel> Section = WeakSectionModel.Pin();
	if (Section && SelectionManager.MouseEvent->GetEffectingButton() == EKeys::LeftMouseButton)
	{
		SelectionManager.ToggleModel(Section);
	}
	else
	{
		FSectionHotspotBase::HandleMouseSelection(SelectionManager);
	}
}

TOptional<FFrameNumber> FSectionResizeHotspot::GetTime() const
{
	UMovieSceneSection* ThisSection = GetSection();
	if (!ThisSection)
	{
		return TOptional<FFrameNumber>();
	}
	return HandleType == Left ? ThisSection->GetInclusiveStartFrame() : ThisSection->GetExclusiveEndFrame();
}

void FSectionResizeHotspot::UpdateOnHover(FTrackAreaViewModel& InTrackArea) const
{
	InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
}

TSharedPtr<ISequencerEditToolDragOperation> FSectionResizeHotspot::InitiateDrag(const FPointerEvent& MouseEvent)
{
	TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer->GetSelection().IsSelected(WeakSectionModel))
	{
		Sequencer->GetSelection().Empty();
		Sequencer->GetSelection().AddToSelection(WeakSectionModel.Pin());
	}

	const bool bIsSlipping = false;
	return MakeShareable( new FResizeSection(*Sequencer, Sequencer->GetSelection().GetSelectedSections(), HandleType == Right, bIsSlipping) );
}

const FSlateBrush* FSectionResizeHotspot::GetCursorDecorator(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (CursorEvent.IsControlDown())
	{
		return FAppStyle::Get().GetBrush(TEXT("Sequencer.CursorDecorator_Retime"));
	}
	else
	{
		return ITrackAreaHotspot::GetCursorDecorator(MyGeometry, CursorEvent);
	}
}

TOptional<FFrameNumber> FSectionEasingHandleHotspot::GetTime() const
{
	UMovieSceneSection* ThisSection = GetSection();
	if (ThisSection)
	{
		if (HandleType == ESequencerEasingType::In && !ThisSection->GetEaseInRange().IsEmpty())
		{
			return UE::MovieScene::DiscreteExclusiveUpper(ThisSection->GetEaseInRange());
		}
		else if (HandleType == ESequencerEasingType::Out && !ThisSection->GetEaseOutRange().IsEmpty())
		{
			return UE::MovieScene::DiscreteInclusiveLower(ThisSection->GetEaseOutRange());
		}
	}
	return TOptional<FFrameNumber>();
}

void FSectionEasingHandleHotspot::UpdateOnHover(FTrackAreaViewModel& InTrackArea) const
{
	InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
}

bool FSectionEasingHandleHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, FFrameTime MouseDownTime)
{
	TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();

	FEasingContextMenu::BuildMenu(MenuBuilder, { FEasingAreaHandle{WeakSectionModel, HandleType} }, *Sequencer, MouseDownTime);
	return true;
}

TSharedPtr<ISequencerEditToolDragOperation> FSectionEasingHandleHotspot::InitiateDrag(const FPointerEvent& MouseEvent)
{
	UMovieSceneSection* Section = GetSection();
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	return MakeShareable( new FManipulateSectionEasing(*static_cast<FSequencer*>(Sequencer.Get()), Section, HandleType == ESequencerEasingType::In) );
}

const FSlateBrush* FSectionEasingHandleHotspot::GetCursorDecorator(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FAppStyle::Get().GetBrush(TEXT("Sequencer.CursorDecorator_EasingHandle"));
}

bool FSectionEasingAreaHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, FFrameTime MouseDownTime)
{
	using namespace UE::Sequencer;

	TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
	FEasingContextMenu::BuildMenu(MenuBuilder, Easings, *Sequencer, MouseDownTime);

	TSharedPtr<FSectionModel> SectionModel = WeakSectionModel.Pin();
	UMovieSceneSection*       ThisSection  = SectionModel ? SectionModel->GetSection() : nullptr;
	if (ThisSection)
	{
		TSharedPtr<IObjectBindingExtension> ObjectBinding = SectionModel->FindAncestorOfType<IObjectBindingExtension>();
		SectionModel->GetSectionInterface()->BuildSectionContextMenu(MenuBuilder, ObjectBinding ? ObjectBinding->GetObjectGuid() : FGuid());
	}

	return true;
}

void FSectionEasingAreaHotspot::HandleMouseSelection(FHotspotSelectionManager& SelectionManager)
{
	TSharedPtr<FSectionModel> Section = WeakSectionModel.Pin();
	if (Section && SelectionManager.MouseEvent->GetEffectingButton() == EKeys::LeftMouseButton)
	{
		SelectionManager.ToggleModel(Section);
	}
	else
	{
		FSectionHotspotBase::HandleMouseSelection(SelectionManager);
	}
}

bool FSectionEasingAreaHotspot::Contains(UMovieSceneSection* InSection) const
{
	return Easings.ContainsByPredicate([=](const FEasingAreaHandle& InHandle){ return InHandle.WeakSectionModel.IsValid() && InHandle.WeakSectionModel.Pin()->GetSection() == InSection; });
}

} // namespace Sequencer
} // namespace UE

#undef LOCTEXT_NAMESPACE
