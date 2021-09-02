// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchTreeFixturePatchRow.h"

#include "DMXEditorUtils.h"
#include "DMXEditorStyle.h"
#include "SDMXFixturePatchTree.h"
#include "DragDrop/DMXEntityDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Widgets/DMXEntityTreeNode.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXFixturePatchTreeFixturePatchRow"

void SDMXFixturePatchTreeFixturePatchRow::Construct(const FArguments& InArgs, TSharedPtr<FDMXEntityTreeEntityNode> InEntityNode, TSharedPtr<STableViewBase> InOwnerTableView, TWeakPtr<SDMXFixturePatchTree> InFixturePatchTree)
{
	// Without ETableRowSignalSelectionMode::Instantaneous, when the user is editing a property in
	// the inspector panel and then clicks on a different row on the list panel, the selection event
	// is deferred. But because we update the tree right after a property change and that triggers
	// selection changes too, the selection change event is triggered only from UpdateTree, with
	// Direct selection mode, which doesn't trigger the SDMXEntityTree::OnSelectionUpdated event.
	// This setting forces the event with OnMouseClick selection type to be fired as soon as the
	// row is clicked.
	STableRow<TSharedPtr<FDMXEntityTreeEntityNode>>::FArguments BaseArguments =
		STableRow<TSharedPtr<FDMXEntityTreeEntityNode>>::FArguments()
		.SignalSelectionMode(ETableRowSignalSelectionMode::Instantaneous)
		.OnDragDetected(this, &SDMXFixturePatchTreeFixturePatchRow::HandleOnDragDetected);

	STableRow<TSharedPtr<FDMXEntityTreeEntityNode>>::Construct(BaseArguments, InOwnerTableView.ToSharedRef());

	WeakEntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(InEntityNode);
	WeakFixturePatchTree = InFixturePatchTree;

	OnEntityDragged = InArgs._OnEntityDragged;
	OnGetFilterText = InArgs._OnGetFilterText;
	OnFixturePatchOrderChanged = InArgs._OnFixturePatchOrderChanged;
	OnAutoAssignChannelStateChanged = InArgs._OnAutoAssignChannelStateChanged;

	const FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);

	// Used address range labels
	const FSlateFontInfo&& ChannelFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FLinearColor ChannelLabelColor(1.0f, 1.0f, 1.0f, 0.8f);
	const float MinChannelTextWidth = 20.0f;

	SetContent
	(
		SNew(SHorizontalBox)

		// Status icon to show the user if there's an error with the Entity's usability
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(this, &SDMXFixturePatchTreeFixturePatchRow::GetStatusIcon)
			.ToolTipText(this, &SDMXFixturePatchTreeFixturePatchRow::GetStatusToolTip)
		]
		
		// Entity's name
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(3.0f, 0.0f)
		[
			SAssignNew(InlineEditableFixturePatchNameWidget, SInlineEditableTextBlock)
			.Text(this, &SDMXFixturePatchTreeFixturePatchRow::GetFixturePatchDisplayText)
			.Font(NameFont)
			.HighlightText(this, &SDMXFixturePatchTreeFixturePatchRow::GetFilterText)
			.OnTextCommitted(this, &SDMXFixturePatchTreeFixturePatchRow::OnNameTextCommit)
			.OnVerifyTextChanged(this, &SDMXFixturePatchTreeFixturePatchRow::OnNameTextVerifyChanged)
			.IsSelected(this, &SDMXFixturePatchTreeFixturePatchRow::IsSelected)
			.IsReadOnly(false)
		]

		// Auto channel assignment check box
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SDMXFixturePatchTreeFixturePatchRow::IsAutoAssignChannelEnabled)
			.OnCheckStateChanged(this, &SDMXFixturePatchTreeFixturePatchRow::OnAutoAssignChannelBoxStateChanged)
			.ToolTipText(LOCTEXT("AutoAssignChannelToolTip", "Auto-assign address from drag/drop list order"))
		]

		// Starting channel number
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 3.0f))
			.BorderImage(FDMXEditorStyle::Get().GetBrush("DMXEditor.BlackBrush"))
			.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.15f))
			.ToolTipText(LOCTEXT("ChannelStartToolTip", "Channels range: start"))
			[
				SNew(STextBlock)
				.Text(this, &SDMXFixturePatchTreeFixturePatchRow::GetStartingChannelText)
				.Font(ChannelFont)
				.ColorAndOpacity(ChannelLabelColor)
				.MinDesiredWidth(MinChannelTextWidth)
				.Justification(ETextJustify::Center)
			]
		]

		// Ending channel number
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.0f, 3.0f))
			.BorderImage(FDMXEditorStyle::Get().GetBrush("DMXEditor.BlackBrush"))
			.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.25f)) // darker background
			.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f)) // darker text
			.ToolTipText(LOCTEXT("ChannelEndToolTip", "Channels range: end"))
			[
				SNew(STextBlock)
				.Text(this, &SDMXFixturePatchTreeFixturePatchRow::GetEndingChannelText)
				.Font(ChannelFont)
				.ColorAndOpacity(ChannelLabelColor)
				.MinDesiredWidth(MinChannelTextWidth)
				.Justification(ETextJustify::Center)
			]
		]
	);
}

