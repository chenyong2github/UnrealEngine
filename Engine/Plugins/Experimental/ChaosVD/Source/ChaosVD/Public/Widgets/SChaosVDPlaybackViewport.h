// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

class FChaosVDPlaybackController;
class SChaosVDTimelineWidget;
struct FChaosVDRecording;
class FChaosVDScene;
class FLevelEditorViewportClient;
class FSceneViewport;
class SViewport;

/* Widget that contains the 3D viewport and playback controls */
class SChaosVDPlaybackViewport : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS( SChaosVDPlaybackViewport ){}
	SLATE_END_ARGS()

	virtual ~SChaosVDPlaybackViewport() override;

	void Construct(const FArguments& InArgs, const UWorld* DefaultWorld, TWeakPtr<FChaosVDPlaybackController> InPlaybackController);

protected:

	TSharedPtr<FLevelEditorViewportClient> CreateViewportClient() const;

	void OnPlaybackControllerUpdated(FChaosVDPlaybackController* Controller) const;

	void OnFrameSelectionUpdated(int32 NewFrameIndex) const;
	void OnStepSelectionUpdated(int32 NewStepIndex) const;
	
	TSharedPtr<SChaosVDTimelineWidget> FramesTimelineWidget;
	TSharedPtr<SChaosVDTimelineWidget> StepsTimelineWidget;

	TSharedPtr<FLevelEditorViewportClient> LevelViewportClient;
	TSharedPtr<SViewport> ViewportWidget;
	TSharedPtr<FSceneViewport> SceneViewport;

	TWeakPtr<FChaosVDPlaybackController> PlaybackController;
};
