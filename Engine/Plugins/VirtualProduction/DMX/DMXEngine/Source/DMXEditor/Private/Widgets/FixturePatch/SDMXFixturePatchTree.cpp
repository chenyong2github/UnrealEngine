// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchTree.h"

#include "DMXEditor.h"
#include "DMXEditorLog.h"
#include "DMXEditorUtils.h"
#include "DMXFixturePatchSharedData.h"
#include "SDMXFixturePatchTreeFixturePatchRow.h"
#include "SDMXFixturePatchTreeUniverseRow.h"
#include "Commands/DMXEditorCommands.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
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
		.DMXEditor(InArgs._DMXEditor)
		.OnSelectionChanged(InArgs._OnSelectionChanged)
		.OnEntitiesAdded(InArgs._OnEntitiesAdded)
		.OnEntityOrderChanged(InArgs._OnEntityOrderChanged)
		.OnEntitiesRemoved(InArgs._OnEntitiesRemoved);

	SDMXEntityTreeViewBase::Construct(BaseArguments);

	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{
		FixturePatchSharedData = PinnedEditor->GetFixturePatchSharedData();
	}
	check(FixturePatchSharedData.IsValid());

	// Bind to fixture patch shared data selection changes
	FixturePatchSharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXFixturePatchTree::OnSharedDataSelectedFixturePatches);

	OnAutoAssignAddressChanged = InArgs._OnAutoAssignAddressChanged;
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
				.OnEntitySelected(this, &SDMXFixturePatchTree::OnFixtureTypeSelected)
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
		LOCTEXT("AssignedFixturesToolTip", "Patches which Universe IDs match are in port range")
	);
	TSharedPtr<FDMXEntityTreeCategoryNode> UnassignedFixturesCategoryNode = MakeShared<FDMXEntityTreeCategoryNode>(
		FDMXEntityTreeCategoryNode::ECategoryType::FixtureAssignmentState,
		LOCTEXT("UnassignedFixturesCategory", "Unassigned Fixtures"),
		-1,
		LOCTEXT("UnassignedFixturesToolTip", "Patches which Universe IDs are out of port range")
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
			.OnFixturePatchOrderChanged(OnEntityOrderChanged)
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
			.OnFixturePatchOrderChanged(OnEntityOrderChanged)
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
			.OnEntitySelected(this, &SDMXFixturePatchTree::OnFixtureTypeSelected);
		// TODO add (somehow) Paste option to this menu
	}

	return MenuBuilder.MakeWidget();
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

		// If pasting Patches we'll need to check against existing Fixture Types
		TArray<UDMXEntityFixtureType*> ExistingFixtureTypes;
		if (NewObjects[0]->GetClass()->IsChildOf<UDMXEntityFixturePatch>())
		{
			ExistingFixtureTypes = Library->GetEntitiesTypeCast<UDMXEntityFixtureType>();
		}
		// Caches suitable replacements for pasted FixtureTypes (Pasted -> Existing replacement)
		TMap<UDMXEntityFixtureType*, UDMXEntityFixtureType*> PatchTemplateReplacements;

		// Add each pasted Entity to the Library
		for (UDMXEntity* NewEntity : NewObjects)
		{
			UDMXEntityFixturePatch* FixurePatch = CastChecked<UDMXEntityFixturePatch>(NewEntity);

			// Check for existing similar Fixture Type templates in this editor's Library to replace the temp one from copy
			// or add the temp one if there's no suitable replacement

			// Do we need to replace the template?
			if (UDMXEntityFixtureType* CopiedPatchTemplate = FixurePatch->GetFixtureType())
			{
				// Did it come from this editor's DMX Library and does the original still exists?
				if (UDMXEntityFixtureType* OriginalTemplate = Cast<UDMXEntityFixtureType>(Library->FindEntity(CopiedPatchTemplate->GetID())))
				{
					FixurePatch->SetFixtureType(OriginalTemplate);
				}
				else
				{
					check(CopiedPatchTemplate != nullptr);

					// Is there already a suitable replacement registered for this template?
					if (PatchTemplateReplacements.Contains(CopiedPatchTemplate))
					{
						// Replace the Patch's template with the replacement
						FixurePatch->SetFixtureType(*PatchTemplateReplacements.Find(CopiedPatchTemplate));
					}
					else
					{
						// Search for a suitable replacement for the pasted Fixture Type, with identical
						// properties, except for the Name, ID and Parent Library
						bool bFoundReplacement = false;
						for (UDMXEntityFixtureType* ExistingFixtureType : ExistingFixtureTypes)
						{
							if (FDMXEditorUtils::AreFixtureTypesIdentical(CopiedPatchTemplate, ExistingFixtureType))
							{
								FixurePatch->SetFixtureType(ExistingFixtureType);
								PatchTemplateReplacements.Add(CopiedPatchTemplate, ExistingFixtureType);
								bFoundReplacement = true;
								break;
							}
						}

						if (!bFoundReplacement)
						{
							// Move the Fixture Type template from the transient package into the Library package
							NewEntity->Rename(*MakeUniqueObjectName(Library, UDMXEntityFixtureType::StaticClass()).ToString(), Library, REN_DoNotDirty | REN_DontCreateRedirectors);
							// Make sure the Template's name and ID won't collide with existing Fixture Types
							CopiedPatchTemplate->SetName(FDMXEditorUtils::FindUniqueEntityName(Library, UDMXEntityFixtureType::StaticClass(), CopiedPatchTemplate->GetDisplayName()));
							CopiedPatchTemplate->RefreshID();
							// Add to the Library
							Library->AddEntity(CopiedPatchTemplate);
						}
					}
				}
			}

			AutoAssignCopiedPatch(FixurePatch);

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
		// Force the text box being edited (if any) to commit its text. The duplicate operation may trigger a regeneration of the tree view,
		// releasing all row widgets. If one row was in edit mode (rename/rename on create), it was released before losing the focus and
		// this would prevent the completion of the 'rename' or 'create + give initial name' transaction (occurring on focus lost).
		FSlateApplication::Get().ClearKeyboardFocus();

		const FScopedTransaction Transaction(SelectedEntities.Num() > 1 ? LOCTEXT("DuplicateFixturePatches", "Duplicate Fixture Patch") : LOCTEXT("DuplicateFixturePatch", "Duplicate Fixture Patch"));
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

				if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(EntityCopy))
				{
					AutoAssignCopiedPatch(FixturePatch);
				}
			}
		}
		
		OnEntitiesAdded.ExecuteIfBound();
		
		// Need to refresh tree so new entities have nodes created for them
		UpdateTree(); 

		SelectItemsByEntities(NewEntities, ESelectInfo::OnMouseClick); // OnMouseClick triggers selection updated event
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

	OnEntitiesRemoved.ExecuteIfBound();

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
		else if (FixturePatch->GetChannelSpan() > DMX_UNIVERSE_SIZE)
		{
			return LOCTEXT("ChannelSpanExceedsUniverseSize", "Patch uses more than 512 channels.");
		}
		else
		{
			const bool bValidUniverse = [FixturePatch, this]()
			{
				UDMXLibrary* DMXLibrary = GetDMXLibrary();
				if (DMXLibrary)
				{
					for (const FDMXInputPortSharedRef& InputPort : DMXLibrary->GetInputPorts())
					{
						if (InputPort->IsLocalUniverseInPortRange(FixturePatch->GetUniverseID()))
						{
							return true;
						}
					}

					for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
					{
						if (OutputPort->IsLocalUniverseInPortRange(FixturePatch->GetUniverseID()))
						{
							return true;
						}
					}
				}

				return false;
			}();

			if (!bValidUniverse)
			{
				return LOCTEXT("InvalidUniverse", "Universe is not supported by any port used in the library.");
			}							
		}

		return FText::GetEmpty();
	}

	ensureMsgf(0, TEXT("Trying to validate null fixture patch in Fixture Patch editor."));
	return FText::GetEmpty();
}


