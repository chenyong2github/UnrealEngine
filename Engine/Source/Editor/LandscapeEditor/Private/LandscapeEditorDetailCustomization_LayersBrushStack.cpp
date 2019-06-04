// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorDetailCustomization_LayersBrushStack.h"
#include "IDetailChildrenBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Brushes/SlateColorBrush.h"
#include "Layout/WidgetPath.h"
#include "SlateOptMacros.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "LandscapeEditorDetailCustomization_Layers.h"

#include "ScopedTransaction.h"

#include "LandscapeEditorDetailCustomization_TargetLayers.h"
#include "Widgets/Input/SEditableText.h"
#include "LandscapeBPCustomBrush.h"

#define LOCTEXT_NAMESPACE "LandscapeEditor.Layers"

TSharedRef<IDetailCustomization> FLandscapeEditorDetailCustomization_LayersBrushStack::MakeInstance()
{
	return MakeShareable(new FLandscapeEditorDetailCustomization_LayersBrushStack);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorDetailCustomization_LayersBrushStack::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LayerCategory = DetailBuilder.EditCategory("Current Layer Brushes");

	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode && LandscapeEdMode->CurrentToolMode != nullptr)
	{
		const FName CurrentToolName = LandscapeEdMode->CurrentTool->GetToolName();

		if (LandscapeEdMode->CurrentToolMode->SupportedTargetTypes != 0 && CurrentToolName == TEXT("BPCustom"))
		{
			LayerCategory.AddCustomBuilder(MakeShareable(new FLandscapeEditorCustomNodeBuilder_LayersBrushStack(DetailBuilder.GetThumbnailPool().ToSharedRef())));
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

//////////////////////////////////////////////////////////////////////////

FEdModeLandscape* FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetEditorMode()
{
	return (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
}

FLandscapeEditorCustomNodeBuilder_LayersBrushStack::FLandscapeEditorCustomNodeBuilder_LayersBrushStack(TSharedRef<FAssetThumbnailPool> InThumbnailPool)
	: ThumbnailPool(InThumbnailPool)
{
}

FLandscapeEditorCustomNodeBuilder_LayersBrushStack::~FLandscapeEditorCustomNodeBuilder_LayersBrushStack()
{
	
}

void FLandscapeEditorCustomNodeBuilder_LayersBrushStack::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
}

void FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	
	if (LandscapeEdMode == NULL)
	{
		return;	
	}

	NodeRow.NameWidget
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::FromString(TEXT("Stack")))
		];
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	if (LandscapeEdMode != NULL)
	{
		TSharedPtr<SDragAndDropVerticalBox> BrushesList = SNew(SDragAndDropVerticalBox)
			.OnCanAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleAcceptDrop)
			.OnDragDetected(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleDragDetected);

		BrushesList->SetDropIndicator_Above(*FEditorStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Above"));
		BrushesList->SetDropIndicator_Below(*FEditorStyle::GetBrush("LandscapeEditor.TargetList.DropZone.Below"));

		ChildrenBuilder.AddCustomRow(FText::FromString(FString(TEXT("Brush Stack"))))
			.Visibility(EVisibility::Visible)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					BrushesList.ToSharedRef()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					//.Padding(4, 0)
					[
						SNew(SButton)
						.Text(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetCommitBrushesButtonText)
						.OnClicked(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::ToggleCommitBrushes)
						.IsEnabled(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::IsCommitBrushesButtonEnabled)
					]
				]
			];

		if (LandscapeEdMode->CurrentToolMode != nullptr)
		{
			const TArray<int8>& BrushOrderStack = LandscapeEdMode->GetBrushesOrderForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType);

			for (int32 i = 0; i < BrushOrderStack.Num(); ++i)
			{
				TSharedPtr<SWidget> GeneratedRowWidget = GenerateRow(i);

				if (GeneratedRowWidget.IsValid())
				{
					BrushesList->AddSlot()
						.AutoHeight()
						[
							GeneratedRowWidget.ToSharedRef()
						];
				}
			}
		}
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedPtr<SWidget> FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GenerateRow(int32 InBrushIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();
	TSharedPtr<SWidget> RowWidget = SNew(SLandscapeEditorSelectableBorder)
		.Padding(0)
		.VAlign(VAlign_Center)
		.OnSelected(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::OnBrushSelectionChanged, InBrushIndex)
		.IsSelected(TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::IsBrushSelected, InBrushIndex)))
		[	
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding(0, 2)
				[
					SNew(STextBlock)
					.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetBrushTextColor, InBrushIndex)))
					.Text(this, &FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetBrushText, InBrushIndex)
				]
			]
		];
	
	return RowWidget;
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool FLandscapeEditorCustomNodeBuilder_LayersBrushStack::IsBrushSelected(int32 InBrushIndex) const
{
	ALandscapeBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	return Brush != nullptr ? Brush->IsSelected() : false;
}

