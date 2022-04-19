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
		FixturePatchSharedData->OnUniverseSelectionChanged.AddSP(this, &SDMXFixturePatchTree::OnUniverseSelected);

		// Bind to library changes
		PinnedDMXEditor->GetDMXLibrary()->GetOnEntitiesAdded().AddSP(this, &SDMXFixturePatchTree::OnEntitiesAddedOrRemoved);
		PinnedDMXEditor->GetDMXLibrary()->GetOnEntitiesRemoved().AddSP(this, &SDMXFixturePatchTree::OnEntitiesAddedOrRemoved);

		// Bind to fixture patch changes
		UDMXEntityFixturePatch::GetOnFixturePatchChanged().AddSP(this, &SDMXFixturePatchTree::OnFixturePatchChanged);

		// Bind to fixture type changes
		UDMXEntityFixtureType::GetOnFixtureTypeChanged().AddSP(this, &SDMXFixturePatchTree::OnFixtureTypeChanged);

		// Make an initial selection
		const TArray<UDMXEntityFixturePatch*> FixturePatches = GetDMXLibrary()->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
		if (FixturePatches.Num() > 0)
		{
			const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> InitialSelection = { FixturePatches[0] };
			FixturePatchSharedData->SelectFixturePatches(InitialSelection);
		}
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
			.OnComboBoxOpened(FOnComboBoxOpened::CreateLambda([this]() 
				{ 
					AddButtonDropdownList->RefreshEntitiesList();
				}));

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

						TSharedRef<FDMXEntityTreeCategoryNode> UniverseCategoryNode = FixturePatchTree->GetOrCreateCategoryNode(
							CategoryType,
							FText::Format(LOCTEXT("UniverseSubcategoryLabel", "Universe {0}"),	FText::AsNumber(FixturePatch->GetUniverseID())),
							FixturePatch->GetUniverseID(),
							AssignedFixturesCategoryNode
						);

						UniverseCategoryNode->AddChild(FixturePatchNode);

						// Retain expansion states
						const int32 UserExpandedCategoryIndex = FixturePatchTree->UserExpandedUniverseCategoryNodes.IndexOfByPredicate([UniverseCategoryNode](const TSharedPtr<FDMXEntityTreeCategoryNode>& OtherUniverseCategoryNode)
							{
								return UniverseCategoryNode->GetIntValue() == OtherUniverseCategoryNode->GetIntValue();
							});

						if (UserExpandedCategoryIndex != INDEX_NONE)
						{
							FixturePatchTree->UserExpandedUniverseCategoryNodes[UserExpandedCategoryIndex] = UniverseCategoryNode;
							FixturePatchTree->SetNodeExpansion(FixturePatchTree->UserExpandedUniverseCategoryNodes[UserExpandedCategoryIndex], true);
						}
						else if (!FixturePatchTree->AutoExpandedUniverseCategoryNode.IsValid() ||
							FixturePatchTree->AutoExpandedUniverseCategoryNode->GetIntValue() == UniverseCategoryNode->GetIntValue())
						{
							FixturePatchTree->SetNodeExpansion(UniverseCategoryNode, true);
							FixturePatchTree->AutoExpandedUniverseCategoryNode = UniverseCategoryNode;
						}
						else
						{
							FixturePatchTree->SetNodeExpansion(UniverseCategoryNode, false);
						}
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
	SetNodeExpansion(AssignedFixturesCategoryNode, true);
	SetNodeExpansion(UnassignedFixturesCategoryNode, true);
	
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

TSharedRef<ITableRow> SDMXFixturePatchTree::OnGenerateRow(TSharedPtr<FDMXEntityTreeNodeBase> InNode, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Create the node of the appropriate type
	if (InNode->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode)
	{
		TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(InNode);
		const bool bIsRootCategory = CategoryNode->GetCategoryType() != FDMXEntityTreeCategoryNode::ECategoryType::UniverseID;

		return 
			SNew(SDMXFixturePatchTreeUniverseRow, OwnerTable, CategoryNode, bIsRootCategory, SharedThis(this))
			.OnFixturePatchOrderChanged(OnEntityOrderChangedDelegate)
			[
				SNew(STextBlock)
				.Text(InNode->GetDisplayNameText())
				.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			];
	}
	else
	{
		TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = StaticCastSharedPtr<FDMXEntityTreeEntityNode>(InNode);

		TSharedRef<SDMXFixturePatchTreeFixturePatchRow> FixturePatchRow = SNew(SDMXFixturePatchTreeFixturePatchRow, EntityNode, OwnerTable, SharedThis(this))
			.OnGetFilterText(this, &SDMXEntityTreeViewBase::GetFilterText)
			.OnEntityDragged(this, &SDMXEntityTreeViewBase::OnEntitiesDragged)
			.OnFixturePatchOrderChanged(OnEntityOrderChangedDelegate)
			.OnAutoAssignChannelStateChanged(this, &SDMXFixturePatchTree::OnAutoAssignChannelStateChanged, EntityNode);

		EntityNodeToEntityRowMap.Add(EntityNode.ToSharedRef(), FixturePatchRow);

		return FixturePatchRow;
	}
}

