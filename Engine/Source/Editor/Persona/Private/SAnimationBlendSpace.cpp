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
#include "AnimationBlendSpaceHelpers.h"
#include "AnimationBlendSpace1DHelpers.h"
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
}

void SBlendSpaceEditor::Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene)
{
	PreviewScenePtr = InPreviewScene;

	Construct(InArgs);
}

void SBlendSpaceEditor::OnSampleMoved(const int32 SampleIndex, const FVector& NewValue, bool bIsInteractive, bool bSnap)
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
	const TArray<FBlendSample>& BlendSamples = BlendSpace->GetBlendSamples();
	FBox AABB(ForceInit);
	for (const FBlendSample& Sample : BlendSamples)
	{
		// Add X value from sample (this is the only valid value to be set for 1D blend spaces / aim offsets
		if (Sample.bIsValid)
		{
			AABB += Sample.SampleValue;
		}
	}

	TArray<int32> DimensionIndices;
	DimensionIndices.Reset(3);
	if (AABB.GetExtent().X > 0.0f)
	{
		DimensionIndices.Push(0);
	}
	if (AABB.GetExtent().Y > 0.0f)
	{
		DimensionIndices.Push(1);
	}
	if (AABB.GetExtent().Z > 0.0f)
	{
		DimensionIndices.Push(2);
	}

	if (DimensionIndices.Num() == 1)
	{
		ResampleData1D(DimensionIndices);
	}
	else if (DimensionIndices.Num() == 2)
	{
		ResampleData2D(DimensionIndices);
	}
	else
	{
		UE_LOG(LogAnimation, Warning, TEXT("Found %d dimensions from the samples"), DimensionIndices.Num());
	}
}


void SBlendSpaceEditor::ResampleData1D(const TArray<int32>& DimensionIndices)
{
	check(DimensionIndices.Num() == 1);
	FLineElementGenerator LineElementGenerator;

	int32 Index0 = DimensionIndices[0];

	const FBlendParameter& BlendParameter = BlendSpace->GetBlendParameter(Index0);
	LineElementGenerator.Init(BlendParameter);

	const TArray<FBlendSample>& BlendSamples = BlendSpace->GetBlendSamples();
	UE_LOG(LogAnimation, Log, TEXT("Resampling data in 1D - %d samples"), BlendSamples.Num());

	if (BlendSamples.Num())
	{
		for (const FBlendSample& Sample : BlendSamples)
		{
			// Add X value from sample (this is the only valid value to be set for 1D blend spaces / aim offsets
			if (Sample.bIsValid)
			{
				LineElementGenerator.SamplePointList.Add(Sample.SampleValue[Index0]);
			}
		}
		LineElementGenerator.CalculateEditorElements();

		// Create point to sample index list
		TArray<int32> PointListToSampleIndices;
		PointListToSampleIndices.Init(INDEX_NONE, LineElementGenerator.SamplePointList.Num());
		for (int32 PointIndex = 0; PointIndex < LineElementGenerator.SamplePointList.Num(); ++PointIndex)
		{
			const float Point = LineElementGenerator.SamplePointList[PointIndex];
			for (int32 SampleIndex = 0; SampleIndex < BlendSamples.Num(); ++SampleIndex)
			{
				if (BlendSamples[SampleIndex].SampleValue[Index0] == Point)
				{
					PointListToSampleIndices[PointIndex] = SampleIndex;
					break;
				}
			}
		}

		BlendSpace->FillupGridElements(PointListToSampleIndices, LineElementGenerator.EditorElements, DimensionIndices);
	}
}


void SBlendSpaceEditor::ResampleData2D(const TArray<int32>& DimensionIndices)
{
	check(DimensionIndices.Num() == 2);

	// TODO for now, Index0 will always be 0 and Index1 will always be 1, since we don't support 3D.
	// However, if/when we support authoring using a third dimension, we could get here with any 2D
	// combination - e.g. XY, XZ or YZ. Then we can either make the triangulation code handle the
	// indexing, or tweak the triangles going into and out of it.
	int32 Index0 = DimensionIndices[0];
	int32 Index1 = DimensionIndices[1];

	// clear first
	FDelaunayTriangleGenerator DelaunayTriangleGenerator;

	// you don't like to overwrite the link here (between visible points vs sample points, 
	// so allow this if no triangle is generated
	const FBlendParameter& BlendParamX = BlendSpace->GetBlendParameter(Index0);
	const FBlendParameter& BlendParamY = BlendSpace->GetBlendParameter(Index1);
	FBlendSpaceGrid	BlendSpaceGrid;
	BlendSpaceGrid.SetGridInfo(BlendParamX, BlendParamY);
	DelaunayTriangleGenerator.SetGridBox(BlendParamX, BlendParamY);

	BlendSpace->EmptyGridElements();

	UE_LOG(LogAnimation, Log, TEXT("Resampling data in 2D - %d samples"), BlendSpace->GetNumberOfBlendSamples());
	if (BlendSpace->GetNumberOfBlendSamples())
	{
		bool bAllSamplesValid = true;
		for (int32 SampleIndex = 0; SampleIndex < BlendSpace->GetNumberOfBlendSamples(); ++SampleIndex)
		{
			const FBlendSample& Sample = BlendSpace->GetBlendSample(SampleIndex);

			// Do not add invalid sample points (user will need to correct them to be incorporated into the blendspace)
			if (Sample.bIsValid)
			{
				DelaunayTriangleGenerator.AddSamplePoint(Sample.SampleValue, SampleIndex);
			}
		}

		// triangulate
		DelaunayTriangleGenerator.Triangulate();

		// once triangulated, generate grid
		const TArray<FPoint>& Points = DelaunayTriangleGenerator.GetSamplePointList();
		const TArray<FTriangle*>& Triangles = DelaunayTriangleGenerator.GetTriangleList();
		BlendSpaceGrid.GenerateGridElements(Points, Triangles);

		// now fill up grid elements in BlendSpace using this Element information
		if (Triangles.Num() > 0)
		{
			const TArray<FEditorElement>& GridElements = BlendSpaceGrid.GetElements();
			BlendSpace->FillupGridElements(DelaunayTriangleGenerator.GetIndiceMapping(), GridElements, DimensionIndices);
		}
	}
}

#undef LOCTEXT_NAMESPACE
