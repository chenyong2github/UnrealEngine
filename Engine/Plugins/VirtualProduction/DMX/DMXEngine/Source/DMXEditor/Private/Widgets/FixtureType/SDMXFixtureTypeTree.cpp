// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixtureTypeTree.h"

#include "DMXEditor.h"
#include "DMXEditorUtils.h"
#include "DMXFixtureTypeSharedData.h"
#include "SDMXFixtureTypeTreeCategoryRow.h"
#include "SDMXFixtureTypeTreeFixtureTypeRow.h"
#include "Commands/DMXEditorCommands.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"
#include "Widgets/DMXEntityTreeNode.h"

#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "SDMXFixtureTypeTree"

void SDMXFixtureTypeTree::Construct(const FArguments& InArgs)
{
	SDMXEntityTreeViewBase::FArguments BaseArguments =
		SDMXEntityTreeViewBase::FArguments()
		.DMXEditor(InArgs._DMXEditor)
		.OnSelectionChanged(this, &SDMXFixtureTypeTree::OnEntitySelected)
		.OnEntitiesAdded(InArgs._OnEntitiesAdded)
		.OnEntityOrderChanged(InArgs._OnEntityOrderChanged)
		.OnEntitiesRemoved(InArgs._OnEntitiesRemoved);

	SDMXEntityTreeViewBase::Construct(BaseArguments);
}

TSharedPtr<FDMXEntityTreeEntityNode> SDMXFixtureTypeTree::CreateEntityNode(UDMXEntity* Entity)
{
	check(Entity);
	TSharedPtr<FDMXEntityTreeEntityNode> NewNode = MakeShared<FDMXEntityTreeEntityNode>(Entity);
	RefreshFilteredState(NewNode, false);

	// Error status
	FText InvalidReason;
	if (!Entity->IsValidEntity(InvalidReason))
	{
		NewNode->SetErrorStatus(InvalidReason);
	}

	return NewNode;
}

TSharedRef<SWidget> SDMXFixtureTypeTree::GenerateAddNewEntityButton()
{
	FText AddButtonLabel = FDMXEditorCommands::Get().AddNewEntityFixtureType->GetLabel();
	FText AddButtonToolTip = FDMXEditorCommands::Get().AddNewEntityFixtureType->GetDescription();

	return
		SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(AddButtonToolTip)
			.ContentPadding(FMargin(5.0f, 1.0f))
			.OnClicked(this, &SDMXFixtureTypeTree::OnAddNewFixtureTypeClicked)
			.Content()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0, 1))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Plus"))
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(FMargin(2, 0, 2, 0))
				[
					SNew(STextBlock)
					.Text(AddButtonLabel)
				]
			];
}

void SDMXFixtureTypeTree::RebuildNodes(const TSharedPtr<FDMXEntityTreeRootNode>& InRootNode)
{
	EntityNodeToEntityRowMap.Reset();

	UDMXLibrary* Library = GetDMXLibrary();
	check(Library != nullptr);

	Library->ForEachEntityOfType<UDMXEntityFixtureType>([this](UDMXEntityFixtureType* FixtureType)
		{
			// Create this entity's node
			TSharedPtr<FDMXEntityTreeEntityNode> FixtureTypeNode = CreateEntityNode(FixtureType);

			// For each Entity, we find or create a category node then add the entity as its child
			const FDMXFixtureCategory DMXCategory = FixtureType->DMXCategory;
			const FText DMXCategoryName = FText::FromName(DMXCategory);

			// Get the category if already existent or create it
			constexpr FDMXEntityTreeCategoryNode::ECategoryType CategoryType = FDMXEntityTreeCategoryNode::ECategoryType::DMXCategory;
			TSharedPtr<FDMXEntityTreeNodeBase> CategoryNode = GetOrCreateCategoryNode(CategoryType, DMXCategoryName, FixtureType->DMXCategory);

			CategoryNode->AddChild(FixtureTypeNode);
		}
	);

	InRootNode->SortChildren();
}

TSharedRef<ITableRow> SDMXFixtureTypeTree::OnGenerateRow(TSharedPtr<FDMXEntityTreeNodeBase> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
		// Create the node of the appropriate type
	if (Node->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode)
	{
		const TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(Node);
		constexpr bool bIsRootCategory = true;

		return 
			SNew(SDMXFixtureTypeTreeCategoryRow, OwnerTable, CategoryNode, bIsRootCategory, SharedThis(this))
			.OnFixtureTypeOrderChanged(OnEntityOrderChanged)
			[
				SNew(STextBlock)
				.Text(Node->GetDisplayNameText())
				.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			];
	}
	else
	{
		const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(Node);

		TSharedRef<SDMXFixtureTypeTreeFixtureTypeRow> FixtureTypeRow = SNew(SDMXFixtureTypeTreeFixtureTypeRow, EntityNode, OwnerTable, SharedThis(this))
			.OnGetFilterText(this, &SDMXFixtureTypeTree::GetFilterText)
			.OnEntityDragged(this, &SDMXFixtureTypeTree::OnEntitiesDragged)
			.OnFixtureTypeOrderChanged(OnEntityOrderChanged);

		EntityNodeToEntityRowMap.Add(EntityNode.ToSharedRef(), FixtureTypeRow);

		return FixtureTypeRow;
	}
}

