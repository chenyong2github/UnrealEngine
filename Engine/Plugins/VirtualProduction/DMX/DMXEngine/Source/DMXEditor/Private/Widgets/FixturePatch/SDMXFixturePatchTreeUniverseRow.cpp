// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchTreeUniverseRow.h"

#include "DMXEditorUtils.h"
#include "SDMXFixturePatchTree.h"
#include "DragDrop/DMXEntityDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "SlateOptMacros.h"


#define LOCTEXT_NAMESPACE "SDMXFixturePatchTreeUniverseRow"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SDMXFixturePatchTreeUniverseRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FDMXEntityTreeCategoryNode> InCategoryNode, bool bInIsRootCategory, TWeakPtr<SDMXFixturePatchTree> InFixturePatchTree)
{
	check(InCategoryNode.IsValid());

	WeakFixturePatchTree = InFixturePatchTree;
	WeakCategoryNode = InCategoryNode;

	OnFixturePatchOrderChanged = InArgs._OnFixturePatchOrderChanged;

	// background color tint
	const FLinearColor BackgroundTint(0.6f, 0.6f, 0.6f, bInIsRootCategory ? 1.0f : 0.3f);

	// rebuilds the whole table row from scratch
	ChildSlot
	.Padding(0.0f, 2.0f, 0.0f, 0.0f)
	[
		SAssignNew(ContentBorder, SBorder)
		.BorderImage(this, &SDMXFixturePatchTreeUniverseRow::GetBackgroundImageBrush)
		.Padding(FMargin(0.0f, 3.0f))
		.BorderBackgroundColor(BackgroundTint)
		.ToolTipText(WeakCategoryNode.Pin()->GetToolTip())
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 2.0f, 2.0f, 2.0f)
			.AutoWidth()
			[
				SNew(SExpanderArrow, STableRow<TSharedPtr<FDMXEntityTreeCategoryNode>>::SharedThis(this))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				InArgs._Content.Widget
			]
		]
	];

	STableRow<TSharedPtr<FDMXEntityTreeCategoryNode>>::ConstructInternal(
		STableRow<TSharedPtr<FDMXEntityTreeCategoryNode>>::FArguments()
			.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(false),
		InOwnerTableView
	);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SDMXFixturePatchTreeUniverseRow::SetContent(TSharedRef< SWidget > InContent)
{
	ContentBorder->SetContent(InContent);
}

void SDMXFixturePatchTreeUniverseRow::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>())
	{
		TestCanDropWithFeedback(EntityDragDropOp.ToSharedRef());
	}
}

FReply SDMXFixturePatchTreeUniverseRow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDMXEntityDragDropOperation> EntityDragDropOp = DragDropEvent.GetOperationAs<FDMXEntityDragDropOperation>();
	TSharedPtr<SDMXFixturePatchTree> FixturePatchTree = WeakFixturePatchTree.Pin();
	const TSharedPtr<FDMXEntityTreeCategoryNode>& MyPinnedCategoryNode = WeakCategoryNode.Pin();
	UDMXLibrary* DMXLibrary = GetDMXLibrary();

	if (EntityDragDropOp.IsValid() && FixturePatchTree.IsValid() && MyPinnedCategoryNode.IsValid() && DMXLibrary)
	{
		if (TestCanDropWithFeedback(EntityDragDropOp.ToSharedRef()))
		{
			const TArray<TWeakObjectPtr<UDMXEntity>> DraggedEntities = EntityDragDropOp->GetDraggedEntities();

			DMXLibrary->Modify();

			// Register transaction and current DMX library state for Undo
			const FScopedTransaction ChangeCategoryTransaction = FScopedTransaction(
				FText::Format(LOCTEXT("DropFixturePatchOnUniverse", "Set Fixture {0}|plural(one=Patch, other=Patches) Universe"), DraggedEntities.Num())
			);

			if (MyPinnedCategoryNode->GetChildren().Num() > 0)
			{
				if (TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(MyPinnedCategoryNode->GetChildren().Last(0)))
				{
					// Index after last entity in hovered category
					UDMXEntity* LastEntityInCategory = EntityNode->GetEntity();

					const int32 LastEntityIndex = DMXLibrary->FindEntityIndex(LastEntityInCategory);
					check(LastEntityIndex != INDEX_NONE);

					// Move dragged entities after the last ones in the category
					// Reverse for to keep dragged entities order.
					int32 DesiredIndex = LastEntityIndex + 1;
					for (int32 EntityIndex = DraggedEntities.Num() - 1; EntityIndex > -1; --EntityIndex)
					{
						if (UDMXEntity* Entity = DraggedEntities[EntityIndex].Get())
						{
							DMXLibrary->SetEntityIndex(Entity, DesiredIndex);
						}
					}
				}
			}

			const int32 UniverseID = MyPinnedCategoryNode->GetIntValue();
			for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
			{
				if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity.Get()))
				{
					FixturePatch->Modify();
					FixturePatch->PreEditChange(nullptr);

					if (FixturePatch->IsAutoAssignAddress())
					{
						FDMXEditorUtils::TryAutoAssignToUniverses(FixturePatch, TSet<int32>({ UniverseID }));
					}
					FixturePatch->SetUniverseID(UniverseID);

					FixturePatch->PostEditChange();
				}
			}

			FixturePatchTree->UpdateTree();

			OnFixturePatchOrderChanged.ExecuteIfBound();

			return FReply::Handled().EndDragDrop();
		}
	}

	return FReply::Unhandled();
}

