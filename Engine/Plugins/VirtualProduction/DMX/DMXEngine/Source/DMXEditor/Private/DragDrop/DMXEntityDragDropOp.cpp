// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEntityDragDropOp.h"

#include "DMXEditorUtils.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Widgets/SDMXEntityList.h"

#include "Dialogs/Dialogs.h"

#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SDMXEntityListBase"

///////////////////////////////////////////////////////////////////////////////
// FDMXEntityDragDropOperation

FDMXEntityDragDropOperation::FDMXEntityDragDropOperation(UDMXLibrary* InLibrary, const TArray<TWeakObjectPtr<UDMXEntity>>& InEntities)
	: DraggedFromLibrary(InLibrary)
	, DraggedEntities(InEntities)
{
	DraggedEntitiesName = DraggedEntities.Num() == 1
		? FText::FromString(TEXT("'") + DraggedEntities[0]->GetDisplayName() + TEXT("'"))
		: FDMXEditorUtils::GetEntityTypeNameText(DraggedEntities[0]->GetClass(), true);

	Construct();
}

void FDMXEntityDragDropOperation::Construct()
{
	// Create the drag-drop decorator window
	CursorDecoratorWindow = SWindow::MakeCursorDecorator();
	const bool bShowImmediately = false;
	FSlateApplication::Get().AddWindow(CursorDecoratorWindow.ToSharedRef(), bShowImmediately);
}

void FDMXEntityDragDropOperation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DraggedFromLibrary);
}

void FDMXEntityDragDropOperation::HandleDragEnterCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow)
{
	bool bShowFeedback = true;
	TestCanDropOnCategoryRow(CategoryRow, bShowFeedback);
}

bool FDMXEntityDragDropOperation::HandleDropOnCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow)
{
	if (TSharedPtr<SDMXEntityList> EntityListPinned = CategoryRow->GetEntityList().Pin())
	{
		bool bShowFeedback = false;
		if (!TestCanDropOnCategoryRow(CategoryRow, bShowFeedback))
		{
			return false;
		}

		UDMXLibrary* Library = EntityListPinned->GetDMXLibrary();
		check(Library && DraggedFromLibrary == Library);

		TSharedPtr<FDMXCategoryTreeNode> CategoryNode = CategoryRow->GetNode();
		check(CategoryNode.IsValid());

		// Register transaction and current DMX library state for Undo
		const FScopedTransaction ChangeCategoryTransaction = FScopedTransaction(
			FText::Format(LOCTEXT("ChangeEntitiesCategory", "Change {0}|plural(one=Entity, other=Entities) category"), DraggedEntities.Num())
		);
		EntityListPinned->GetDMXLibrary()->Modify();

		switch (CategoryNode->GetCategoryType())
		{
			case FDMXTreeNodeBase::ECategoryType::DMXCategory:
			{
				const FDMXFixtureCategory& FixtureCategory = CategoryNode->GetCategoryValue();
				for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
				{
					if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
					{
						FixtureType->Modify();

						FixtureType->PreEditChange(nullptr);
						FixtureType->DMXCategory = FixtureCategory;
						FixtureType->PostEditChange();
					}
				}
			}
			break;

			case FDMXTreeNodeBase::ECategoryType::UniverseID:
			case FDMXTreeNodeBase::ECategoryType::FixtureAssignmentState:
			{
				int32 UniverseID = CategoryNode->GetIntValue();
				for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
				{
					if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity))
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
			}

			default:
				// The other category types don't change properties
				break;
		}

		if (CategoryNode->GetChildren().Num() > 0)
		{
			// Index after last entity in hovered category
			UDMXEntity* LastEntityInCategory = CategoryNode->GetChildren().Last(0)->GetEntity();
			const int32 LastEntityIndex = Library->FindEntityIndex(LastEntityInCategory);
			check(LastEntityIndex != INDEX_NONE);

			// Move dragged entities after the last ones in the category
			// Reverse for to keep dragged entities order.
			int32 DesiredIndex = LastEntityIndex + 1;
			for (int32 EntityIndex = DraggedEntities.Num() - 1; EntityIndex > -1; --EntityIndex)
			{
				if (UDMXEntity* Entity = DraggedEntities[EntityIndex].Get())
				{
					Library->SetEntityIndex(Entity, DesiredIndex);
				}
			}
		}

		return true;
	}

	return false;
}

void FDMXEntityDragDropOperation::HandleDragEnterEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow)
{
	bool bShowFeedback = true;
	TestCanDropOnEntityRow(EntityRow, bShowFeedback);
}