void SDMXFixturePatchTree::OnExpansionChanged(TSharedPtr<FDMXEntityTreeNodeBase> Node, bool bInExpansionState)
{
	SDMXEntityTreeViewBase::OnExpansionChanged(Node, bInExpansionState);

	if (Node.IsValid())
	{
		if (bInExpansionState &&
			Node->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode &&
			Node != AutoExpandedUniverseCategoryNode)
		{
			TSharedRef<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedRef<FDMXEntityTreeCategoryNode>(Node.ToSharedRef());
			if (CategoryNode->GetCategoryType() == FDMXEntityTreeCategoryNode::ECategoryType::UniverseID)
			{
				UserExpandedUniverseCategoryNodes.AddUnique(CategoryNode);
			}
		}
		else if (!bInExpansionState &&
			Node->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode)
		{
			TSharedRef<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedRef<FDMXEntityTreeCategoryNode>(Node.ToSharedRef());
			if (CategoryNode->GetCategoryType() == FDMXEntityTreeCategoryNode::ECategoryType::UniverseID)
			{
				UserExpandedUniverseCategoryNodes.RemoveSingle(CategoryNode);

				if (AutoExpandedUniverseCategoryNode == CategoryNode)
				{
					AutoExpandedUniverseCategoryNode.Reset();
				}
			}
		}
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

void SDMXFixturePatchTree::OnSelectionChanged(TSharedPtr<FDMXEntityTreeNodeBase> InSelectedNode, ESelectInfo::Type SelectInfo)
{
	const TArray<UDMXEntity*> NewSelection = GetSelectedEntities();

	TGuardValue<bool> RecursionGuard(bChangingFixturePatchSelection, true);

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
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches;

		for (UDMXEntity* Entity : NewSelection)
		{
			if (UDMXEntityFixturePatch* FixtureType = Cast<UDMXEntityFixturePatch>(Entity))
			{
				SelectedFixturePatches.Add(FixtureType);
			}
		}

		// Scroll into view
		if (SelectedFixturePatches.Num() > 0)
		{
			// The weak ptrs are known to be valid here, as they were created from the strong pointers of NewSelection above
			SelectedFixturePatches.Sort([](const TWeakObjectPtr<UDMXEntityFixturePatch>& WeakFixturePatchA, const TWeakObjectPtr<UDMXEntityFixturePatch>& WeakFixturePatchB)
				{
					const UDMXEntityFixturePatch* FixturePatchA = WeakFixturePatchA.Get();
					const UDMXEntityFixturePatch* FixturePatchB = WeakFixturePatchB.Get();

					const bool bUniverseIsLower = FixturePatchA->GetUniverseID() < FixturePatchB->GetUniverseID();
					const bool bChannelIsLower =
						FixturePatchA->GetUniverseID() == FixturePatchB->GetUniverseID() &&
						FixturePatchA->GetStartingChannel() <= FixturePatchB->GetStartingChannel();

					return bUniverseIsLower || bChannelIsLower;
				});

			if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = FindNodeByEntity(SelectedFixturePatches[0].Get())) 
			{
				RequestScrollIntoView(EntityNode);
			}
		}

		FixturePatchSharedData->SelectFixturePatches(SelectedFixturePatches);
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
	// Get the library that's being edited
	UDMXLibrary* Library = GetDMXLibrary();
	check(Library);

	// Get the Entities to paste from the clipboard
	TArray<UDMXEntity*> NewObjects = FDMXEditorUtils::CreateEntitiesFromClipboard(Library);
	if (NewObjects.Num() != 0)
	{
		// Start transaction for Undo and take a snapshot of the current Library state
		const FScopedTransaction PasteEntities(NewObjects.Num() > 1 ? LOCTEXT("PasteFixturePatches", "Paste Fixture Patches") : LOCTEXT("PasteFixturePatch", "Paste Fixture Patch"));
		Library->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));

		// Add each pasted Entity to the Library
		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewFixturePatches;
		for (UDMXEntity* NewEntity : NewObjects)
		{
			if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(NewEntity))
			{
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
					UDMXEntityFixtureType* FixtureType = (*SourceFixturePatchPtr)->GetFixtureType();
					FixturePatch->SetFixtureType(FixtureType);
				}

				// Move the Fixture Type template from the transient package into the Library package
				FixturePatch->Rename(*MakeUniqueObjectName(Library, UDMXEntityFixtureType::StaticClass()).ToString(), Library, REN_DoNotDirty | REN_DontCreateRedirectors);

				// Make sure the Entity's name won't collide with existing ones
				FixturePatch->SetName(FDMXRuntimeUtils::FindUniqueEntityName(Library, FixturePatch->GetClass(), FixturePatch->GetDisplayName()));

				// Update the library and ID
				FixturePatch->RefreshID();
				FixturePatch->SetParentLibrary(Library);
				AutoAssignCopiedPatch(FixturePatch);
			}
		}

		Library->PostEditChange();

		FixturePatchSharedData->SelectFixturePatches(NewFixturePatches);

		UpdateTree();
	}
}

