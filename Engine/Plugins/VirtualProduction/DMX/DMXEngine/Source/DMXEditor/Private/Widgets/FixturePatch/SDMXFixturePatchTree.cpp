// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchTree.h"

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "DMXEditorUtils.h"
#include "DMXFixturePatchSharedData.h"
#include "DMXFixtureTypeSharedData.h"
#include "DMXRuntimeUtils.h"
#include "SDMXFixturePatchTreeFixturePatchRow.h"
#include "SDMXFixturePatchTreeUniverseRow.h"
#include "Commands/DMXEditorCommands.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXLibrary.h"
#include "Widgets/SDMXEntityDropdownMenu.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "SDMXFixturePatchTree"

void SDMXFixturePatchTree::Construct(const FArguments& InArgs)
{
	SDMXEntityTreeViewBase::FArguments BaseArguments =
		SDMXEntityTreeViewBase::FArguments()
		.DMXEditor(InArgs._DMXEditor);

	SDMXEntityTreeViewBase::Construct(BaseArguments);

	if (TSharedPtr<FDMXEditor> PinnedDMXEditor = InArgs._DMXEditor.Pin())
	{
		FixturePatchSharedData = PinnedDMXEditor->GetFixturePatchSharedData();

		// Bind to selection changes
		FixturePatchSharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXFixturePatchTree::OnFixturePatchesSelected);

		// Bind to library changes
		PinnedDMXEditor->GetDMXLibrary()->GetOnEntitiesAdded().AddSP(this, &SDMXFixturePatchTree::OnEntitiesAddedOrRemoved);
		PinnedDMXEditor->GetDMXLibrary()->GetOnEntitiesRemoved().AddSP(this, &SDMXFixturePatchTree::OnEntitiesAddedOrRemoved);

		// Bind to fixture patch changes
		UDMXEntityFixturePatch::GetOnFixturePatchChanged().AddSP(this, &SDMXFixturePatchTree::OnFixturePatchChanged);

		// Make an initial selection
		TArray<UDMXEntityFixturePatch*> FixturePatches = GetDMXLibrary()->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
		if (FixturePatches.Num() > 0)
		{
			TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> InitialSelection = { FixturePatches[0] };
			FixturePatchSharedData->SelectFixturePatches(InitialSelection);
		}
	}
}

TSharedPtr<FDMXEntityTreeEntityNode> SDMXFixturePatchTree::CreateEntityNode(UDMXEntity* Entity)
{
	check(Entity && Entity->GetClass() == UDMXEntityFixturePatch::StaticClass());

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

void SDMXFixturePatchTree::OnEntitiesAddedOrRemoved(UDMXLibrary* DMXLibrary, TArray<UDMXEntity*> Entities)
{
	if (DMXLibrary && AddButtonDropdownList.IsValid())
	{
		AddButtonDropdownList->RefreshEntitiesList();
		UpdateTree();
	}
}

void SDMXFixturePatchTree::OnFixturePatchChanged(const UDMXEntityFixturePatch* FixturePatch)
{
	if (TSharedPtr<FDMXEditor> PinnedDMXEditor = DMXEditor.Pin())
	{
		if (PinnedDMXEditor->GetDMXLibrary() == FixturePatch->GetParentLibrary())
		{
			UpdateTree();
		}
	}
}

void SDMXFixturePatchTree::OnFixturePatchesSelected()
{
	if (!bChangingSelection)
	{
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();

		TArray<UDMXEntity*> NewSelection;
		for (TWeakObjectPtr<UDMXEntityFixturePatch> SelectedFixturePatch : SelectedFixturePatches)
		{
			if (UDMXEntityFixturePatch* SelectedEntity = SelectedFixturePatch.Get())
			{
				NewSelection.Add(SelectedEntity);
			}
		}

		SelectItemsByEntities(NewSelection);
	}
}

TSharedRef<SWidget> SDMXFixturePatchTree::GenerateAddNewEntityButton()
{
	// Top part, with the  [+ Add New] button and the filter box
	FText AddButtonLabel = FDMXEditorCommands::Get().AddNewEntityFixturePatch->GetLabel();
	FText AddButtonToolTip = FDMXEditorCommands::Get().AddNewEntityFixturePatch->GetDescription();

	TSharedRef<SComboButton> AddComboButton = 
		SNew(SComboButton)
			.ButtonContent()
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
			]
			.MenuContent()
			[
				SAssignNew(AddButtonDropdownList, SDMXEntityDropdownMenu<UDMXEntityFixtureType>)
				.DMXEditor(DMXEditor)
				.OnEntitySelected(this, &SDMXFixturePatchTree::OnAddNewFixturePatchClicked)
			]
			.IsFocusable(true)
			.ContentPadding(FMargin(5.0f, 1.0f))
			.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
			.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(AddButtonToolTip)
			.OnComboBoxOpened(FOnComboBoxOpened::CreateLambda([this]() { AddButtonDropdownList->ClearSelection(); }));

	AddButtonDropdownList->SetComboButton(AddComboButton);

	return AddComboButton;
}

