// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimationBlendSpaceBase.h"
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

#define LOCTEXT_NAMESPACE "BlendSpaceEditorBase"

SBlendSpaceEditorBase::~SBlendSpaceEditorBase()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandleDelegateHandle);
}

void SBlendSpaceEditorBase::Construct(const FArguments& InArgs)
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
							.OnSampleMoved(this, &SBlendSpaceEditorBase::OnSampleMoved)
							.OnSampleRemoved(this, &SBlendSpaceEditorBase::OnSampleRemoved)
							.OnSampleAdded(this, &SBlendSpaceEditorBase::OnSampleAdded)
							.OnSampleReplaced(this, &SBlendSpaceEditorBase::OnSampleReplaced)
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

	OnPropertyChangedHandle = FCoreUObjectDelegates::FOnObjectPropertyChanged::FDelegate::CreateRaw(this, &SBlendSpaceEditorBase::OnPropertyChanged);
	OnPropertyChangedHandleDelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.Add(OnPropertyChangedHandle);
}

void SBlendSpaceEditorBase::Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene)
{
	PreviewScenePtr = InPreviewScene;

	Construct(InArgs);
}

void SBlendSpaceEditorBase::OnSampleMoved(const int32 SampleIndex, const FVector& NewValue, bool bIsInteractive, bool bSnap)
{
	bool bMoveSuccesful = true;
	if (BlendSpace->IsValidBlendSampleIndex(SampleIndex) && BlendSpace->GetBlendSample(SampleIndex).SampleValue != NewValue && !BlendSpace->IsTooCloseToExistingSamplePoint(NewValue, SampleIndex))
	{
		FScopedTransaction ScopedTransaction(LOCTEXT("MoveSample", "Moving Blend Grid Sample"));
		BlendSpace->Modify();

		bMoveSuccesful = BlendSpace->EditSampleValue(SampleIndex, NewValue, bSnap);
		if (bMoveSuccesful)
		{
			BlendSpace->ValidateSampleData();
			ResampleData();
		}
	}
}

void SBlendSpaceEditorBase::OnSampleRemoved(const int32 SampleIndex)
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

void SBlendSpaceEditorBase::OnSampleAdded(UAnimSequence* Animation, const FVector& Value)
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

void SBlendSpaceEditorBase::OnSampleReplaced(int32 InSampleIndex, UAnimSequence* Animation)
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

void SBlendSpaceEditorBase::PostUndoRedo()
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

TSharedPtr<class IPersonaPreviewScene> SBlendSpaceEditorBase::GetPreviewScene() const
{
	return PreviewScenePtr.Pin();
}

void SBlendSpaceEditorBase::UpdatePreviewParameter() const
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

void SBlendSpaceEditorBase::UpdateFromBlendSpaceState() const
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

void SBlendSpaceEditorBase::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Update the preview as long as its enabled
	if (NewBlendSpaceGridWidget->IsPreviewing() || bShouldSetPreviewPosition)
	{
		UpdatePreviewParameter();
		bShouldSetPreviewPosition = false;
	}

	UpdateFromBlendSpaceState();
}

void SBlendSpaceEditorBase::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (ObjectBeingModified == BlendSpace)
	{
		BlendSpace->ValidateSampleData();
		ResampleData();
		NewBlendSpaceGridWidget->InvalidateCachedData();
	}
}

void SBlendSpaceEditorBase::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (BlendSpace)
	{
		BlendSpace->Modify();
	}	
}

void SBlendSpaceEditorBase::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (BlendSpace)
	{
		BlendSpace->ValidateSampleData();
		ResampleData();
		BlendSpace->MarkPackageDirty();
	}
}

#undef LOCTEXT_NAMESPACE