bool SDMXFixturePatchTree::CanPasteNodes() const
{
	UDMXLibrary* Library = GetDMXLibrary();
	check(IsValid(Library));

	return FDMXEditorUtils::CanPasteEntities(Library);
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
		Library->PreEditChange(UDMXEntityFixtureType::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));

		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> NewFixturePatches;
		NewFixturePatches.Reserve(SelectedEntities.Num());

		// We'll have the duplicates be placed right after their original counterparts
		int32 NewEntityIndex = Library->FindEntityIndex(SelectedEntities.Last(0));
		for (UDMXEntity* Entity : SelectedEntities)
		{
			FObjectDuplicationParameters DuplicationParams(Entity, GetDMXLibrary());
			
			if (UDMXEntityFixturePatch* EntityCopy = CastChecked<UDMXEntityFixturePatch>(StaticDuplicateObjectEx(DuplicationParams)))
			{
				EntityCopy->RefreshID();
				EntityCopy->SetName(FDMXRuntimeUtils::FindUniqueEntityName(Library, EntityCopy->GetClass(), EntityCopy->GetDisplayName()));
				NewFixturePatches.Add(EntityCopy);

				Library->SetEntityIndex(EntityCopy, ++NewEntityIndex);

				if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(EntityCopy))
				{
					AutoAssignCopiedPatch(FixturePatch);
				}
			}
		}

		Library->PostEditChange();

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
	// Gather Fixture Patches to remove from the DMX Library
	const TArray<UDMXEntity*> EntitiesToDelete = GetSelectedEntities();

	TArray<UDMXEntityFixturePatch*> FixturePatchesToDelete;
	for (UDMXEntity* Entity : EntitiesToDelete)
	{
		if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity))
		{
			FixturePatchesToDelete.Add(FixturePatch);
		}
	}

	// Find a new selection
	const TSharedPtr<FDMXEntityTreeEntityNode> EntityNodeToSelect = [&FixturePatchesToDelete, this]() -> TSharedPtr<FDMXEntityTreeEntityNode>
	{
		const TArray<TSharedPtr<FDMXEntityTreeEntityNode>> EntityNodes = GetEntityNodes();

		for (const TSharedPtr<FDMXEntityTreeEntityNode>& EntityNode : EntityNodes)
		{
			if (EntityNode.IsValid() && !FixturePatchesToDelete.Contains(EntityNode->GetEntity()))
			{
				return EntityNode;
			}
		}
		return nullptr;
	}();

	// Apply the new selectio
	if (EntityNodeToSelect.IsValid())
	{
		if (UDMXEntityFixturePatch* FixturePatchToSelect = Cast<UDMXEntityFixturePatch>(EntityNodeToSelect->GetEntity()))
		{
			const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatchsToSelect = { FixturePatchToSelect };
			FixturePatchSharedData->SelectFixturePatches(FixturePatchsToSelect);
		}
	}
	else
	{
		// Clear selection if no patches remain
		FixturePatchSharedData->SelectFixturePatches(TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>());
	}

	// Remove the Fixture Patches from the DMX Library
	const FScopedTransaction Transaction(EntitiesToDelete.Num() > 1 ? LOCTEXT("RemoveEntities", "Remove Entities") : LOCTEXT("RemoveEntity", "Remove Entity"));

	UDMXLibrary* DMXLibrary = GetDMXLibrary();
	DMXLibrary->PreEditChange(UDMXLibrary::StaticClass()->FindPropertyByName(UDMXLibrary::GetEntitiesPropertyName()));
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatchesToDelete)
	{
		ensureMsgf(DMXLibrary == FixturePatch->GetParentLibrary(), TEXT("Unexpected DMX Library of Fixture Patch and DMX Library of Editor do not match when removing Fixture Patches."));
		const FDMXEntityFixturePatchRef FixturePatchRef(FixturePatch);
		UDMXEntityFixturePatch::RemoveFixturePatchFromLibrary(FixturePatchRef);
	}
	DMXLibrary->PostEditChange();

	UpdateTree();
}