void SDMXFixturePatchTree::RebuildNodes(const TSharedPtr<FDMXEntityTreeRootNode>& InRootNode)
{
	struct Local
	{
		static void AssignPatchesToUniversesOrShowErrors(
			SDMXFixturePatchTree* FixturePatchTree, 
			const TSharedPtr<FDMXEntityTreeCategoryNode>& AssignedFixturesCategoryNode, 
			const TSharedPtr<FDMXEntityTreeCategoryNode>& UnassignedFixturesCategoryNode)
		{
			UDMXLibrary* Library = FixturePatchTree->GetDMXLibrary();
			check(Library);
			
			Library->ForEachEntityOfType<UDMXEntityFixturePatch>([&] (UDMXEntityFixturePatch* FixturePatch)
				{
					// Create this entity's node
					TSharedPtr<FDMXEntityTreeEntityNode> FixturePatchNode = FixturePatchTree->CreateEntityNode(FixturePatch);

					const FText ErrorMessage = FixturePatchTree->CheckForPatchError(FixturePatch);
					const bool bHasPatchNoErrors = ErrorMessage.IsEmpty();
				
					if (bHasPatchNoErrors && 
						FixturePatch->GetFixtureType() && 
						FixturePatch->GetUniverseID() >= 0 &&
						FixturePatch->GetChannelSpan() > 0)
					{
						constexpr FDMXEntityTreeCategoryNode::ECategoryType CategoryType = FDMXEntityTreeCategoryNode::ECategoryType::UniverseID;

						TSharedPtr<FDMXEntityTreeCategoryNode> UniverseCategoryNode = FixturePatchTree->GetOrCreateCategoryNode(
							CategoryType,
							FText::Format(LOCTEXT("UniverseSubcategoryLabel", "Universe {0}"),	FText::AsNumber(FixturePatch->GetUniverseID())),
							FixturePatch->GetUniverseID(),
							AssignedFixturesCategoryNode
						);

						UniverseCategoryNode->AddChild(FixturePatchNode);
					}
					else
					{
						FixturePatchNode->SetErrorStatus(ErrorMessage);
						UnassignedFixturesCategoryNode->AddChild(FixturePatchNode);
					}
				}
			);
		}
		static void SortPatchesByChannel(const TSharedPtr<FDMXEntityTreeCategoryNode>& AssignedFixturesCategoryNode)
		{
			for (TSharedPtr<FDMXEntityTreeNodeBase> UniverseIDCategory : AssignedFixturesCategoryNode->GetChildren())
			{			
				// Sort Patches by starting channel
				UniverseIDCategory->SortChildren([](const TSharedPtr<FDMXEntityTreeNodeBase>& A, const TSharedPtr<FDMXEntityTreeNodeBase>& B)->bool
					{
						check(A->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode && B->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode);
						const TSharedPtr<FDMXEntityTreeEntityNode> EntityNodeA = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(A);
						const TSharedPtr<FDMXEntityTreeEntityNode> EntityNodeB = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(B);

						const UDMXEntityFixturePatch* PatchA = Cast<UDMXEntityFixturePatch>(EntityNodeA->GetEntity());
						const UDMXEntityFixturePatch* PatchB = Cast<UDMXEntityFixturePatch>(EntityNodeB->GetEntity());
						if (PatchA != nullptr && PatchB != nullptr)
						{
							const int32& ChannelA = PatchA->GetStartingChannel();
							const int32& ChannelB = PatchB->GetStartingChannel();

							return PatchA->GetStartingChannel() <= PatchB->GetStartingChannel();
						}
						return false;
					});
			}
		}
		static void ShowErrorForOverlappingPatches(const TSharedPtr<FDMXEntityTreeCategoryNode>& AssignedFixturesCategoryNode)
		{
			for (const TSharedPtr<FDMXEntityTreeNodeBase>& ChildNode : AssignedFixturesCategoryNode->GetChildren())
			{
				check(ChildNode->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode);
				const TSharedPtr<FDMXEntityTreeCategoryNode> UniverseIDNode = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(ChildNode);

				// Store the latest occupied channel in this Universe
				int32 AvailableChannel = 1;
				const UDMXEntityFixturePatch* PreviousPatch = nullptr;

				for (TSharedPtr<FDMXEntityTreeNodeBase> CategoryChildNode : UniverseIDNode->GetChildren())
				{
					check(CategoryChildNode->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode);
					const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(CategoryChildNode);

					if (UDMXEntityFixturePatch* Patch = Cast<UDMXEntityFixturePatch>(EntityNode->GetEntity()))
					{
						const int32 ChannelSpan = Patch->GetChannelSpan();

						FText InvalidReason;
						if (Patch->GetStartingChannel() < AvailableChannel && PreviousPatch)
						{
							// The Patch is overlapping occupied channels
							EntityNode->SetErrorStatus(FText::Format(
								LOCTEXT("PatchOverlapWarning", "Start channel overlaps channels from {0}"),
								FText::FromString(PreviousPatch->GetDisplayName())
							));
						}
						else if (!Patch->IsValidEntity(InvalidReason))
						{
							// Update error status because after auto-channel changes there could be validation errors
							EntityNode->SetErrorStatus(InvalidReason);
						}
						else
						{
							EntityNode->SetErrorStatus(FText::GetEmpty());
						}

						// Update the next available channel from this Patch's functions
						AvailableChannel = Patch->GetStartingChannel() + ChannelSpan;

						PreviousPatch = Patch;
					}
				}
			}
		}
	};

	EntityNodeToEntityRowMap.Reset();
	
	static constexpr uint32 UnassignedUniverseValue = MAX_uint32;
	
	// These nodes' categories are either Assigned or Unassigned
	TSharedPtr<FDMXEntityTreeCategoryNode> AssignedFixturesCategoryNode = MakeShared<FDMXEntityTreeCategoryNode>(
		FDMXEntityTreeCategoryNode::ECategoryType::FixtureAssignmentState,
		LOCTEXT("AssignedFixturesCategory", "Assigned Fixtures"),
		LOCTEXT("AssignedFixturesToolTip", "Fixtures patched to a valid Universe")
	);
	TSharedPtr<FDMXEntityTreeCategoryNode> UnassignedFixturesCategoryNode = MakeShared<FDMXEntityTreeCategoryNode>(
		FDMXEntityTreeCategoryNode::ECategoryType::FixtureAssignmentState,
		LOCTEXT("UnassignedFixturesCategory", "Unassigned Fixtures"),
		-1,
		LOCTEXT("UnassignedFixturesToolTip", "Patches with an invalid Universe")
	);
	
	InRootNode->AddChild(AssignedFixturesCategoryNode);
	InRootNode->AddChild(UnassignedFixturesCategoryNode);
	EntitiesTreeWidget->SetItemExpansion(AssignedFixturesCategoryNode, true);
	EntitiesTreeWidget->SetItemExpansion(UnassignedFixturesCategoryNode, true);
	
	RefreshFilteredState(AssignedFixturesCategoryNode, false);
	RefreshFilteredState(UnassignedFixturesCategoryNode, false);

	Local::AssignPatchesToUniversesOrShowErrors(this, AssignedFixturesCategoryNode, UnassignedFixturesCategoryNode);
	
	AssignedFixturesCategoryNode->SortChildren();
	Local::SortPatchesByChannel(AssignedFixturesCategoryNode);
	
	Local::ShowErrorForOverlappingPatches(AssignedFixturesCategoryNode);

	// Restore Selection
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
	TArray<UDMXEntity*> SelectedEntities;
	for (TWeakObjectPtr<UDMXEntityFixturePatch> WeakFixturePatch : SelectedFixturePatches)
	{
		if (WeakFixturePatch.IsValid())
		{
			SelectedEntities.Add(WeakFixturePatch.Get());
		}
	}
	SelectItemsByEntities(SelectedEntities);
}

