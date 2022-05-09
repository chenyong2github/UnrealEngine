// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigTweenWidget.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "AssetRegistry/AssetData.h"
#include "Styling/AppStyle.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Styling/AppStyle.h"
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
#include "Viewports/InViewportUIDragOperation.h"
#include "ControlRigEditModeToolkit.h"

#define LOCTEXT_NAMESPACE "ControlRigTweenWidget"

void SControlRigTweenWidget::Construct(const FArguments& InArgs)
{
	PoseBlendValue = 0.0f;
	bIsBlending = false;
	bSliderStartedTransaction = false;
	OwningToolkit = InArgs._InOwningToolkit;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewport.OverlayBrush"))
		.Padding(20.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(STextBlock)
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
				.Text(LOCTEXT("TweenController", "Tween Controller"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SSpinBox<float>)
				.PreventThrottling(true)
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
	TArray<UControlRig*> ControlRigs = GetControlRigs();
	if (ControlRigs.Num() > 0 && WeakSequencer.IsValid() && bIsBlending)
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
		GEditor->BeginTransaction(LOCTEXT("TweenTransaction", "Tween"));
		SetupControls();
	}
}

void SControlRigTweenWidget::SetupControls()
{
	//if getting sequencer from level sequence need to use the current(master), not the focused
	ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	TArray<UControlRig*> ControlRigs = GetControlRigs();
	if (ControlRigs.Num() > 0)
	{
		WeakSequencer.Pin()->GetFocusedMovieSceneSequence()->GetMovieScene()->Modify();
		ControlsToTween.Setup(ControlRigs, WeakSequencer);
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

FReply SControlRigTweenWidget::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Need to remember where within a tab we grabbed
	const FVector2D TabGrabScreenSpaceOffset = MouseEvent.GetScreenSpacePosition() - MyGeometry.GetAbsolutePosition();

	FOnInViewportUIDropped OnUIDropped = FOnInViewportUIDropped::CreateSP(this, &SControlRigTweenWidget::FinishDraggingWidget);
	// Start dragging.
	TSharedRef<FInViewportUIDragOperation> DragDropOperation =
		FInViewportUIDragOperation::New(
			SharedThis(this),
			TabGrabScreenSpaceOffset,
			GetDesiredSize(),
			OnUIDropped
		);
	if (OwningToolkit.IsValid())
	{
		OwningToolkit.Pin()->TryRemoveTweenOverlay();
	}
	return FReply::Handled().BeginDragDrop(DragDropOperation);

	return FReply::Unhandled();
}

void SControlRigTweenWidget::FinishDraggingWidget(const FVector2D InLocation)
{
	if (OwningToolkit.IsValid())
	{
		OwningToolkit.Pin()->UpdateTweenWidgetLocation(InLocation);
		OwningToolkit.Pin()->TryShowTweenOverlay();
	}
}

void SControlRigTweenWidget::OnPoseBlendCommited(float ChangedVal, ETextCommit::Type Type)
{
	TArray<UControlRig*> ControlRigs = GetControlRigs();
	if (ControlRigs.Num() > 0)
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

TArray<UControlRig*>SControlRigTweenWidget::GetControlRigs()
{
	TArray<UControlRig*> ControlRigs;
	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName)))
	{
		TMap<UControlRig*, TArray<FRigElementKey>> SelectedControls;
		EditMode->GetAllSelectedControls(SelectedControls);
		for (TPair<UControlRig*, TArray<FRigElementKey>>& Selected: SelectedControls)
		{
			ControlRigs.Add(Selected.Key);
		}
	}
	return ControlRigs;
}

#undef LOCTEXT_NAMESPACE
