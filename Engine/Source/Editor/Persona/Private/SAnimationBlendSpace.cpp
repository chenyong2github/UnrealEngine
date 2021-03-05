// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimationBlendSpace.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Editor.h"

#include "SlateOptMacros.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ScopedTransaction.h"
#include "AnimPreviewInstance.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "SAnimationBlendSpaceGridWidget.h"

#define LOCTEXT_NAMESPACE "BlendSpaceEditor"

SBlendSpaceEditor::~SBlendSpaceEditor()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandleDelegateHandle);
}

void SBlendSpaceEditor::Construct(const FArguments& InArgs)
{
	BlendSpace = InArgs._BlendSpace;

	OnBlendSpaceSampleAdded = InArgs._OnBlendSpaceSampleAdded;
	OnBlendSpaceSampleRemoved = InArgs._OnBlendSpaceSampleRemoved;
	OnBlendSpaceSampleReplaced = InArgs._OnBlendSpaceSampleReplaced;
	OnSetPreviewPosition = InArgs._OnSetPreviewPosition;

	bShouldSetPreviewPosition = false;

	SAnimEditorBase::Construct(SAnimEditorBase::FArguments()
		.DisplayAnimTimeline(false)
		.DisplayAnimScrubBar(InArgs._DisplayScrubBar),
		PreviewScenePtr.Pin());

	NonScrollEditorPanels->AddSlot()
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1)
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(1)
					.Padding(2) 
					[
						SNew(SVerticalBox)
						// Grid area
						+SVerticalBox::Slot()
						.FillHeight(1)
						[
							SAssignNew(NewBlendSpaceGridWidget, SBlendSpaceGridWidget)
							.Cursor(EMouseCursor::Crosshairs)
							.BlendSpaceBase(BlendSpace)
							.NotifyHook(this)
							.Position(InArgs._PreviewPosition)
							.FilteredPosition(InArgs._PreviewFilteredPosition)
							.OnSampleMoved(this, &SBlendSpaceEditor::OnSampleMoved)
							.OnSampleRemoved(this, &SBlendSpaceEditor::OnSampleRemoved)
							.OnSampleAdded(this, &SBlendSpaceEditor::OnSampleAdded)
							.OnSampleReplaced(this, &SBlendSpaceEditor::OnSampleReplaced)
							.OnSampleDoubleClicked(InArgs._OnBlendSpaceSampleDoubleClicked)
							.OnExtendSampleTooltip(InArgs._OnExtendSampleTooltip)
							.OnGetBlendSpaceSampleName(InArgs._OnGetBlendSpaceSampleName)
							.StatusBarName(InArgs._StatusBarName)
						]
					]
				]
			]
		]
	];

	OnPropertyChangedHandle = FCoreUObjectDelegates::FOnObjectPropertyChanged::FDelegate::CreateRaw(this, &SBlendSpaceEditor::OnPropertyChanged);
	OnPropertyChangedHandleDelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.Add(OnPropertyChangedHandle);

	// Force a resampling of the data on construction - it should be fast, and ensures that the
	// runtime data are in sync with what is in the editor, so there's no surprises if the user
	// changes something. 
	ResampleData();
}

void SBlendSpaceEditor::Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene)
{
	PreviewScenePtr = InPreviewScene;

	Construct(InArgs);
}

void SBlendSpaceEditor::OnSampleMoved(const int32 SampleIndex, const FVector& NewValue, bool bIsInteractive)
{
	bool bMoveSuccesful = true;
	if (BlendSpace->IsValidBlendSampleIndex(SampleIndex) && BlendSpace->GetBlendSample(SampleIndex).SampleValue != NewValue && !BlendSpace->IsTooCloseToExistingSamplePoint(NewValue, SampleIndex))
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("MoveSample", "Moving Blend Grid Sample"));
		BlendSpace->Modify();

		bMoveSuccesful = BlendSpace->EditSampleValue(SampleIndex, NewValue);
		if (bMoveSuccesful)
		{
			BlendSpace->ValidateSampleData();
			ResampleData();
		}
	}
}

void SBlendSpaceEditor::OnSampleRemoved(const int32 SampleIndex)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("RemoveSample", "Removing Blend Grid Sample"));
	BlendSpace->Modify();

	const bool bRemoveSuccesful = BlendSpace->DeleteSample(SampleIndex);
	if (bRemoveSuccesful)
	{
		ResampleData();
		BlendSpace->ValidateSampleData();

		OnBlendSpaceSampleRemoved.ExecuteIfBound(SampleIndex);
	}
	BlendSpace->PostEditChange();
}

