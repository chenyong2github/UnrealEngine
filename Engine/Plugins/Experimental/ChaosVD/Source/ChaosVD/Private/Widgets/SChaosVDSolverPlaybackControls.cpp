// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDSolverPlaybackControls.h"

#include "ChaosVDPlaybackController.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SChaosVDPlaybackViewport.h"
#include "Widgets/SChaosVDTimelineWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

void SChaosVDSolverPlaybackControls::Construct(const FArguments& InArgs, int32 InSolverID, const TWeakPtr<FChaosVDPlaybackController>& InPlaybackController)
{
	SolverID = InSolverID;

	ChildSlot
	[
		SNew(SVerticalBox)
		// Playback controls
		// TODO: Now that the tool is In-Editor, see if we can/is worth use the Sequencer widgets
		// instead of these custom ones
		+SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(0.7f)
			[
			SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.Text(LOCTEXT("PlaybackViewportWidgetPhysicsFramesLabel", "Physics Frames" ))
				]
				+SVerticalBox::Slot()
				[
					SAssignNew(FramesTimelineWidget, SChaosVDTimelineWidget)
						.HidePlayStopButtons(false)
						.OnFrameChanged_Raw(this, &SChaosVDSolverPlaybackControls::OnFrameSelectionUpdated)
						.MaxFrames(0)
				]
			]
			+SHorizontalBox::Slot()
			.FillWidth(0.3f)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Center)
					.Text(LOCTEXT("PlaybackViewportWidgetStepsLabel", "Solver Steps" ))
				]
				+SVerticalBox::Slot()
				[
					SAssignNew(StepsTimelineWidget, SChaosVDTimelineWidget)
					.HidePlayStopButtons(true)
					.OnFrameChanged_Raw(this, &SChaosVDSolverPlaybackControls::OnStepSelectionUpdated)
					.MaxFrames(0)
				]	
			]
		]
	];

	RegisterNewController(InPlaybackController);

	if (const TSharedPtr<FChaosVDPlaybackController> CurrentPlaybackControllerPtr = InPlaybackController.Pin())
	{
		if (const FChaosVDTrackInfo* SolverTrackInfo = CurrentPlaybackControllerPtr->GetTrackInfo(EChaosVDTrackType::Solver, SolverID))
		{
			HandleControllerTrackFrameUpdated(InPlaybackController, SolverTrackInfo, InvalidGuid);
		}
	}
}

void SChaosVDSolverPlaybackControls::HandlePlaybackControllerDataUpdated(TWeakPtr<FChaosVDPlaybackController> InController)
{
	if (PlaybackController != InController)
	{
		RegisterNewController(InController);
	}

	const TSharedPtr<FChaosVDPlaybackController> ControllerSharedPtr = PlaybackController.Pin();
	if (ControllerSharedPtr.IsValid() && ControllerSharedPtr->IsRecordingLoaded())
	{	
		const int32 AvailableFrames = ControllerSharedPtr->GetTrackFramesNumber(EChaosVDTrackType::Solver, SolverID);
		const int32 AvailableSteps = ControllerSharedPtr->GetTrackStepsAtFrame(EChaosVDTrackType::Solver, SolverID, ControllerSharedPtr->GetTrackCurrentFrame(EChaosVDTrackType::Solver, SolverID));

		// Max is inclusive and we use this to request as the index on the recorded frames/steps arrays so we need to -1 to the available frames/steps
		FramesTimelineWidget->UpdateMinMaxValue(0, AvailableFrames != INDEX_NONE ? AvailableFrames -1 : 0);

		//TODO: This will show steps 0/0 if only one step is recorded, we need to add a way to override that functionality
		// or just set the slider to start from 1 and handle the offset later 
		StepsTimelineWidget->UpdateMinMaxValue(0,AvailableSteps != INDEX_NONE ? AvailableSteps -1 : 0);
	}
	else
	{
		FramesTimelineWidget->UpdateMinMaxValue(0, 0);
		FramesTimelineWidget->ResetTimeline();
		StepsTimelineWidget->UpdateMinMaxValue(0,0);
		StepsTimelineWidget->ResetTimeline();
	}
}

void SChaosVDSolverPlaybackControls::HandleControllerTrackFrameUpdated(TWeakPtr<FChaosVDPlaybackController> InController, const FChaosVDTrackInfo* UpdatedTrackInfo, FGuid InstigatorGuid)
{
	if (InstigatorGuid == GetInstigatorID())
	{
		// Ignore the update if we initiated it
		return;
	}

	if (const TSharedPtr<FChaosVDPlaybackController> CurrentPlaybackControllerPtr = InController.Pin())
	{
		if (const FChaosVDTrackInfo* SolverTrackInfo = CurrentPlaybackControllerPtr->GetTrackInfo(EChaosVDTrackType::Solver, SolverID))
		{
			FramesTimelineWidget->SetCurrentTimelineFrame(SolverTrackInfo->CurrentFrame, EChaosVDSetTimelineFrameFlags::None);

			const int32 AvailableSteps = CurrentPlaybackControllerPtr->GetTrackStepsAtFrame(EChaosVDTrackType::Solver, SolverID, SolverTrackInfo->CurrentFrame);
			StepsTimelineWidget->UpdateMinMaxValue(0,AvailableSteps != INDEX_NONE ? AvailableSteps -1 : 0);

			// On Frame updates, always use Step 0
			StepsTimelineWidget->SetCurrentTimelineFrame(0, EChaosVDSetTimelineFrameFlags::None);
		}
	}
}

void SChaosVDSolverPlaybackControls::OnFrameSelectionUpdated(int32 NewFrameIndex) const
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		const int32 AvailableSteps = PlaybackControllerPtr->GetTrackStepsAtFrame(EChaosVDTrackType::Solver, SolverID, PlaybackControllerPtr->GetTrackCurrentFrame(EChaosVDTrackType::Solver, SolverID));
		StepsTimelineWidget->UpdateMinMaxValue(0,AvailableSteps != INDEX_NONE ? AvailableSteps - 1 : 0);

		// On Frame updates, always use Step 0
		constexpr int32 StepNumber = 0;
		StepsTimelineWidget->SetCurrentTimelineFrame(StepNumber, EChaosVDSetTimelineFrameFlags::None);
		PlaybackControllerPtr->GoToTrackFrame(GetInstigatorID(), EChaosVDTrackType::Solver, SolverID, NewFrameIndex, StepNumber);
	}
}

void SChaosVDSolverPlaybackControls::OnStepSelectionUpdated(int32 NewStepIndex) const
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		// On Steps updates. Always use the current Frame
		int32 CurrentFrame = PlaybackControllerPtr->GetTrackCurrentFrame(EChaosVDTrackType::Solver, SolverID);
		PlaybackControllerPtr->GoToTrackFrame(GetInstigatorID(), EChaosVDTrackType::Solver, SolverID, CurrentFrame, NewStepIndex);
	}
}

#undef LOCTEXT_NAMESPACE