TSharedRef<ITableRow> SDMXFixturePatchTree::OnGenerateRow(TSharedPtr<FDMXEntityTreeNodeBase> InNodePtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Create the node of the appropriate type
	if (InNodePtr->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode)
	{
		TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(InNodePtr);
		const bool bIsRootCategory = CategoryNode->GetCategoryType() != FDMXEntityTreeCategoryNode::ECategoryType::UniverseID;

		return 
			SNew(SDMXFixturePatchTreeUniverseRow, OwnerTable, CategoryNode, bIsRootCategory, SharedThis(this))
			.OnFixturePatchOrderChanged(OnEntityOrderChangedDelegate)
			[
				SNew(STextBlock)
				.Text(InNodePtr->GetDisplayNameText())
				.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			];
	}
	else
	{
		TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(InNodePtr);

		TSharedRef<SDMXFixturePatchTreeFixturePatchRow> FixturePatchRow = SNew(SDMXFixturePatchTreeFixturePatchRow, EntityNode, OwnerTable, SharedThis(this))
			.OnGetFilterText(this, &SDMXEntityTreeViewBase::GetFilterText)
			.OnEntityDragged(this, &SDMXEntityTreeViewBase::OnEntitiesDragged)
			.OnFixturePatchOrderChanged(OnEntityOrderChangedDelegate)
			.OnAutoAssignChannelStateChanged(this, &SDMXFixturePatchTree::OnAutoAssignChannelStateChanged, EntityNode);

		EntityNodeToEntityRowMap.Add(EntityNode.ToSharedRef(), FixturePatchRow);

		return FixturePatchRow;
	}
}

