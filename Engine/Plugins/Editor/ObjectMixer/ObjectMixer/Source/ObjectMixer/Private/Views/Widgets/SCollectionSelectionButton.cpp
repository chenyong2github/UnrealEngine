// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Widgets/SCollectionSelectionButton.h"

#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Styling/AppStyle.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"
#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

class FCollectionSelectionButtonDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCollectionSelectionButtonDragDropOp, FDecoratedDragDropOp)

	/** The item being dragged and dropped */
	FName DraggedItem;

	/** Constructs a new drag/drop operation */
	static TSharedRef<FCollectionSelectionButtonDragDropOp> New(const FName DraggedItem)
	{
		TSharedRef<FCollectionSelectionButtonDragDropOp> Operation = MakeShareable(
			   new FCollectionSelectionButtonDragDropOp());
		if (DraggedItem != NAME_None)
		{
			Operation->DraggedItem = DraggedItem;

			Operation->DefaultHoverIcon = FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.Error");

			Operation->DefaultHoverText = LOCTEXT("DefaultCollectionButtonHoverText","Drop onto another Collection Button to set a custom order.");

			Operation->Construct();
		}

		return Operation;
	}
};

void SCollectionSelectionButton::Construct(const FArguments& InArgs, const TSharedRef<SObjectMixerEditorMainPanel> MainPanelWidget, const FName& InCollectionName)
{
	MainPanelPtr = MainPanelWidget;
	CollectionName = InCollectionName;
	
	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(16, 4))
		.BorderImage(this, &SCollectionSelectionButton::GetBorderBrush)
		.ForegroundColor(this, &SCollectionSelectionButton::GetBorderForeground)
		[
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "SmallText")
			.Text(FText::FromName(CollectionName))
			.Visibility(EVisibility::HitTestInvisible)
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
}

FReply SCollectionSelectionButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) 
{
	bIsPressed = true;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		return FReply::Handled().DetectDrag( SharedThis(this), EKeys::LeftMouseButton );
	}
	return FReply::Handled();
}

FReply SCollectionSelectionButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) 
{
	bIsPressed = false;
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		ECheckBoxState CurrentState = MainPanelPtr.Pin()->IsCollectionChecked(CollectionName);

		ECheckBoxState NewState = ECheckBoxState::Checked;

		switch (CurrentState)
		{
		case ECheckBoxState::Checked:
			NewState = ECheckBoxState::Unchecked;
			break;

		default:
			break;
		}
		
		MainPanelPtr.Pin()->OnCollectionCheckedStateChanged(NewState, CollectionName);
	}
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		MainPanelPtr.Pin()->RequestRemoveCollection(CollectionName);
	}
	return FReply::Handled();
}

FReply SCollectionSelectionButton::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (CollectionName != "All")
	{
		TSharedRef<FCollectionSelectionButtonDragDropOp> OperationFromCollection =
		   FCollectionSelectionButtonDragDropOp::New(CollectionName);

		OperationFromCollection->ResetToDefaultToolTip();

		bDropIsValid = false;

		return FReply::Handled().BeginDragDrop(OperationFromCollection);
	}

	return FReply::Handled();
}

void SCollectionSelectionButton::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FObjectMixerListRowDragDropOp> OperationFromRow =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>())
	{
		if (CollectionName != "All" && CollectionName != NAME_None)
		{
			OperationFromRow->SetToolTip(
			   LOCTEXT("DropRowItemsOntoCollectionButtonCTA","Add selected items to this collection"),
			   FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OK"));

			bDropIsValid = true;
		}
	}

	if (TSharedPtr<FCollectionSelectionButtonDragDropOp> OperationFromCollection =
		DragDropEvent.GetOperationAs<FCollectionSelectionButtonDragDropOp>())
	{
		if (CollectionName != "All" && CollectionName != NAME_None && CollectionName != OperationFromCollection->DraggedItem)
		{
			OperationFromCollection->SetToolTip(
			   FText::Format(
				LOCTEXT("DropCollectionButtonOntoCollectionButtonCTA_Format","Reorder {0} before {1}"),
				FText::FromName(OperationFromCollection->DraggedItem), FText::FromName(CollectionName)),
			   FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OK"));

			bDropIsValid = true;
		}
	}
}

void SCollectionSelectionButton::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bIsPressed = false;
	bDropIsValid = false;
	
	if (TSharedPtr<FObjectMixerListRowDragDropOp> OperationFromRow =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>())
	{
		OperationFromRow->ResetToDefaultToolTip();
	}

	if (TSharedPtr<FCollectionSelectionButtonDragDropOp> OperationFromCollection =
		DragDropEvent.GetOperationAs<FCollectionSelectionButtonDragDropOp>())
	{
		OperationFromCollection->ResetToDefaultToolTip();
	}
}

FReply SCollectionSelectionButton::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bIsPressed = false;
	bDropIsValid = false;
	
	if (TSharedPtr<FObjectMixerListRowDragDropOp> OperationFromRow =
		DragDropEvent.GetOperationAs<FObjectMixerListRowDragDropOp>())
	{
		if (CollectionName != "All" && CollectionName != NAME_None)
		{
			if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanelModel = MainPanelPtr.Pin()->GetMainPanelModel().Pin())
			{
				TSet<FSoftObjectPath> ObjectPaths;

				for (const TSharedPtr<FObjectMixerEditorListRow>& Item : OperationFromRow->DraggedItems)
				{
					if (const UObject* Object = Item->GetObject())
					{
						ObjectPaths.Add(Object);
					}
				}
	
				MainPanelModel->AddObjectsToCollection(CollectionName, ObjectPaths);
			}
		}
	}

	if (TSharedPtr<FCollectionSelectionButtonDragDropOp> OperationFromCollection =
		DragDropEvent.GetOperationAs<FCollectionSelectionButtonDragDropOp>())
	{
		if (CollectionName != "All" && CollectionName != NAME_None && CollectionName != OperationFromCollection->DraggedItem)
		{
			if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanelModel = MainPanelPtr.Pin()->GetMainPanelModel().Pin())
			{
				MainPanelModel->ReorderCollection(OperationFromCollection->DraggedItem, CollectionName);
			}
		}
	}
		
	return FReply::Handled();
}

const FSlateBrush* SCollectionSelectionButton::GetBorderBrush() const
{
	if (MainPanelPtr.Pin()->IsCollectionChecked(CollectionName) == ECheckBoxState::Checked)
	{
		if (bIsPressed)
		{
			return &CheckedPressedImage;
		}
		else if (IsHovered())
		{
			return &CheckedHoveredImage;
		}

		return &CheckedImage;
	}

	if (bIsPressed)
	{
		return &UncheckedPressedImage;
	}
	else if (IsHovered())
	{
		return &UncheckedHoveredImage;
	}

	return &UncheckedImage;
			
}

FSlateColor SCollectionSelectionButton::GetBorderForeground() const
{
	if (MainPanelPtr.Pin()->IsCollectionChecked(CollectionName) == ECheckBoxState::Checked ||
		bIsPressed || IsHovered())
	{
		return FStyleColors::White;
	}

	return FStyleColors::Foreground;
}

#undef LOCTEXT_NAMESPACE