void SBlendSpaceEditor::OnSampleAdded(UAnimSequence* Animation, const FVector& Value)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("AddSample", "Adding Blend Grid Sample"));
	BlendSpace->Modify();

	bool bAddSuccesful = false;

	if(BlendSpace->IsAsset())
	{
		bAddSuccesful = BlendSpace->AddSample(Animation, Value);
	}
	else
	{
		bAddSuccesful = BlendSpace->AddSample(Value);
	}

	if (bAddSuccesful)
	{
		ResampleData();
		BlendSpace->ValidateSampleData();

		OnBlendSpaceSampleAdded.ExecuteIfBound(Animation, Value);
	}
	BlendSpace->PostEditChange();
}

void SBlendSpaceEditor::OnSampleReplaced(int32 InSampleIndex, UAnimSequence* Animation)
{
	FScopedTransaction ScopedTransaction(LOCTEXT("UpdateAnimation", "Changing Animation Sequence"));
	BlendSpace->Modify();

	bool bUpdateSuccesful = false;
	if(BlendSpace->IsAsset())
	{
		bUpdateSuccesful = BlendSpace->ReplaceSampleAnimation(InSampleIndex, Animation);
	}
	else
	{
		bUpdateSuccesful = true;
	}

	if (bUpdateSuccesful)
	{
		ResampleData();
		BlendSpace->ValidateSampleData();

		OnBlendSpaceSampleReplaced.ExecuteIfBound(InSampleIndex, Animation);
	}
}

void SBlendSpaceEditor::PostUndoRedo()
{
	// Validate and resample blend space data
	BlendSpace->ValidateSampleData();
	ResampleData();

	// Invalidate widget data
	NewBlendSpaceGridWidget->InvalidateCachedData();

	// Invalidate sample indices used for UI info
	NewBlendSpaceGridWidget->InvalidateState();

	// Set flag which will update the preview value in the next tick (this due the recreation of data after Undo)
	bShouldSetPreviewPosition = true;
}

TSharedPtr<class IPersonaPreviewScene> SBlendSpaceEditor::GetPreviewScene() const
{
	return PreviewScenePtr.Pin();
}

void SBlendSpaceEditor::UpdatePreviewParameter() const
{
	if(GetPreviewScene().IsValid())
	{
		class UDebugSkelMeshComponent* Component = GetPreviewScene()->GetPreviewMeshComponent();

		if (Component != nullptr && Component->IsPreviewOn())
		{
			if (Component->PreviewInstance->GetCurrentAsset() == BlendSpace)
			{
				const FVector PreviewPosition = NewBlendSpaceGridWidget->GetPreviewPosition();
				Component->PreviewInstance->SetBlendSpacePosition(PreviewPosition);
				GetPreviewScene()->InvalidateViews();			
			}
		}
	}
	else if(OnSetPreviewPosition.IsBound())
	{
		const FVector PreviewPosition = NewBlendSpaceGridWidget->GetPreviewPosition();
		OnSetPreviewPosition.Execute(PreviewPosition);
	}
}

void SBlendSpaceEditor::UpdateFromBlendSpaceState() const
{
	if (GetPreviewScene().IsValid())
	{
		class UDebugSkelMeshComponent* Component = GetPreviewScene()->GetPreviewMeshComponent();

		if (Component != nullptr && Component->IsPreviewOn())
		{
			if (Component->PreviewInstance->GetCurrentAsset() == BlendSpace)
			{
				FVector FilteredPosition;
				FVector Position;
				Component->PreviewInstance->GetBlendSpaceState(Position, FilteredPosition);
				NewBlendSpaceGridWidget->SetPreviewingState(Position, FilteredPosition);
			}
		}
	}
}

void SBlendSpaceEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Update the preview as long as its enabled
	if (NewBlendSpaceGridWidget->IsPreviewing() || bShouldSetPreviewPosition)
	{
		UpdatePreviewParameter();
		bShouldSetPreviewPosition = false;
	}

	UpdateFromBlendSpaceState();
}

void SBlendSpaceEditor::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (ObjectBeingModified == BlendSpace)
	{
		BlendSpace->ValidateSampleData();
		ResampleData();
		NewBlendSpaceGridWidget->InvalidateCachedData();
	}
}

void SBlendSpaceEditor::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (BlendSpace)
	{
		BlendSpace->Modify();
	}	
}

void SBlendSpaceEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (BlendSpace)
	{
		BlendSpace->ValidateSampleData();
		ResampleData();
		BlendSpace->MarkPackageDirty();
	}
}

void SBlendSpaceEditor::ResampleData()
{
	if (BlendSpace)
	{
		BlendSpace->ResampleData();
	}
}

#undef LOCTEXT_NAMESPACE