TSharedPtr<SWidget> SDMXFixturePatchTree::OnContextMenuOpen()
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
		return SNew(SDMXEntityDropdownMenu<UDMXEntityFixtureType>)
			.DMXEditor(DMXEditor)
			.OnEntitySelected(this, &SDMXFixturePatchTree::OnAddNewFixturePatchClicked);
	}

	return MenuBuilder.MakeWidget();
}

void SDMXFixturePatchTree::OnSelectionChanged(TSharedPtr<FDMXEntityTreeNodeBase> InSelectedNodePtr, ESelectInfo::Type SelectInfo)
{
	const TArray<UDMXEntity*> NewSelection = GetSelectedEntities();

	TGuardValue<bool> RecursionGuard(bChangingSelection, true);

	// Never clear the selection
	if (GetSelectedEntities().Num() == 0)
	{
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> OldSelection = FixturePatchSharedData->GetSelectedFixturePatches();
		TArray<UDMXEntity*> OldSelectionAsEntities;
		for (TWeakObjectPtr<UDMXEntityFixturePatch> SelectedFixtureType : OldSelection)
		{
			if (UDMXEntityFixturePatch* SelectedEntity = SelectedFixtureType.Get())
			{
				OldSelectionAsEntities.Add(SelectedEntity);
			}
		}

		if (OldSelectionAsEntities.Num() > 0)
		{
			SelectItemsByEntities(OldSelectionAsEntities);
		}
	}
	else
	{
		// Select selected Fixture Types in Fixture Type Shared Data
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixtureTypes;

		for (UDMXEntity* Entity : NewSelection)
		{
			if (UDMXEntityFixturePatch* FixtureType = Cast<UDMXEntityFixturePatch>(Entity))
			{
				SelectedFixtureTypes.Add(FixtureType);
			}
		}

		FixturePatchSharedData->SelectFixturePatches(SelectedFixtureTypes);
	}
}

void SDMXFixturePatchTree::OnCutSelectedNodes()
{
	TArray<UDMXEntity*> SelectedEntities = GetSelectedEntities();
	const FScopedTransaction Transaction(SelectedEntities.Num() > 1 ? LOCTEXT("CutFixturePatches", "Cut Fixture Patches") : LOCTEXT("CutFixturePatch", "Cut Fixture Patch"));

	OnCopySelectedNodes();
	OnDeleteNodes();
}

bool SDMXFixturePatchTree::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void SDMXFixturePatchTree::OnCopySelectedNodes()
{
	TArray<UDMXEntity*> SelectedEntities = GetSelectedEntities();

	// Copy the entities to the clipboard
	FDMXEditorUtils::CopyEntities(MoveTemp(SelectedEntities));
}