TSharedPtr<SWidget> SDMXFixtureTypeTree::OnContextMenuOpen()
{
	const bool bCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bCloseWindowAfterMenuSelection, CommandList);

	if (GetSelectedEntities().Num() > 0)
	{
		MenuBuilder.BeginSection("BasicOperations");
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder.EndSection();
	}
	else
	{
		MenuBuilder.BeginSection("BasicOperations");
		{
			MenuBuilder.AddMenuEntry(FDMXEditorCommands::Get().AddNewEntityFixtureType);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SDMXFixtureTypeTree::OnCutSelectedNodes()
{
	TArray<UDMXEntity*>&& SelectedNodes = GetSelectedEntities();
	const FScopedTransaction Transaction(SelectedNodes.Num() > 1 ? LOCTEXT("CutFixtureTypes", "Cut Fixture Types") : LOCTEXT("CutFixtureType", "Cut Fixture Type"));

	OnCopySelectedNodes();
	OnDeleteNodes();
}

bool SDMXFixtureTypeTree::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void SDMXFixtureTypeTree::OnCopySelectedNodes()
{
	TArray<UDMXEntity*>&& EntitiesToCopy = GetSelectedEntities();

	// Copy the entities to the clipboard
	FDMXEditorUtils::CopyEntities(MoveTemp(EntitiesToCopy));
}

bool SDMXFixtureTypeTree::CanCopyNodes() const
{
	TArray<UDMXEntity*>&& EntitiesToCopy = GetSelectedEntities();
	return EntitiesToCopy.Num() > 0;
}

void SDMXFixtureTypeTree::OnPasteNodes()
{
	// Get the Entities to paste from the clipboard
	TArray<UDMXEntity*> NewObjects;
	FDMXEditorUtils::GetEntitiesFromClipboard(NewObjects);

	if (NewObjects.Num() != 0)
	{
		// Get the library that's being edited
		UDMXLibrary* Library = GetDMXLibrary();
		check(Library);

		// Start transaction for Undo and take a snapshot of the current Library state
		const FScopedTransaction PasteEntities(NewObjects.Num() > 1 ? LOCTEXT("PasteFixtureTypes", "Paste Fixture Types") : LOCTEXT("PasteFixtureType", "Paste Fixture Type"));
		Library->Modify();

		// Add each pasted Entity to the Library
		for (UDMXEntity* NewEntity : NewObjects)
		{
			check(NewEntity);

			// Move the Entity from the transient package into the Library package
			NewEntity->Rename(*MakeUniqueObjectName(Library, NewEntity->GetClass()).ToString(), Library, REN_DoNotDirty | REN_DontCreateRedirectors);

			// Make sure the Entity's name won't collide with existing ones
			NewEntity->SetName(FDMXEditorUtils::FindUniqueEntityName(Library, NewEntity->GetClass(), NewEntity->GetDisplayName()));

			Library->AddEntity(NewEntity);

			OnEntitiesAdded.ExecuteIfBound();
		}

		UpdateTree();
		SelectItemsByEntities(NewObjects, ESelectInfo::OnMouseClick);
	}
}

bool SDMXFixtureTypeTree::CanPasteNodes() const
{
	return FDMXEditorUtils::CanPasteEntities();
}

void SDMXFixtureTypeTree::OnDuplicateNodes()
{
	UDMXLibrary* Library = GetDMXLibrary();
	TArray<UDMXEntity*> SelectedEntities = GetSelectedEntities();

	if (Library && SelectedEntities.Num() > 0)
	{
		// Force the text box being edited (if any) to commit its text. The duplicate operation may trigger a regeneration of the tree view,
		// releasing all row widgets. If one row was in edit mode (rename/rename on create), it was released before losing the focus and
		// this would prevent the completion of the 'rename' or 'create + give initial name' transaction (occurring on focus lost).
		FSlateApplication::Get().ClearKeyboardFocus();

		const FScopedTransaction Transaction(SelectedEntities.Num() > 1 ? LOCTEXT("DuplicateFixtureTypes", "Duplicate Fixture Types") : LOCTEXT("DuplicateFixtureType", "Duplicate Fixture Type"));
		Library->Modify();

		TArray<UDMXEntity*> NewEntities;
		NewEntities.Reserve(SelectedEntities.Num());

		// We'll have the duplicates be placed right after their original counterparts
		int32 NewEntityIndex = Library->FindEntityIndex(SelectedEntities.Last(0));
		for (UDMXEntity* Entity : SelectedEntities)
		{
			FObjectDuplicationParameters DuplicationParams(Entity, GetDMXLibrary());
			
			if (UDMXEntity* EntityCopy = CastChecked<UDMXEntity>(StaticDuplicateObjectEx(DuplicationParams)))
			{
				EntityCopy->SetName(FDMXEditorUtils::FindUniqueEntityName(Library, EntityCopy->GetClass(), EntityCopy->GetDisplayName()));
				NewEntities.Add(EntityCopy);
				
				Library->AddEntity(EntityCopy);
				Library->SetEntityIndex(EntityCopy, ++NewEntityIndex);
			}
		}
		
		OnEntitiesAdded.ExecuteIfBound();
		
		UpdateTree(); // Need to refresh tree so new entities have nodes created for them
		SelectItemsByEntities(NewEntities, ESelectInfo::OnMouseClick); // OnMouseClick triggers selection updated event
	}
}

bool SDMXFixtureTypeTree::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

void SDMXFixtureTypeTree::OnDeleteNodes()
{
	TArray<UDMXEntity*> EntitiesToDelete = GetSelectedEntities();
	
	// Check for entities being used by other objects
	TArray<UDMXEntity*> EntitiesInUse;
	for (UDMXEntity* Entity : EntitiesToDelete)
	{
		if (FDMXEditorUtils::IsEntityUsed(GetDMXLibrary(), Entity))
		{
			EntitiesInUse.Add(Entity);
		}
	}

	// Clears references to the Entities and delete them
	FDMXEditorUtils::RemoveEntities(GetDMXLibrary(), MoveTemp(EntitiesToDelete));

	OnEntitiesRemoved.ExecuteIfBound();

	UpdateTree();
}

bool SDMXFixtureTypeTree::CanDeleteNodes() const
{
	return GetSelectedEntities().Num() > 0;
}

void SDMXFixtureTypeTree::OnRenameNode()
{
	TArray<TSharedPtr<FDMXEntityTreeNodeBase>> SelectedItems = EntitiesTreeWidget->GetSelectedItems();

	if (SelectedItems.Num() == 1 && SelectedItems[0].IsValid() && SelectedItems[0]->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode)
	{
		EntitiesTreeWidget->RequestScrollIntoView(SelectedItems[0].ToSharedRef());

		const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(SelectedItems[0]);
		const TSharedPtr<SDMXFixtureTypeTreeFixtureTypeRow> FixtureTypeRow = FindEntityRowByNode(EntityNode.ToSharedRef());

		if (FixtureTypeRow.IsValid())
		{
			FixtureTypeRow->EnterRenameMode();
		}
	}
}

bool SDMXFixtureTypeTree::CanRenameNode() const
{
	TArray<TSharedPtr<FDMXEntityTreeNodeBase>> SelectedItems = EntitiesTreeWidget->GetSelectedItems();

	return 
		SelectedItems.Num() == 1 && 
		SelectedItems[0].IsValid() &&
		SelectedItems[0]->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode;
}

TSharedPtr<SDMXFixtureTypeTreeFixtureTypeRow> SDMXFixtureTypeTree::FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode)
{
	if (const TSharedRef<SDMXFixtureTypeTreeFixtureTypeRow>* RowPtr = EntityNodeToEntityRowMap.Find(EntityNode))
	{
		return *RowPtr;
	}

	return nullptr;
}

void SDMXFixtureTypeTree::OnEntitySelected(const TArray<UDMXEntity*>& NewSelection)
{
	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{
		if (TSharedPtr<FDMXFixtureTypeSharedData> SharedData = PinnedEditor->GetFixtureTypeSharedData())
		{
			// Never clear the selection
			if (GetSelectedEntities().Num() == 0)
			{
				TArray<TWeakObjectPtr<UDMXEntityFixtureType>> OldSelection = SharedData->GetSelectedFixtureTypes();
				TArray<UDMXEntity*> OldSelectionAsEntities;
				for (TWeakObjectPtr<UDMXEntityFixtureType> SelectedFixtureType : OldSelection)
				{
					if (UDMXEntityFixtureType* SelectedEntity = SelectedFixtureType.Get())
					{
						OldSelectionAsEntities.Add(SelectedEntity);
					}
				}

				SelectItemsByEntities(OldSelectionAsEntities);
			}
			else
			{
				// Select selected Fixture Types in Fixture Type Shared Data
				TArray<TWeakObjectPtr<UDMXEntityFixtureType>> SelectedFixtureTypes;

				for (UDMXEntity* Entity : NewSelection)
				{
					if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
					{
						SelectedFixtureTypes.Add(FixtureType);
					}
				}

				SharedData->SelectFixtureTypes(SelectedFixtureTypes);
			}
		}
	}
}

FReply SDMXFixtureTypeTree::OnAddNewFixtureTypeClicked()
{
	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{		
		PinnedEditor->GetToolkitCommands()->ExecuteAction(FDMXEditorCommands::Get().AddNewEntityFixtureType.ToSharedRef());
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