void SDMXFixturePatchTree::OnSharedDataSelectedFixturePatches()
{
	check(FixturePatchSharedData.IsValid());
	const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& SelectedFixturePatches = FixturePatchSharedData->GetSelectedFixturePatches();
	const TArray<UDMXEntity*>& SelectedEntitiesInTree = GetSelectedEntities();

	bool bSelectionChanged = false;
	if (SelectedEntitiesInTree.Num() != SelectedFixturePatches.Num())
	{
		bSelectionChanged = true;
	}

	for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : SelectedFixturePatches)
	{
		if (!SelectedEntitiesInTree.Contains(Patch))
		{
			bSelectionChanged = true;
			break;
		}
	}

	if (bSelectionChanged)
	{
		TArray<UDMXEntity*> NewSelection;
		for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : SelectedFixturePatches)
		{
			if (Patch.IsValid())
			{
				NewSelection.Add(Patch.Get());
			}
		}
		SelectItemsByEntities(NewSelection);
	}
}

void SDMXFixturePatchTree::OnFixtureTypeSelected(UDMXEntity* InSelectedFixtureType)
{
	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{
		// Editor will call these during the creation of the new Entity
		UDMXEntityFixtureType* AsFixtureType = CastChecked<UDMXEntityFixtureType>(InSelectedFixtureType);
		OnGetBaseNameForNewEntityHandle = PinnedEditor->GetOnGetBaseNameForNewEntity().AddSP(this, &SDMXFixturePatchTree::OnEditorGetBaseNameForNewFixturePatch, AsFixtureType);
		OnSetupNewEntityHandle = PinnedEditor->GetOnSetupNewEntity().AddSP(this, &SDMXFixturePatchTree::OnEditorSetupNewFixturePatch, AsFixtureType);

		PinnedEditor->GetToolkitCommands()->ExecuteAction(FDMXEditorCommands::Get().AddNewEntityFixturePatch.ToSharedRef());
	}
}