bool SDMXFixturePatchTree::CanCopyNodes() const
{
	TArray<UDMXEntity*> SelectedEntities = GetSelectedEntities();
	return SelectedEntities.Num() > 0;
}

void SDMXFixturePatchTree::OnPasteNodes()
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
		const FScopedTransaction PasteEntities(NewObjects.Num() > 1 ? LOCTEXT("PasteFixturePatches", "Paste Fixture Patches") : LOCTEXT("PasteFixturePatch", "Paste Fixture Patch"));
		Library->Modify();

		// Add each pasted Entity to the Library
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewFixturePatches;
		for (UDMXEntity* NewEntity : NewObjects)
		{
			UDMXEntityFixturePatch* FixturePatch = CastChecked<UDMXEntityFixturePatch>(NewEntity);
			NewFixturePatches.Add(FixturePatch);

			// Search for a suitable replacement for the pasted Fixture Type, with identical
			// properties, except for the Name, ID and Parent Library
			const TArray<UDMXEntityFixturePatch*> FixturePatches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
			const UDMXEntityFixturePatch* const* SourceFixturePatchPtr = FixturePatches.FindByPredicate([FixturePatch](const UDMXEntityFixturePatch* SourceFixturePatch)
				{
					return SourceFixturePatch->GetID() == FixturePatch->GetID();
				});

			if (SourceFixturePatchPtr)
			{
				FixturePatch->SetFixtureType((*SourceFixturePatchPtr)->GetFixtureType());
			}

			// Move the Fixture Type template from the transient package into the Library package
			NewEntity->Rename(*MakeUniqueObjectName(Library, UDMXEntityFixtureType::StaticClass()).ToString(), Library, REN_DoNotDirty | REN_DontCreateRedirectors);
			NewEntity->RefreshID();

			// Add to the Library
			AutoAssignCopiedPatch(FixturePatch);

			// Move the Entity from the transient package into the Library package
			NewEntity->Rename(*MakeUniqueObjectName(Library, NewEntity->GetClass()).ToString(), Library, REN_DoNotDirty | REN_DontCreateRedirectors);
			
			// Make sure the Entity's name won't collide with existing ones
			NewEntity->SetName(FDMXRuntimeUtils::FindUniqueEntityName(Library, NewEntity->GetClass(), NewEntity->GetDisplayName()));
		}

		FixturePatchSharedData->SelectFixturePatches(NewFixturePatches);

		UpdateTree();
	}
}

bool SDMXFixturePatchTree::CanPasteNodes() const
{
	return FDMXEditorUtils::CanPasteEntities();
}

void SDMXFixturePatchTree::OnDuplicateNodes()
{
	TArray<UDMXEntity*> SelectedEntities = GetSelectedEntities();
	// Sort selected entities by universe and starting channel to get a meaningful order when auto assign addresses
	SelectedEntities.Sort([&](UDMXEntity& FirstEntity, UDMXEntity& SecondEntity) {
		UDMXEntityFixturePatch* FirstPatch = CastChecked<UDMXEntityFixturePatch>(&FirstEntity);
		UDMXEntityFixturePatch* SecondPatch = CastChecked<UDMXEntityFixturePatch>(&SecondEntity);
		return
			FirstPatch->GetUniverseID() < SecondPatch->GetUniverseID() ||
			(FirstPatch->GetUniverseID() == SecondPatch->GetUniverseID() &&
				FirstPatch->GetStartingChannel() <= SecondPatch->GetStartingChannel());
		});
	
	UDMXLibrary* Library = GetDMXLibrary();
	if (SelectedEntities.Num() > 0 && Library)
	{
		const FScopedTransaction Transaction(SelectedEntities.Num() > 1 ? LOCTEXT("DuplicateFixturePatches", "Duplicate Fixture Patch") : LOCTEXT("DuplicateFixturePatch", "Duplicate Fixture Patch"));
		Library->Modify();

		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewFixturePatches;
		NewFixturePatches.Reserve(SelectedEntities.Num());

		// We'll have the duplicates be placed right after their original counterparts
		int32 NewEntityIndex = Library->FindEntityIndex(SelectedEntities.Last(0));
		for (UDMXEntity* Entity : SelectedEntities)
		{
			FObjectDuplicationParameters DuplicationParams(Entity, GetDMXLibrary());
			
			if (UDMXEntityFixturePatch* EntityCopy = CastChecked<UDMXEntityFixturePatch>(StaticDuplicateObjectEx(DuplicationParams)))
			{
				EntityCopy->SetName(FDMXRuntimeUtils::FindUniqueEntityName(Library, EntityCopy->GetClass(), EntityCopy->GetDisplayName()));
				NewFixturePatches.Add(EntityCopy);

				Library->SetEntityIndex(EntityCopy, ++NewEntityIndex);

				if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(EntityCopy))
				{
					AutoAssignCopiedPatch(FixturePatch);
				}
			}
		}

		FixturePatchSharedData->SelectFixturePatches(NewFixturePatches);

		UpdateTree(); 
	}
}