void SDMXFixturePatchTreeFixturePatchRow::EnterRenameMode()
{
	InlineEditableFixturePatchNameWidget->EnterEditingMode();
}

FReply SDMXFixturePatchTreeFixturePatchRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	const TSharedPtr<SDMXFixturePatchTree> FixturePatchTree = WeakFixturePatchTree.Pin();
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin();

	if (FixturePatchTree.IsValid() && EntityNode.IsValid())
	{
		FixturePatchTree->SelectItemByNode(EntityNode.ToSharedRef());

		InlineEditableFixturePatchNameWidget->EnterEditingMode();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDMXFixturePatchTreeFixturePatchRow::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (const TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{
		TestCanDropWithFeedback(EntityDragDropOp.ToSharedRef());
	}
}

FReply SDMXFixturePatchTreeFixturePatchRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>();
	const TSharedPtr<SDMXFixturePatchTree> FixturePatchTree = WeakFixturePatchTree.Pin();
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin();
	UDMXEntityFixturePatch* FixturePatch = EntityNode.IsValid() ? Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()) : nullptr;

	if (EntityDragDropOp.IsValid() && FixturePatchTree.IsValid() && EntityNode.IsValid() && FixturePatch)
	{
		if (TestCanDropWithFeedback(EntityDragDropOp.ToSharedRef()))
		{
			const TArray<TWeakObjectPtr<UDMXEntity>> DraggedEntities = EntityDragDropOp->GetDraggedEntities();

			// Register transaction and current DMX library state for Undo
			const FScopedTransaction ReorderTransaction = FScopedTransaction(
				FText::Format(LOCTEXT("DropFixturePatchOnFixturePatch", "Assign Fixture {0}|plural(one=Patch, other=Patches)"), DraggedEntities.Num())
			);

			UDMXLibrary* DMXLibrary = GetDMXLibrary();
			DMXLibrary->Modify();

			// The index of the Entity, we're about to insert the dragged one before
			const int32 InsertBeforeIndex = DMXLibrary->FindEntityIndex(FixturePatch);
			check(InsertBeforeIndex != INDEX_NONE);

			// Reverse for to keep dragged entities order
			for (int32 EntityIndex = DraggedEntities.Num() - 1; EntityIndex > -1; --EntityIndex)
			{
				if (DraggedEntities[EntityIndex].IsValid())
				{
					UDMXEntity* DraggedEntity = DraggedEntities[EntityIndex].Get();
					DMXLibrary->SetEntityIndex(DraggedEntity, InsertBeforeIndex);
				}
			}

			TSharedPtr<FDMXEntityTreeCategoryNode> TargetCategory = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(EntityNode->GetParent().Pin());
			const uint32 UniverseID = TargetCategory->GetIntValue();

			for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
			{
				if (UDMXEntityFixturePatch* DraggedFixturePatch = Cast<UDMXEntityFixturePatch>(Entity))
				{
					DraggedFixturePatch->Modify();
					DraggedFixturePatch->PreEditChange(nullptr);

					FDMXEditorUtils::AutoAssignedAddresses(TArray<UDMXEntityFixturePatch*>({ DraggedFixturePatch }));
					DraggedFixturePatch->SetUniverseID(UniverseID);

					DraggedFixturePatch->PostEditChange();
				}
			}

			// Display the changes in the Entity Tree
			FixturePatchTree->UpdateTree();

			OnFixturePatchOrderChanged.ExecuteIfBound();

			return FReply::Handled().EndDragDrop();
		}
	}

	return FReply::Unhandled();
}