void SDMXFixturePatchTree::OnEditorGetBaseNameForNewFixturePatch(TSubclassOf<UDMXEntity> InEntityClass, FString& OutBaseName, UDMXEntityFixtureType* InSelectedFixtureType)
{
	if (!InEntityClass->IsChildOf(UDMXEntityFixturePatch::StaticClass()))
	{
		return;
	}

	TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin();
	if (PinnedEditor.IsValid())
	{
		PinnedEditor->GetOnGetBaseNameForNewEntity().Remove(OnGetBaseNameForNewEntityHandle);

		OutBaseName = InSelectedFixtureType->GetDisplayName() + TEXT("_Patch");
	}
}

void SDMXFixturePatchTree::OnEditorSetupNewFixturePatch(UDMXEntity* InNewEntity, UDMXEntityFixtureType* InSelectedFixtureType)
{
	if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(InNewEntity))
	{
		TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin();
		if (PinnedEditor.IsValid())
		{
			PinnedEditor->GetOnSetupNewEntity().Remove(OnSetupNewEntityHandle);
			FixturePatch->SetFixtureType(InSelectedFixtureType);
		
			FDMXEditorUtils::UpdatePatchColors(FixturePatch->GetParentLibrary());
			
			const bool bExceedsUniverseSize = FixturePatch->GetChannelSpan() > DMX_UNIVERSE_SIZE;
			if(bExceedsUniverseSize)
			{
				FixturePatch->SetUniverseID(INDEX_NONE);
			}
			else
			{
				TSet<int32> UniverseIDsReachableByPorts = [this]()
				{
					if (UDMXLibrary* Library = GetDMXLibrary())
					{
						return Library->GetAllLocalUniversesIDsInPorts();
					}

					return TSet<int32>();
				}();
				FDMXEditorUtils::TryAutoAssignToUniverses(FixturePatch, UniverseIDsReachableByPorts);
			}

			// Issue a selection to trigger a OnSelectionUpdate and make the inspector display the new values
			SelectItemByEntity(FixturePatch);
		}
	}
	else
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S: New Entity wasn't a FixturePatch!"), __FUNCTION__);
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
					FixturePatch->SetAutoAssignAddressUnsafe(NewState);

					ChangedPatches.Add(FixturePatch);
				}
			}
		}
	}
	else
	{
		if (UDMXEntityFixturePatch* Patch = Cast<UDMXEntityFixturePatch>(InNodePtr->GetEntity()))
		{
			Patch->Modify();
			Patch->SetAutoAssignAddressUnsafe(NewState);

			ChangedPatches.Add(Patch);
		}
	}

	FDMXEditorUtils::AutoAssignedAddresses(ChangedPatches);

	UpdateTree();
	OnAutoAssignAddressChanged.ExecuteIfBound(ChangedPatches);
}

#undef LOCTEXT_NAMESPACE