bool SDMXFixturePatchTree::CanDeleteNodes() const
{
	return GetSelectedEntities().Num() > 0;
}

void SDMXFixturePatchTree::OnRenameNode()
{
	TArray<TSharedPtr<FDMXEntityTreeNodeBase>> SelectedItems = GetSelectedNodes();

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
	return GetSelectedNodes().Num() == 1 && GetSelectedNodes()[0]->CanRename();
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

TSharedPtr<SDMXFixturePatchTreeFixturePatchRow> SDMXFixturePatchTree::FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode) const
{
	if (const TSharedRef<SDMXFixturePatchTreeFixturePatchRow>* RowPtr = EntityNodeToEntityRowMap.Find(EntityNode))
	{
		return *RowPtr;
	}

	return nullptr;
}

TSharedPtr<FDMXEntityTreeCategoryNode> SDMXFixturePatchTree::FindCategoryNodeByUniverseID(int32 UniverseID, TSharedPtr<FDMXEntityTreeNodeBase> StartNode) const
{
	// Start at root node if none was provided
	if (!StartNode.IsValid())
	{
		StartNode = GetRootNode();
	}

	// Test the StartNode
	if (StartNode.IsValid() && StartNode->GetNodeType() == FDMXEntityTreeNodeBase::ENodeType::CategoryNode)
	{
		TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = StaticCastSharedPtr<FDMXEntityTreeCategoryNode>(StartNode);
		if (CategoryNode->GetCategoryType() == FDMXEntityTreeCategoryNode::ECategoryType::UniverseID &&
			CategoryNode->GetIntValue() == UniverseID)
		{
			return CategoryNode;
		}
	}

	// Test children recursively 
	for (const TSharedPtr<FDMXEntityTreeNodeBase>& ChildNode : StartNode->GetChildren())
	{
		TSharedPtr<FDMXEntityTreeCategoryNode> CategoryNode = FindCategoryNodeByUniverseID(UniverseID, ChildNode);
		if (CategoryNode.IsValid())
		{
			return CategoryNode;
		}
	}

	return nullptr;
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

void SDMXFixturePatchTree::OnFixtureTypeChanged(const UDMXEntityFixtureType* FixtureType)
{
	if (TSharedPtr<FDMXEditor> PinnedDMXEditor = DMXEditor.Pin())
	{
		if (PinnedDMXEditor->GetDMXLibrary() == FixtureType->GetParentLibrary())
		{
			UpdateTree();
		}
	}
}

void SDMXFixturePatchTree::OnFixturePatchesSelected()
{
	if (!bChangingFixturePatchSelection)
	{
		// Apply selection
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

		// Scroll into view
		if (NewSelection.Num() > 0)
		{
			if (const TSharedPtr<FDMXEntityTreeEntityNode> EntityNode = FindNodeByEntity(NewSelection[0]))
			{
				RequestScrollIntoView(EntityNode);
			}
		}
	}
}

void SDMXFixturePatchTree::OnUniverseSelected()
{
	const int32 SelectedUniverse = FixturePatchSharedData->GetSelectedUniverse();

	if (AutoExpandedUniverseCategoryNode.IsValid() &&
		!UserExpandedUniverseCategoryNodes.Contains(AutoExpandedUniverseCategoryNode))
	{
		SetNodeExpansion(AutoExpandedUniverseCategoryNode, false);
	}

	AutoExpandedUniverseCategoryNode = FindCategoryNodeByUniverseID(SelectedUniverse);
	if (AutoExpandedUniverseCategoryNode.IsValid())
	{
		SetNodeExpansion(AutoExpandedUniverseCategoryNode, true);
		RequestScrollIntoView(AutoExpandedUniverseCategoryNode);
	}
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
			UDMXLibrary* DMXLibrary = FixtureType->GetParentLibrary();
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

			const FScopedTransaction Transaction(LOCTEXT("CreateFixturePatchTransaction", "Create DMX Fixture Patch"));

			FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
			FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);

			UDMXEntityFixturePatch* NewFixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, FixtureType->Name);
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

void SDMXFixturePatchTree::OnAutoAssignChannelStateChanged(bool NewState, TSharedPtr<FDMXEntityTreeEntityNode> InNode)
{
	const FScopedTransaction Transaction(LOCTEXT("SetAutoAssignChannelTransaction", "Set Auto Assign Channel"));

	TArray<UDMXEntityFixturePatch*> ChangedPatches;

	// Was the changed entity one of the selected ones?
	if (IsNodeSelected(InNode))
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
		if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(InNode->GetEntity()))
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