bool FDMXEntityDragDropOperation::HandleDropOnEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow)
{
	if (TSharedPtr<SDMXEntityList> EntityListPinned = EntityRow->GetEntityList().Pin())
	{
		UDMXLibrary* Library = EntityListPinned->GetDMXLibrary();
		check(Library && DraggedFromLibrary == Library);

		TSharedPtr<FDMXEntityTreeNode> Node = EntityRow->GetNode();
		check(Node);

		UDMXEntity* HoveredEntity = Node->GetEntity();
		check(HoveredEntity);

		bool bShowFeedback = false;
		if (!TestCanDropOnEntityRow(EntityRow, bShowFeedback))
		{
			return false;
		}

		// Register transaction and current DMX library state for Undo
		const FScopedTransaction ReorderTransaction = FScopedTransaction(
			FText::Format(LOCTEXT("ReorderEntities", "Reorder {0}|plural(one=Entity, other=Entities)"), GetDraggedEntities().Num())
		);

		Library->Modify();

		// The index of the Entity, we're about to insert the dragged one before
		const int32 InsertBeforeIndex = Library->FindEntityIndex(HoveredEntity);
		check(InsertBeforeIndex != INDEX_NONE);

		// Reverse for to keep dragged entities order
		for (int32 EntityIndex = DraggedEntities.Num() - 1; EntityIndex > -1; --EntityIndex)
		{
			if (DraggedEntities[EntityIndex].IsValid())
			{
				UDMXEntity* Entity = DraggedEntities[EntityIndex].Get();
				Library->SetEntityIndex(Entity, InsertBeforeIndex);
			}
		}

		// Handle cases where it was was dropped on another category
		UClass* DraggedType = nullptr;
		if (GetDraggedEntityTypes().Num() == 1)
		{
			DraggedType = GetDraggedEntityTypes()[0];
		}

		TSharedPtr<FDMXCategoryTreeNode> TargetCategory = StaticCastSharedPtr<FDMXCategoryTreeNode>(Node->GetParent().Pin());
		check(TargetCategory.IsValid());

		switch (TargetCategory->GetCategoryType())
		{
			case FDMXTreeNodeBase::ECategoryType::DMXCategory:
			{
				const FDMXFixtureCategory& FixtureCategory = TargetCategory->GetCategoryValue();
				for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
				{
					if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
					{
						FixtureType->Modify();
						FixtureType->DMXCategory = FixtureCategory;
					}
				}
			}
			break;

			case FDMXTreeNodeBase::ECategoryType::UniverseID:
			case FDMXTreeNodeBase::ECategoryType::FixtureAssignmentState:
			{
				const uint32& UniverseID = TargetCategory->GetIntValue();

				for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
				{
					if (UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(Entity))
					{
						FixturePatch->Modify();

						FDMXEditorUtils::AutoAssignedAddresses(TArray<UDMXEntityFixturePatch*>({ FixturePatch }));
						FixturePatch->SetUniverseID(UniverseID);
					}
				}
			}

			default:
				// The other category types don't change properties
				break;
		}
	}

	return true;
}

TArray<UClass*> FDMXEntityDragDropOperation::GetDraggedEntityTypes() const
{
	TArray<UClass*> EntityClasses;
	for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
	{
		if (!Entity.IsValid())
		{
			continue;
		}

		EntityClasses.Add(Entity->GetClass());
	}

	return EntityClasses;
}

void FDMXEntityDragDropOperation::SetFeedbackMessageError(const FText& Message)
{
	const FSlateBrush* StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
	SetFeedbackMessage(StatusSymbol, Message);
}

void FDMXEntityDragDropOperation::SetFeedbackMessageOK(const FText& Message)
{
	const FSlateBrush* StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
	SetFeedbackMessage(StatusSymbol, Message);
}

void FDMXEntityDragDropOperation::SetFeedbackMessage(const FSlateBrush* Icon, const FText& Message)
{
	if (!Message.IsEmpty())
	{
		CursorDecoratorWindow->ShowWindow();
		CursorDecoratorWindow->SetContent
		(
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(3.0f)
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage)
						.Image(Icon)
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(3.0f)
				.MaxWidth(500)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.WrapTextAt(480.0f)
					.Text(Message)
					.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
				]
			]
		);
	}
	else
	{
		CursorDecoratorWindow->HideWindow();
		CursorDecoratorWindow->SetContent(SNullWidget::NullWidget);
	}
}

void FDMXEntityDragDropOperation::SetCustomFeedbackWidget(const TSharedRef<SWidget>& Widget)
{
	CursorDecoratorWindow->ShowWindow();
	CursorDecoratorWindow->SetContent
	(
		Widget
	);
}

TArray<TSharedPtr<FDMXTreeNodeBase>> FDMXEntityDragDropOperation::GetDraggedFromCategories(const TSharedPtr<SDMXEntityList>& EntityList) const
{
	TArray<TSharedPtr<FDMXTreeNodeBase>> TreeNodes;
	for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
	{
		if (Entity.IsValid())
		{
			TSharedPtr<FDMXTreeNodeBase> Node = EntityList->GetCategoryNode(Entity.Get());
			check(Node.IsValid());

			TreeNodes.Add(Node);
		}
	}

	return TreeNodes;
}