bool SDMXFixturePatchTree::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

void SDMXFixturePatchTree::OnDeleteNodes()
{
	const TArray<UDMXEntity*> EntitiesToDelete = GetSelectedEntities();

	// Clears references to the Entities and delete them
	FDMXEditorUtils::RemoveEntities(GetDMXLibrary(), EntitiesToDelete);

	// Clear selection if no patches remain
	if (GetSelectedEntities().Num() == 0)
	{
		FixturePatchSharedData->SelectFixturePatches(TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>());
	}

	UpdateTree();
}

bool SDMXFixturePatchTree::CanDeleteNodes() const
{
	return GetSelectedEntities().Num() > 0;
}

void SDMXFixturePatchTree::OnRenameNode()
{
	TArray<TSharedPtr<FDMXEntityTreeNodeBase>> SelectedItems = EntitiesTreeWidget->GetSelectedItems();

	if (SelectedItems.Num() == 1 && SelectedItems[0]->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::EntityNode)
	{
		const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(SelectedItems[0]);
		const TSharedPtr<SDMXFixturePatchTreeFixturePatchRow> FixturePatchRow = FindEntityRowByNode(EntityNode.ToSharedRef());

		if (FixturePatchRow.IsValid())
		{
			FixturePatchRow->EnterRenameMode();
		}
	}
}

bool SDMXFixturePatchTree::CanRenameNode() const
{
	return EntitiesTreeWidget->GetSelectedItems().Num() == 1 && EntitiesTreeWidget->GetSelectedItems()[0]->CanRename();
}

TSharedPtr<SDMXFixturePatchTreeFixturePatchRow> SDMXFixturePatchTree::FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode)
{
	if (const TSharedRef<SDMXFixturePatchTreeFixturePatchRow>* RowPtr = EntityNodeToEntityRowMap.Find(EntityNode))
	{
		return *RowPtr;
	}

	return nullptr;
}

void SDMXFixturePatchTree::AutoAssignCopiedPatch(UDMXEntityFixturePatch* Patch) const
{
	check(Patch);

	const int32 OriginalStartingChannel = Patch->GetStartingChannel();

	Patch->Modify();
	Patch->SetAutoAssignAddressUnsafe(true);
	FDMXEditorUtils::AutoAssignedAddresses(TArray<UDMXEntityFixturePatch*>{ Patch }, OriginalStartingChannel + 1);
}

FText SDMXFixturePatchTree::CheckForPatchError(UDMXEntityFixturePatch* FixturePatch) const
{
	if (FixturePatch)
	{
		UDMXEntityFixtureType* FixtureType = FixturePatch->GetFixtureType();
		if (!FixtureType || FixtureType->Modes.Num() == 0)
		{
			return LOCTEXT("FixtureTypeHasNoModes", "This patch's fixture type has no modes.");
		}

		const bool bHasAnyFunctions = [FixturePatch]()
		{
			for (const FDMXFixtureMode& Mode : FixturePatch->GetFixtureType()->Modes)
			{
				if (Mode.Functions.Num() > 0 ||
					Mode.FixtureMatrixConfig.CellAttributes.Num() > 0)
				{
					return true;
				}
			}
			return false;
		}();
		if (!bHasAnyFunctions)
		{
			return LOCTEXT("FixtureTypeHasNoFunctions", "This patch's fixture type has no functions.");
		}
		else if (FixturePatch->GetChannelSpan() == 0)
		{
			return LOCTEXT("FixtureTypeHasNoChannelSpan", "This patch has a channel span of 0.");
		}

		return FText::GetEmpty();
	}

	ensureMsgf(0, TEXT("Trying to validate null fixture patch in Fixture Patch editor."));
	return FText::GetEmpty();
}