bool SDMXFixturePatchTreeUniverseRow::TestCanDropWithFeedback(const TSharedRef<FDMXEntityDragDropOperation>& EntityDragDropOp) const
{
	if (const TSharedPtr<FDMXEntityTreeCategoryNode>& MyPinnedCategoryNode = WeakCategoryNode.Pin())
	{
		// Only handle entity drag drop ops that contain fixture patches only 
		const bool bOnlyFixturePatchesAreDragged = [EntityDragDropOp]()
		{
			for (UClass* Class : EntityDragDropOp->GetDraggedEntityTypes())
			{
				if (Class != UDMXEntityFixturePatch::StaticClass())
				{
					return false;
				}
			}
			return true;
		}();

		const bool bRowAndEntitiesAreOfSameLibrary = [EntityDragDropOp, this]()
		{
			UDMXLibrary* DMXLibrary = GetDMXLibrary();
			for (TWeakObjectPtr<UDMXEntity> Entity : EntityDragDropOp->GetDraggedEntities())
			{
				if (Entity.IsValid() && Entity->GetParentLibrary() != DMXLibrary)
				{
					return false;
				}
			}
			return true;
		}();

		if (bOnlyFixturePatchesAreDragged && bRowAndEntitiesAreOfSameLibrary)
		{
			if (MyPinnedCategoryNode->CanDropOntoCategory())
			{
				const int32 NumDraggedEntities = EntityDragDropOp->GetDraggedEntities().Num();
				const TArray<TSharedRef<FDMXEntityTreeCategoryNode>> DraggedFromCategories = GetCategoryNodesFromDragDropOp(EntityDragDropOp);

				if (DraggedFromCategories.Num() == 1 && DraggedFromCategories[0] == MyPinnedCategoryNode)
				{
					// There wouldn't be any change by dragging the items into their own category.
					EntityDragDropOp->SetFeedbackMessageError(FText::Format(
						LOCTEXT("DragIntoSelfCategory", "The selected {0} {1}|plural(one=is, other=are) already in this category"),
						FDMXEditorUtils::GetEntityTypeNameText(UDMXEntityFixturePatch::StaticClass(), NumDraggedEntities > 1),
						NumDraggedEntities
					));

					return false;
				}
				else
				{
					const uint32 MyUniverseID = MyPinnedCategoryNode->GetIntValue();

					EntityDragDropOp->SetFeedbackMessageOK(FText::Format(
						LOCTEXT("ReorderBeforeAndSetProperty", "Reorder {0} before '{1}'\nSet {2} = '{3}'"),
						EntityDragDropOp->GetDraggedEntitiesName(),
						MyPinnedCategoryNode->GetDisplayNameText(),
						LOCTEXT("UniverseCategoryName", "Universe"),
						FText::AsNumber(MyUniverseID)
					));

					return true;
				}
			}
			else
			{
				EntityDragDropOp->SetFeedbackMessageError(FText::Format(
					LOCTEXT("DragIntoNoValueCategory", "Cannot drop {0} onto this category"),
					EntityDragDropOp->GetDraggedEntitiesName()
				));

				return false;
			}
		}
	}

	return false;
}

TArray<TSharedRef<FDMXEntityTreeCategoryNode>> SDMXFixturePatchTreeUniverseRow::GetCategoryNodesFromDragDropOp(const TSharedRef<FDMXEntityDragDropOperation>& DragDropOp) const
{
	TArray<TSharedRef<FDMXEntityTreeCategoryNode>> CategoryNodes;
	if (const TSharedPtr<SDMXFixturePatchTree>& FixturePatchTree = WeakFixturePatchTree.Pin())
	{
		TArray<TWeakObjectPtr<UDMXEntity>> DraggedEntities = DragDropOp->GetDraggedEntities();

		for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
		{
			if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity.Get()))
			{
				TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = FixturePatchTree->FindCategoryNodeOfEntity(FixturePatch);
				if (CategoryNode.IsValid())
				{
					CategoryNodes.Add(CategoryNode.ToSharedRef());
				}
			}
		}
	}

	return CategoryNodes;
}

const FSlateBrush* SDMXFixturePatchTreeUniverseRow::GetBackgroundImageBrush() const
{
	if (IsHovered())
	{
		return IsItemExpanded()
			? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered")
			: FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
	}
	else
	{
		return IsItemExpanded()
			? FEditorStyle::GetBrush("DetailsView.CategoryTop")
			: FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
	}
}

UDMXLibrary* SDMXFixturePatchTreeUniverseRow::GetDMXLibrary() const
{
	if (const TSharedPtr<SDMXFixturePatchTree>& FixturePatchTree = WeakFixturePatchTree.Pin())
	{
		return FixturePatchTree->GetDMXLibrary();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
