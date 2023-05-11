// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDPlaybackControllerInstigator.h"
#include "ChaosVDPlaybackControllerObserver.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SCompoundWidget.h"

struct FChaosVDTrackInfo;
class FChaosVDPlaybackController;
class SChaosVDTimelineWidget;

/** Widget that Generates playback controls for solvers
 * Which are two timelines, one for physics frames and other for steps
 */
class SChaosVDSolverPlaybackControls : public SCompoundWidget, public FChaosVDPlaybackControllerObserver, public IChaosVDPlaybackControllerInstigator
{
public:
	SLATE_BEGIN_ARGS( SChaosVDSolverPlaybackControls ){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, int32 InSolverID, const TWeakPtr<FChaosVDPlaybackController>& InPlaybackController);

private:

	void OnFrameSelectionUpdated(int32 NewFrameIndex) const;
	void OnStepSelectionUpdated(int32 NewStepIndex) const;

	virtual void HandlePlaybackControllerDataUpdated(TWeakPtr<FChaosVDPlaybackController> InController) override;
	virtual void HandleControllerTrackFrameUpdated(TWeakPtr<FChaosVDPlaybackController> InController, const FChaosVDTrackInfo* UpdatedTrackInfo, FGuid InstigatorGuid) override;

	int32 SolverID = INDEX_NONE;
	TSharedPtr<SChaosVDTimelineWidget> FramesTimelineWidget;
	TSharedPtr<SChaosVDTimelineWidget> StepsTimelineWidget;
};
