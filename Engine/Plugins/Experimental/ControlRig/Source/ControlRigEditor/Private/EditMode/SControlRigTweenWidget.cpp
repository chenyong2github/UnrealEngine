// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigTweenWidget.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "ScopedTransaction.h"
#include "ControlRig.h"
#include "UnrealEdGlobals.h"
#include "ControlRigEditMode.h"
#include "Tools/ControlRigPose.h"
#include "EditorModeManager.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "ILevelSequenceEditorToolkit.h"

#define LOCTEXT_NAMESPACE "ControlRigTweenWidget"

void SControlRigTweenWidget::Construct(const FArguments& InArgs)
{
	PoseBlendValue = 0.0f;
	bIsBlending = false;
	bSliderStartedTransaction = false;

	ChildSlot
	[
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(20.0f))
		[
			SNew(SVerticalBox)
			 +SVerticalBox::Slot()
				 .AutoHeight()
				 .HAlign(HAlign_Center)
				 [
					 SNew(SSpinBox<float>)
					 .Value(this, &SControlRigTweenWidget::OnGetPoseBlendValueFloat)
					  .ToolTipText(LOCTEXT("TweenTooltip", "Key at current frame between previous(-1.0) and next(1.0) poses. Use Ctrl drag for under and over shoot."))
					 .MinValue(-2.0f)
					 .MaxValue(2.0f)
					 .MinSliderValue(-1.0f)
					 .MaxSliderValue(1.0f)
					 .SliderExponent(1)
					 .Delta(0.005f)
					 .MinDesiredWidth(100.0f)
					 .SupportDynamicSliderMinValue(true)
					 .SupportDynamicSliderMaxValue(true)
					 .OnValueChanged(this, &SControlRigTweenWidget::OnPoseBlendChanged)
					 .OnValueCommitted(this, &SControlRigTweenWidget::OnPoseBlendCommited)
					 .OnBeginSliderMovement(this, &SControlRigTweenWidget::OnBeginSliderMovement)
					 .OnEndSliderMovement(this, &SControlRigTweenWidget::OnEndSliderMovement)
				 ]
		]
	];	
}

void SControlRigTweenWidget::OnPoseBlendChanged(float ChangedVal)
{
	UControlRig* ControlRig = GetControlRig();
	if (ControlRig  && WeakSequencer.IsValid() && bIsBlending)
	{
		PoseBlendValue = ChangedVal;
		ControlsToTween.Blend(WeakSequencer, ChangedVal);
		WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
}

void SControlRigTweenWidget::OnBeginSliderMovement()
{
	if (bSliderStartedTransaction == false)
	{
		bIsBlending = true;
		bSliderStartedTransaction = true;
		GEditor->BeginTransaction(LOCTEXT("TweenTransaction", "Tween It"));
		SetupControls();
	}
}

void SControlRigTweenWidget::SetupControls()
{
	ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	UControlRig* ControlRig = GetControlRig();
	if (ControlRig && WeakSequencer.IsValid())
	{
		WeakSequencer.Pin()->GetFocusedMovieSceneSequence()->GetMovieScene()->Modify();
		TArray<UControlRig*> SelectedControlRigs;
		SelectedControlRigs.Add(ControlRig);
		ControlsToTween.Setup(SelectedControlRigs, WeakSequencer);
	}
}

void SControlRigTweenWidget::OnEndSliderMovement(float NewValue)
{
	if (bSliderStartedTransaction)
	{
		GEditor->EndTransaction();
		bSliderStartedTransaction = false;

	}
	WeakSequencer = nullptr;
}

void SControlRigTweenWidget::OnPoseBlendCommited(float ChangedVal, ETextCommit::Type Type)
{
	UControlRig* ControlRig = GetControlRig();
	if (ControlRig)
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("TweenTransaction", "Tween"));
		if (bIsBlending == false)
		{
			SetupControls();
			bIsBlending = true;
		}
		PoseBlendValue = ChangedVal;
		OnPoseBlendChanged(ChangedVal);
		bIsBlending = false;
		PoseBlendValue = 0.0f;
	}
}

UControlRig* SControlRigTweenWidget::GetControlRig()
{
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName)))
	{
		return EditMode->GetControlRig(true);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