void SDMXFixturePatchTree::OnAddNewFixturePatchClicked(UDMXEntity* InSelectedFixtureType)
{
	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{
		if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(InSelectedFixtureType))
		{
			// Find the first free channel behind all patches, before adding the new patch
			const UDMXLibrary* DMXLibrary = FixtureType->GetParentLibrary();
			TArray<UDMXEntityFixturePatch*> OtherFixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
			OtherFixturePatches.Sort([](const UDMXEntityFixturePatch& OtherPatchA, const UDMXEntityFixturePatch& OtherPatchB)
				{
					if (OtherPatchA.GetUniverseID() > OtherPatchB.GetUniverseID())
					{
						return true;
					}
					else if (OtherPatchA.GetUniverseID() == OtherPatchB.GetUniverseID())
					{
						return OtherPatchA.GetStartingChannel() >= OtherPatchB.GetStartingChannel();
					}

					return false;
				});

			const int32 LastUniverseInUse = [&OtherFixturePatches]()
			{
				if (OtherFixturePatches.Num() > 0)
				{
					return OtherFixturePatches[0]->GetUniverseID();
				}
				return 1;
			}();

			const int32 FirstFreeDMXChannel = [&OtherFixturePatches, LastUniverseInUse]()
			{
				if (OtherFixturePatches.Num() > 0)
				{
					return OtherFixturePatches[0]->GetStartingChannel() + OtherFixturePatches[0]->GetChannelSpan();
				}

				return 1;
			}();

			const FDMXEntityFixtureTypeRef FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
			UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixtureTypeRef, FixtureType->Name);
			if (NewFixturePatch)
			{
				FDMXEditorUtils::UpdatePatchColors(NewFixturePatch->GetParentLibrary());

				const bool bExceedsUniverseSize = NewFixturePatch->GetChannelSpan() > DMX_UNIVERSE_SIZE;
				if (bExceedsUniverseSize)
				{
					NewFixturePatch->SetUniverseID(INDEX_NONE);
				}
				else
				{
					if (FirstFreeDMXChannel + NewFixturePatch->GetChannelSpan() - 1 <= DMX_UNIVERSE_SIZE)
					{
						NewFixturePatch->SetUniverseID(LastUniverseInUse);
						NewFixturePatch->SetStartingChannel(FirstFreeDMXChannel);
					}
					else
					{
						NewFixturePatch->SetUniverseID(LastUniverseInUse + 1);
						NewFixturePatch->SetStartingChannel(1);
					}
				}

				FixturePatchSharedData->SelectFixturePatches(TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>({ NewFixturePatch }));

				UpdateTree();
			}
		}
	}
}

void SDMXFixturePatchTree::OnAutoAssignChannelStateChanged(bool NewState, TSharedPtr<FDMXEntityTreeEntityNode> InNodePtr)
{
	const FScopedTransaction Transaction(LOCTEXT("SetAutoAssignChannelTransaction", "Set Auto Assign Channel"));

	TArray<UDMXEntityFixturePatch*> ChangedPatches;

	// Was the changed entity one of the selected ones?
	if (EntitiesTreeWidget->IsItemSelected(InNodePtr))
	{
		for (UDMXEntity* Entity : GetSelectedEntities())
		{
			if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity))
			{
				if (FixturePatch->IsAutoAssignAddress() != NewState)
				{
					FixturePatch->Modify();
					FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetAutoAssignAddressPropertyNameChecked()));

					FixturePatch->SetAutoAssignAddressUnsafe(NewState);

					FixturePatch->PostEditChange();

					ChangedPatches.Add(FixturePatch);
				}
			}
		}
	}
	else
	{
		if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(InNodePtr->GetEntity()))
		{
			FixturePatch->Modify();
			FixturePatch->PreEditChange(UDMXEntityFixturePatch::StaticClass()->FindPropertyByName(UDMXEntityFixturePatch::GetAutoAssignAddressPropertyNameChecked()));

			FixturePatch->SetAutoAssignAddressUnsafe(NewState);

			FixturePatch->PostEditChange();

			ChangedPatches.Add(FixturePatch);
		}
	}

	FDMXEditorUtils::AutoAssignedAddresses(ChangedPatches);

	UpdateTree();
}

#undef LOCTEXT_NAMESPACE