bool SDMXFixturePatchTreeFixturePatchRow::TestCanDropWithFeedback(const TSharedRef<FDMXEntityDragDropOperation>& EntityDragDropOp) const
{
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin();
	const UDMXEntityFixturePatch* FixturePatch = EntityNode.IsValid() ? Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()) : nullptr;

	if (EntityNode.IsValid() && FixturePatch)
	{
		const TArray<TWeakObjectPtr<UDMXEntity>> DraggedEntities = EntityDragDropOp->GetDraggedEntities();

		const bool bOnlyFixturePatchesAreDragged = [DraggedEntities]()
		{
			for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
			{
				if (Entity.IsValid() && Entity->GetClass() != UDMXEntityFixturePatch::StaticClass())
				{
					return false;
				}
			}
			return true;
		}();

		const bool bRowAndEntitiesAreOfSameDMXLibrary = [DraggedEntities, this]()
		{
			UDMXLibrary* DMXLibrary = GetDMXLibrary();
			for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
			{
				if (Entity.IsValid() && Entity->GetParentLibrary() != DMXLibrary)
				{
					return false;
				}
			}
			return true;
		}();

		if (bOnlyFixturePatchesAreDragged && bRowAndEntitiesAreOfSameDMXLibrary)
		{
			const bool bDragBetweenUniverses = [DraggedEntities, FixturePatch]()
			{
				for (TWeakObjectPtr<UDMXEntity> DraggedEntity : DraggedEntities)
				{
					if (UDMXEntityFixturePatch* DraggedFixturePatch = Cast<UDMXEntityFixturePatch>(DraggedEntity))
					{
						if (DraggedFixturePatch->GetUniverseID() != FixturePatch->GetUniverseID())
						{
							return true;
						}
					}
				}
				return false;
			}();

			if (DraggedEntities.Contains(FixturePatch))
			{
				// The fixture patch of this row is a dragged fixture patch
				EntityDragDropOp->SetFeedbackMessageError(FText::Format(
					LOCTEXT("ReorderBeforeItself", "Drop {0} on itself"),
					EntityDragDropOp->GetDraggedEntitiesName()
				));

				return false;
			}
			else if (!bDragBetweenUniverses)
			{
				EntityDragDropOp->SetFeedbackMessageError(FText::Format(
					LOCTEXT("DragFixturePatchToItsOwnUniverse", "{0} already resides in this universe."),
					EntityDragDropOp->GetDraggedEntitiesName()
				));

				return false;
			}
			else
			{
				TSharedPtr<FDMXEntityTreeCategoryNode> TargetCategory = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(EntityNode->GetParent().Pin());

				const FText	ListTypeText = LOCTEXT("Property_Universe", "Universe");
				const int32 UniverseID = TargetCategory->GetIntValue();
				const FText	CategoryText = UniverseID == -1
					? LOCTEXT("UnassignedUniverseIDValue", "Unassigned")
					: FText::AsNumber(UniverseID);

				EntityDragDropOp->SetFeedbackMessageOK(FText::Format(
					LOCTEXT("ReorderBeforeAndSetProperty", "Reorder {0} before '{1}'\nSet {2} = '{3}'"),
					EntityDragDropOp->GetDraggedEntitiesName(),
					FText::FromString(FixturePatch->GetDisplayName()),
					ListTypeText,
					CategoryText
				));

				return true;
			}
		}
	}

	return false;
}

FText SDMXFixturePatchTreeFixturePatchRow::GetFixturePatchDisplayText() const
{
	if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		return EntityNode->GetDisplayNameText();
	}

	return FText::GetEmpty();
}

FText SDMXFixturePatchTreeFixturePatchRow::GetStartingChannelText() const
{
	if (TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		if (UDMXEntityFixturePatch* Patch = Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()))
		{
			return FText::AsNumber(Patch->GetStartingChannel());
		}
	}

	return FText::GetEmpty();
}

FText SDMXFixturePatchTreeFixturePatchRow::GetEndingChannelText() const
{
	if (TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		if (UDMXEntityFixturePatch* Patch = Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()))
		{
			return FText::AsNumber(Patch->GetStartingChannel() + Patch->GetChannelSpan() - 1);
		}
	}

	return FText::GetEmpty();
}