bool FDMXEntityDragDropOperation::TestCanDropOnCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow, bool bShowFeedback)
{
	if (TSharedPtr<SDMXEntityList> EntityList = CategoryRow->GetEntityList().Pin())
	{
		TSharedPtr<FDMXCategoryTreeNode> CategoryNode = CategoryRow->GetNode();
		check(CategoryNode.IsValid());
		
		UDMXLibrary* Library = EntityList->GetDMXLibrary();
		check(Library);

		if (CategoryNode.IsValid())
		{

			if (!CategoryNode->CanDropOntoCategory())
			{
				if (bShowFeedback)
				{
					// There wouldn't be any change by dragging the items into their own category.
					SetFeedbackMessageError(FText::Format(
						LOCTEXT("DragIntoNoValueCategory", "Cannot drop {0} onto this category"),
						GetDraggedEntitiesName()
					));
				}
				return false;
			}

			TArray<TSharedPtr<FDMXTreeNodeBase>> DraggedFromCategories = GetDraggedFromCategories(EntityList);

			if (DraggedFromCategories.Num() == 1 && DraggedFromCategories[0] == CategoryNode)
			{
				if (bShowFeedback)
				{
					// There wouldn't be any change by dragging the items into their own category.
					SetFeedbackMessageError(FText::Format(
						LOCTEXT("DragIntoSelfCategory", "The selected {0} {1}|plural(one=is, other=are) already in this category"),
						FDMXEditorUtils::GetEntityTypeNameText(EntityList->GetListType(), DraggedEntities.Num() > 1),
						DraggedEntities.Num()
					));
				}

				return false;
			}
			else
			{
				if (bShowFeedback)
				{
					// Some (or all) items will have a property changed because they come from another category
					FText PropertyChangeName;
					FText PropertyNewValue;

					TSharedPtr<FDMXCategoryTreeNode> TargetCategory = CategoryRow->GetNode();
					check(TargetCategory.IsValid());

					// Generate 'OK' feedback per category
					FText ListTypeText;
					FText CategoryText;

					const TSubclassOf<UDMXEntity> ListType = EntityList->GetListType();
					if (ListType == UDMXEntityFixtureType::StaticClass())
					{
						ListTypeText = LOCTEXT("Property_DMXCategory", "DMX Category");
						CategoryText = TargetCategory->GetDisplayName();
					}
					else if (ListType == UDMXEntityFixturePatch::StaticClass())
					{
						ListTypeText = LOCTEXT("Property_Universe", "Universe");
						const uint32 UniverseID = TargetCategory->GetIntValue();
						CategoryText = FText::AsNumber(UniverseID);
					}

					SetFeedbackMessageOK(FText::Format(
						LOCTEXT("ReorderBeforeAndSetProperty", "Reorder {0} before '{1}'\nSet {2} = '{3}'"),
						GetDraggedEntitiesName(),
						TargetCategory->GetDisplayName(),
						ListTypeText,
						CategoryText
					));
				}

				return true;
			}
		}
	}

	return false;
}