void FLandscapeEditorCustomNodeBuilder_LayersBrushStack::OnBrushSelectionChanged(int32 InBrushIndex)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr && LandscapeEdMode->AreAllBrushesCommitedToCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType))
	{
		return;
	}

	ALandscapeBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr && !Brush->IsCommited())
	{
		GEditor->SelectNone(true, true);
		GEditor->SelectActor(Brush, true, true);
	}
}

FText FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetBrushText(int32 InBrushIndex) const
{
	ALandscapeBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr)
	{
		return FText::FromString(Brush->GetActorLabel());
	}

	return FText::FromName(NAME_None);
}

FSlateColor FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetBrushTextColor(int32 InBrushIndex) const
{
	ALandscapeBlueprintCustomBrush* Brush = GetBrush(InBrushIndex);

	if (Brush != nullptr)
	{
		return Brush->IsCommited() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
	}

	return FSlateColor::UseSubduedForeground();
}

ALandscapeBlueprintCustomBrush* FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetBrush(int32 InBrushIndex) const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		return LandscapeEdMode->GetBrushForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType, InBrushIndex);
	}

	return nullptr;
}

FReply FLandscapeEditorCustomNodeBuilder_LayersBrushStack::ToggleCommitBrushes()
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		bool CommitBrushes = !LandscapeEdMode->AreAllBrushesCommitedToCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType);

		if (CommitBrushes)
		{
			TArray<ALandscapeBlueprintCustomBrush*> BrushStack = LandscapeEdMode->GetBrushesForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType);

			for (ALandscapeBlueprintCustomBrush* Brush : BrushStack)
			{
				GEditor->SelectActor(Brush, false, true);
			}
		}

		LandscapeEdMode->SetBrushesCommitStateForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType, CommitBrushes);
	}

	return FReply::Handled();
}

bool FLandscapeEditorCustomNodeBuilder_LayersBrushStack::IsCommitBrushesButtonEnabled() const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		TArray<ALandscapeBlueprintCustomBrush*> BrushStack = LandscapeEdMode->GetBrushesForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType);

		return BrushStack.Num() > 0;
	}

	return false;
}

FText FLandscapeEditorCustomNodeBuilder_LayersBrushStack::GetCommitBrushesButtonText() const
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		return LandscapeEdMode->AreAllBrushesCommitedToCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType) ? LOCTEXT("UnCommitBrushesText", "Uncommit") : LOCTEXT("CommitBrushesText", "Commit");
	}

	return FText::FromName(NAME_None);
}

FReply FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	FEdModeLandscape* LandscapeEdMode = GetEditorMode();

	if (LandscapeEdMode != nullptr)
	{
		const TArray<int8>& BrushOrderStack = LandscapeEdMode->GetBrushesOrderForCurrentLayer(LandscapeEdMode->CurrentToolTarget.TargetType);

		if (BrushOrderStack.IsValidIndex(SlotIndex))
		{
			TSharedPtr<SWidget> Row = GenerateRow(SlotIndex);

			if (Row.IsValid())
			{
				return FReply::Handled().BeginDragDrop(FLandscapeListElementDragDropOp::New(SlotIndex, Slot, Row));
			}
		}
	}

	return FReply::Unhandled();
}

TOptional<SDragAndDropVerticalBox::EItemDropZone> FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FLandscapeListElementDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FLandscapeListElementDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		return DropZone;
	}

	return TOptional<SDragAndDropVerticalBox::EItemDropZone>();
}

FReply FLandscapeEditorCustomNodeBuilder_LayersBrushStack::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone DropZone, int32 SlotIndex, SVerticalBox::FSlot* Slot)
{
	TSharedPtr<FLandscapeListElementDragDropOp> DragDropOperation = DragDropEvent.GetOperationAs<FLandscapeListElementDragDropOp>();

	if (DragDropOperation.IsValid())
	{
		FEdModeLandscape* LandscapeEdMode = GetEditorMode();
		ALandscape* Landscape = LandscapeEdMode ? LandscapeEdMode->GetLandscape() : nullptr;
		if (Landscape)
		{
			int32 StartingLayerIndex = DragDropOperation->SlotIndexBeingDragged;
			int32 DestinationLayerIndex = SlotIndex;
			const FScopedTransaction Transaction(LOCTEXT("Landscape_LayerBrushes_Reorder", "Reorder Layer Brush"));
			if (Landscape->ReorderLayerBrush(LandscapeEdMode->GetCurrentLayerIndex(), LandscapeEdMode->CurrentToolTarget.TargetType, StartingLayerIndex, DestinationLayerIndex))
			{
				LandscapeEdMode->RefreshDetailPanel();
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE