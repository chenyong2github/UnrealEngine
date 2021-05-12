// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerHotspots.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "SequencerCommonHelpers.h"
#include "SSequencer.h"
#include "Tools/EditToolDragOperations.h"
#include "SequencerContextMenus.h"
#include "SSequencerTrackArea.h"
#include "Tools/SequencerEditTool_Movement.h"
#include "Tools/SequencerEditTool_Selection.h"
#include "SequencerTrackNode.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneTimeHelpers.h"

#define LOCTEXT_NAMESPACE "SequencerHotspots"


void FKeyHotspot::UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const
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

bool FKeyHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& InSequencer, FFrameTime MouseDownTime)
{
	FSequencer& Sequencer = static_cast<FSequencer&>(InSequencer);
	FKeyContextMenu::BuildMenu(MenuBuilder, Sequencer);
	return true;
}

TOptional<FFrameNumber> FSectionHotspot::GetTime() const
{
	UMovieSceneSection* ThisSection = WeakSection.Get();
	return ThisSection && ThisSection->HasStartFrame() ? ThisSection->GetInclusiveStartFrame() : TOptional<FFrameNumber>();
}

TOptional<FFrameTime> FSectionHotspot::GetOffsetTime() const
{
	UMovieSceneSection* ThisSection = WeakSection.Get();
	return ThisSection ? ThisSection->GetOffsetTime() : TOptional<FFrameTime>();
}

void FSectionHotspot::UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const
{
	UMovieSceneSection* ThisSection = WeakSection.Get();

	// Move sections if they are selected
	if (InSequencer.GetSelection().IsSelected(ThisSection))
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
				if (Channel->GetNumKeys() != 0)
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

bool FSectionHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& InSequencer, FFrameTime MouseDownTime)
{
	UMovieSceneSection* ThisSection = WeakSection.Get();
	if (ThisSection)
	{
		FSequencer& Sequencer = static_cast<FSequencer&>(InSequencer);
		FSectionContextMenu::BuildMenu(MenuBuilder, Sequencer, MouseDownTime);

		TOptional<FSectionHandle> SectionHandle = Sequencer.GetNodeTree()->GetSectionHandle(ThisSection);
		if (SectionHandle.IsSet())
		{
			FGuid ObjectBinding = SectionHandle->GetTrackNode()->GetObjectGuid();
			SectionHandle->GetSectionInterface()->BuildSectionContextMenu(MenuBuilder, ObjectBinding);
		}
	}

	return true;
}

TOptional<FFrameNumber> FSectionResizeHotspot::GetTime() const
{
	UMovieSceneSection* ThisSection = WeakSection.Get();
	if (!ThisSection)
	{
		return TOptional<FFrameNumber>();
	}
	return HandleType == Left ? ThisSection->GetInclusiveStartFrame() : ThisSection->GetExclusiveEndFrame();
}

void FSectionResizeHotspot::UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const
{
	InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
}

TSharedPtr<ISequencerEditToolDragOperation> FSectionResizeHotspot::InitiateDrag(ISequencer& Sequencer)
{
	UMovieSceneSection* ThisSection = WeakSection.Get();

	if (ThisSection && !Sequencer.GetSelection().GetSelectedSections().Contains(ThisSection))
	{
		Sequencer.GetSelection().Empty();
		Sequencer.GetSelection().AddToSelection(ThisSection);
		SequencerHelpers::UpdateHoveredNodeFromSelectedSections(static_cast<FSequencer&>(Sequencer));
	}

	const bool bIsSlipping = false;
	return MakeShareable( new FResizeSection(static_cast<FSequencer&>(Sequencer), Sequencer.GetSelection().GetSelectedSections(), HandleType == Right, bIsSlipping) );
}

const FSlateBrush* FSectionResizeHotspot::GetCursorDecorator(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (CursorEvent.IsControlDown())
	{
		return FEditorStyle::Get().GetBrush(TEXT("Sequencer.CursorDecorator_Retime"));
	}
	else
	{
		return ISequencerHotspot::GetCursorDecorator(MyGeometry, CursorEvent);
	}
}

TOptional<FFrameNumber> FSectionEasingHandleHotspot::GetTime() const
{
	UMovieSceneSection* ThisSection = WeakSection.Get();
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

void FSectionEasingHandleHotspot::UpdateOnHover(SSequencerTrackArea& InTrackArea, ISequencer& InSequencer) const
{
	InTrackArea.AttemptToActivateTool(FSequencerEditTool_Movement::Identifier);
}

bool FSectionEasingHandleHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& Sequencer, FFrameTime MouseDownTime)
{
	FEasingContextMenu::BuildMenu(MenuBuilder, { FEasingAreaHandle{WeakSection, HandleType} }, static_cast<FSequencer&>(Sequencer), MouseDownTime);
	return true;
}

TSharedPtr<ISequencerEditToolDragOperation> FSectionEasingHandleHotspot::InitiateDrag(ISequencer& Sequencer)
{
	return MakeShareable( new FManipulateSectionEasing(static_cast<FSequencer&>(Sequencer), WeakSection, HandleType == ESequencerEasingType::In) );
}

const FSlateBrush* FSectionEasingHandleHotspot::GetCursorDecorator(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FEditorStyle::Get().GetBrush(TEXT("Sequencer.CursorDecorator_EasingHandle"));
}

bool FSectionEasingAreaHotspot::PopulateContextMenu(FMenuBuilder& MenuBuilder, ISequencer& InSequencer, FFrameTime MouseDownTime)
{
	FSequencer& Sequencer = static_cast<FSequencer&>(InSequencer);
	FEasingContextMenu::BuildMenu(MenuBuilder, Easings, Sequencer, MouseDownTime);

	UMovieSceneSection* ThisSection = WeakSection.Get();
	if (ThisSection)
	{
		TOptional<FSectionHandle> SectionHandle = Sequencer.GetNodeTree()->GetSectionHandle(ThisSection);
		if (SectionHandle.IsSet())
		{
			FGuid ObjectBinding = SectionHandle->GetTrackNode()->GetObjectGuid();
			SectionHandle->GetSectionInterface()->BuildSectionContextMenu(MenuBuilder, ObjectBinding);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
