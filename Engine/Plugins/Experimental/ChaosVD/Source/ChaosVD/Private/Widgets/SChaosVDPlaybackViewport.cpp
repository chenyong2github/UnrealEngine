// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDPlaybackViewport.h"

#include "ChaosVDPlaybackController.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SChaosVDTimelineWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

SChaosVDPlaybackViewport::~SChaosVDPlaybackViewport()
{
	LevelViewportClient->Viewport = nullptr;
	LevelViewportClient.Reset();
}

TSharedPtr<FLevelEditorViewportClient> SChaosVDPlaybackViewport::CreateViewportClient() const
{
	TSharedPtr<FLevelEditorViewportClient> NewViewport = MakeShareable(new FLevelEditorViewportClient(TSharedPtr<class SLevelViewport>()));

	NewViewport->SetAllowCinematicControl(false);
	
	NewViewport->bSetListenerPosition = false;
	NewViewport->EngineShowFlags = FEngineShowFlags(ESFIM_Editor);
	NewViewport->LastEngineShowFlags = FEngineShowFlags(ESFIM_Editor);
	NewViewport->ViewportType = LVT_Perspective;
	NewViewport->bDrawAxes = true;
	NewViewport->bDisableInput = false;
	NewViewport->VisibilityDelegate.BindLambda([] {return true; });

	return NewViewport;
}

void SChaosVDPlaybackViewport::Construct(const FArguments& InArgs, const UWorld* DefaultWorld, TWeakPtr<FChaosVDPlaybackController> InPlaybackController)
{
	ensure(DefaultWorld);
	ensure(InPlaybackController.IsValid());

	PlaybackController = InPlaybackController;

	LevelViewportClient = CreateViewportClient();

	ViewportWidget = SNew(SViewport)
		.RenderDirectlyToWindow(false)
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
		.EnableGammaCorrection(false)
		.EnableBlending(false);

	SceneViewport = MakeShareable(new FSceneViewport(LevelViewportClient.Get(), ViewportWidget));

	LevelViewportClient->Viewport = SceneViewport.Get();

	ViewportWidget->SetViewportInterface(SceneViewport.ToSharedRef());
	
	// Default to the base map
	LevelViewportClient->SetReferenceToWorldContext(*GEngine->GetWorldContextFromWorld(DefaultWorld));

	ChildSlot
	[
		// 3D Viewport
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(0.9f)
		[
			ViewportWidget.ToSharedRef()
		]
		// Playback controls
		// TODO: Now that the tool is In-Editor, see if we can/is worth use the Sequencer widgets
		// instead of these custom ones
		+SVerticalBox::Slot()
		.Padding(16.0f, 16.0f, 16.0f, 16.0f)
		.FillHeight(0.1f)
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
						.OnFrameChanged_Raw(this, &SChaosVDPlaybackViewport::OnFrameSelectionUpdated)
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
					.OnFrameChanged_Raw(this, &SChaosVDPlaybackViewport::OnStepSelectionUpdated)
					.MaxFrames(0)
				]	
			]
		]
	];

	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		PlaybackControllerPtr->OnControllerUpdated().BindRaw(this, &SChaosVDPlaybackViewport::OnPlaybackControllerUpdated);
	}
}

void SChaosVDPlaybackViewport::OnPlaybackControllerUpdated(FChaosVDPlaybackController* Controller) const
{
	if (!ensure(Controller))
	{
		return;
	}

	if (TSharedPtr<FChaosVDRecording> LoadedRecording = Controller->GetCurrentRecording().Pin())
	{
		const int32 AvailableFrames = Controller->GetAvailableFramesNumber();
		const int32 AvailableSteps = Controller->GetStepsForFrame(Controller->GetCurrentFrame());

		// Max is inclusive and we use this to request as the index on the recorded frames/steps arrays so we need to -1 to the available frames/steps
		FramesTimelineWidget->UpdateMinMaxValue(0, AvailableFrames != INDEX_NONE ? AvailableFrames -1  : 0);

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

	LevelViewportClient->bNeedsRedraw = true;
}

void SChaosVDPlaybackViewport::OnFrameSelectionUpdated(int32 NewFrameIndex) const
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		// On Frame updates, always use Step 0
		PlaybackControllerPtr->GoToRecordedStep(NewFrameIndex, 0);

		LevelViewportClient->bNeedsRedraw = true;
	}
}

void SChaosVDPlaybackViewport::OnStepSelectionUpdated(int32 NewStepIndex) const
{
	if (const TSharedPtr<FChaosVDPlaybackController> PlaybackControllerPtr = PlaybackController.Pin())
	{
		// On Steps updates. Always use the current Frame
		PlaybackControllerPtr->GoToRecordedStep(PlaybackControllerPtr->GetCurrentFrame(), NewStepIndex);
	
		LevelViewportClient->bNeedsRedraw = true;
	}
}

#undef LOCTEXT_NAMESPACE