void SDMXFixturePatchTreeFixturePatchRow::OnAutoAssignChannelBoxStateChanged(ECheckBoxState NewState)
{
	if (OnAutoAssignChannelStateChanged.IsBound())
	{
		switch (NewState)
		{
		case ECheckBoxState::Unchecked:
			OnAutoAssignChannelStateChanged.Execute(false);
			break;
		case ECheckBoxState::Checked:
			OnAutoAssignChannelStateChanged.Execute(true);
			break;
		case ECheckBoxState::Undetermined:
		default:
			break;
		}
	}
}

FText SDMXFixturePatchTreeFixturePatchRow::GetFilterText() const
{
	if (OnGetFilterText.IsBound())
	{
		return OnGetFilterText.Execute();
	}
	return FText();
}

void SDMXFixturePatchTreeFixturePatchRow::OnNameTextCommit(const FText& InNewName, ETextCommit::Type InTextCommit)
{
	const TSharedPtr<SDMXFixturePatchTree> FixturePatchTree = WeakFixturePatchTree.Pin();
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin();

	if (FixturePatchTree.IsValid() && EntityNode.IsValid())
	{
		const FString NewNameString = InNewName.ToString();

		// Check if the name is unchanged
		if (NewNameString.Equals(EntityNode->GetDisplayNameString()))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("RenameEntity", "Rename Entity"));
		FixturePatchTree->GetDMXLibrary()->Modify();

		FDMXEditorUtils::RenameEntity(FixturePatchTree->GetDMXLibrary(), EntityNode->GetEntity(), NewNameString);

		FixturePatchTree->SelectItemByName(NewNameString, ESelectInfo::OnMouseClick);
	}
}

bool SDMXFixturePatchTreeFixturePatchRow::OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage)
{
	TSharedPtr<SDMXFixturePatchTree> FixturePatchTree = WeakFixturePatchTree.Pin();
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin();

	if (FixturePatchTree.IsValid() && EntityNode.IsValid())
	{
		FString TextAsString = InNewText.ToString();
		if (TextAsString.Equals(WeakEntityNode.Pin()->GetDisplayNameString()))
		{
			return true;
		}

		return FDMXEditorUtils::ValidateEntityName(
			TextAsString,
			FixturePatchTree->GetDMXLibrary(),
			WeakEntityNode.Pin()->GetEntity()->GetClass(),
			OutErrorMessage
		);
	}

	return false;
}

FReply SDMXFixturePatchTreeFixturePatchRow::HandleOnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && OnEntityDragged.IsBound())
	{
		if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
		{
			return OnEntityDragged.Execute(EntityNode, MouseEvent);
		}
	}

	return FReply::Unhandled();
}

const FSlateBrush* SDMXFixturePatchTreeFixturePatchRow::GetStatusIcon() const
{
	static FSlateNoResource EmptyBrush = FSlateNoResource();

	if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		if (!EntityNode->GetErrorStatus().IsEmpty())
		{
			return FEditorStyle::GetBrush("Icons.Error");
		}

		if (!EntityNode->GetWarningStatus().IsEmpty())
		{
			return FEditorStyle::GetBrush("Icons.Warning");
		}
	}

	return &EmptyBrush;
}

FText SDMXFixturePatchTreeFixturePatchRow::GetStatusToolTip() const
{
	if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		const FText& ErrorStatus = EntityNode->GetErrorStatus();
		if (!ErrorStatus.IsEmpty())
		{
			return ErrorStatus;
		}

		const FText& WarningStatus = EntityNode->GetWarningStatus();
		if (!WarningStatus.IsEmpty())
		{
			return WarningStatus;
		}
	}

	return FText::GetEmpty();
}

ECheckBoxState SDMXFixturePatchTreeFixturePatchRow::IsAutoAssignChannelEnabled() const
{
	if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = WeakEntityNode.Pin())
	{
		if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()))
		{
			return FixturePatch->IsAutoAssignAddress() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
	}

	return ECheckBoxState::Undetermined;
}

UDMXLibrary* SDMXFixturePatchTreeFixturePatchRow::GetDMXLibrary() const
{
	if (const TSharedPtr<SDMXFixturePatchTree>& FixturePatchTree = WeakFixturePatchTree.Pin())
	{
		return FixturePatchTree->GetDMXLibrary();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
