// Copyright Epic Games, Inc. All Rights Reserved.
/**
* Hold the View for the Tween Widget
*/
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Tools/ControlRigPose.h"
#include "Tools/ControlRigTweener.h"

class UControlRig;
class ISequencer;
class FControlRigEditModeToolkit;

class SControlRigTweenWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SControlRigTweenWidget) {}
	SLATE_ARGUMENT(UControlRigPoseAsset*, PoseAsset)
    SLATE_ARGUMENT(TSharedPtr<FControlRigEditModeToolkit>, InOwningToolkit)
	SLATE_END_ARGS()
	~SControlRigTweenWidget()
	{
	}

	void Construct(const FArguments& InArgs);

private:

	UControlRig* GetControlRig();

	/*
	* Delegates and Helpers
	*/
	void OnPoseBlendChanged(float ChangedVal);
	void OnPoseBlendCommited(float ChangedVal, ETextCommit::Type Type);
	void OnBeginSliderMovement();
	void OnEndSliderMovement(float NewValue);
	float OnGetPoseBlendValueFloat() const { return PoseBlendValue; }

	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	};
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void FinishDraggingWidget(const FVector2D InLocation);

	void SetupControls();
	float PoseBlendValue;
	bool bIsBlending;
	bool bSliderStartedTransaction;

	FControlsToTween  ControlsToTween;

	TWeakPtr<ISequencer> WeakSequencer;
	TWeakPtr<FControlRigEditModeToolkit> OwningToolkit;
};