bool FDMXEntityDragDropOperation::TestCanDropOnEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow, bool bShowFeedback)
{
	if (TSharedPtr<SDMXEntityList> EntityList = EntityRow->GetEntityList().Pin())
	{
		TSharedPtr<FDMXEntityTreeNode> Node = EntityRow->GetNode();
		check(Node);

		UDMXEntity* HoveredEntity = Node->GetEntity();
		check(HoveredEntity);

		int32 NumDraggedEntities = DraggedEntities.Num();

		// Test the drag is comming from the same library
		UDMXLibrary* Library = EntityList->GetDMXLibrary();
		check(Library);
		if (!TestLibraryEquals(Library, bShowFeedback))
		{
			return false;
		}

		// Test if the dragdrop op holds a valid object
		if (!TestHasValidEntities(bShowFeedback))
		{
			return false;
		}

		// Test the drag is of same type
		if (!TestAreDraggedEntitiesOfClass(HoveredEntity->GetClass()))
		{
			return false;
		}

		const TSubclassOf<UDMXEntity> ListType = EntityList->GetListType();
		if (!TestAreDraggedEntitiesOfClass(ListType, bShowFeedback))
		{
			return false;
		}

		// Test the hovered entity is not the dragged entitiy
		if (DraggedEntities.Contains(HoveredEntity))
		{
			if (bShowFeedback)
			{
				SetFeedbackMessageError(FText::Format(
					LOCTEXT("ReorderBeforeItself", "Drop {0} on itself"),
					GetDraggedEntitiesName()
				));
			}

			return false;
		}

		// For Fixture Patches, test if the hovered entity resides in the same universe
		if (UDMXEntityFixturePatch* HoveredFixturePatch = Cast<UDMXEntityFixturePatch>(HoveredEntity))
		{
			if (!TestCanDropOnFixturePatch(HoveredFixturePatch, bShowFeedback))
			{
				return false;
			}
		}

		check(Node->GetParent().IsValid());
		TSharedPtr<FDMXCategoryTreeNode> TargetCategory = StaticCastSharedPtr<FDMXCategoryTreeNode>(Node->GetParent().Pin());
		check(TargetCategory.IsValid());

		// Generate 'OK' feedback per category
		FText ListTypeText;
		FText CategoryText;

		if (ListType == UDMXEntityFixtureType::StaticClass())
		{
			ListTypeText = LOCTEXT("Property_DMXCategory", "DMX Category");
			CategoryText = TargetCategory->GetDisplayName();
		}
		else if (ListType == UDMXEntityFixturePatch::StaticClass())
		{
			ListTypeText = LOCTEXT("Property_Universe", "Universe");
			const uint32 UniverseID = TargetCategory->GetIntValue();
			CategoryText = UniverseID == MAX_uint32
				? LOCTEXT("UnassignedUniverseIDValue", "Unassigned")
				: FText::AsNumber(UniverseID);
		}

		SetFeedbackMessageOK(FText::Format(
			LOCTEXT("ReorderBeforeAndSetProperty", "Reorder {0} before '{1}'\nSet {2} = '{3}'"),
			GetDraggedEntitiesName(),
			FText::FromString(HoveredEntity->GetDisplayName()),
			ListTypeText,
			CategoryText
		));

		return true;
	}

	return false;
}

bool FDMXEntityDragDropOperation::TestLibraryEquals(UDMXLibrary* DragToLibrary, bool bShowFeedback)
{
	check(DragToLibrary);
	if (DragToLibrary && DragToLibrary == DraggedFromLibrary)
	{
		return true;
	}

	if (bShowFeedback)
	{
		// For now, we don't allow dragging entities from one library to the other
		SetFeedbackMessageError(FText::Format(
			LOCTEXT("CantDragToDifferentLibrary", "Cannot move {0} outside {1}|plural(one=its, other=their) library"),
			GetDraggedEntitiesName(),
			GetDraggedEntities().Num()
		));
	}

	return false;
}

bool FDMXEntityDragDropOperation::TestHasValidEntities(bool bShowFeedback /** = true */)
{
	const TWeakObjectPtr<UDMXEntity>* ValidEntityPtr =
		DraggedEntities.FindByPredicate([](TWeakObjectPtr<UDMXEntity> Entity) {
			return Entity.IsValid();
		});

	if (ValidEntityPtr)
	{
		return true;
	}

	SetFeedbackMessageError(
		LOCTEXT("DragNullAssetError", "Dragged object no longer available")
	);

	return false;
}

bool FDMXEntityDragDropOperation::TestAreDraggedEntitiesOfClass(TSubclassOf<UDMXEntity> EntityClass, bool bShowFeedback)
{
	for (TWeakObjectPtr<UDMXEntity> Entity : DraggedEntities)
	{
		if (!Entity.IsValid())
		{
			continue;
		}

		if (!Entity->IsA(EntityClass))
		{
			if (bShowFeedback)
			{
				// Don't allow dragging entities from a type onto a different type tab
				SetFeedbackMessageError(FText::Format(
					LOCTEXT("CantDragToDifferentType", "Cannot move {0} to {1} editor"),
					GetDraggedEntitiesName(),
					FDMXEditorUtils::GetEntityTypeNameText(EntityClass, GetDraggedEntities().Num() > 1)
				));
			}

			return false;
		}
	}

	return true;
}

bool FDMXEntityDragDropOperation::TestCanDropOnFixturePatch(UDMXEntityFixturePatch* HoveredFixturePatch, bool bShowFeedback)
{
	check(TestAreDraggedEntitiesOfClass(UDMXEntityFixturePatch::StaticClass(), false));

	bool bDragBetweenUniverses = false;
	for (TWeakObjectPtr<UDMXEntity> DraggedEntity : DraggedEntities)
	{
		if (UDMXEntityFixturePatch* DraggedFixturePatch = Cast<UDMXEntityFixturePatch>(DraggedEntity))
		{
			if (DraggedFixturePatch->GetUniverseID() != HoveredFixturePatch->GetUniverseID())
			{
				bDragBetweenUniverses = true;
				break;
			}
		}
	}

	if (!bDragBetweenUniverses)
	{
		if (bShowFeedback)
		{
			SetFeedbackMessageError(FText::Format(
				LOCTEXT("DragFixturePatchToItsOwnUniverse", "{0} already resides in this universe."),
				GetDraggedEntitiesName()
			));
		}

		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
