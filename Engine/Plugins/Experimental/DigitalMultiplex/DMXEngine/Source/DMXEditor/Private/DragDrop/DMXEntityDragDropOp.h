// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/DragAndDrop.h"
#include "UObject/GCObject.h"

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FDMXTreeNodeBase;
class SDMXCategoryRow;
class FDMXEntityTreeNode;
class SDMXEntityRow;
class SDMXEntityList;
class UDMXEntity;
class UDMXEntityFixturePatch;
class UDMXLibrary;
struct FSlateBrush;

class FDMXEntityDragDropOperation
	: public FDragDropOperation
	, public FGCObject
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDMXEntityDragDropOperation, FDragDropOperation)

	/**
	 * Constructs the entity drag drop operation
	 *
	 *
	 *
	 *
	 */
	FDMXEntityDragDropOperation(UDMXLibrary* InLibrary, const TArray<TWeakObjectPtr<UDMXEntity>>& InEntities);

protected:
	/** Constructs the tooltip widget that follows the mouse */
	virtual void Construct() override;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

public:
	/** Handles op entering a category row */
	void HandleDragEnterCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow);

	/** Handles dropping the op on a category row, returns true if successfully dropped */
	bool HandleDropOnCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow);

	/** Handles op entering an entity row */
	void HandleDragEnterEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow);

	/** Handles dropping the op on an entity row, returns true if successfully dropped */
	bool HandleDropOnEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow);

	/** Returns the entities dragged with this drag drop op */
	const TArray<TWeakObjectPtr<UDMXEntity>>& GetDraggedEntities() const { return DraggedEntities; }

	/** Returns the dragged entity's names or the first of the dragged entities names */
	const FText& GetDraggedEntitiesName() const { return DraggedEntitiesName; }

	/** Returns the types of the entities */
	TArray<UClass*> GetDraggedEntityTypes() const;

	/** Sets the cursor decorrator to show an error sign and a message */
	void SetFeedbackMessageError(const FText& Message);

	/** Sets the cursor decorrator to show an ok sign and a message */
	void SetFeedbackMessageOK(const FText& Message);

	/** Sets the cursor decorator to show an icon and a message */
	void SetFeedbackMessage(const FSlateBrush* Icon, const FText& Message);

	/** Sets a custom cursor decorator */
	void SetCustomFeedbackWidget(const TSharedRef<SWidget>& Widget);

private:
	/** Returns the categories the entities were dragged out of */
	TArray<TSharedPtr<FDMXTreeNodeBase>> GetDraggedFromCategories(const TSharedPtr<SDMXEntityList>& EntityList) const;
	
	/**
	 * Tests if the drag drop op can be dropped on specified category row.
	 * Updates Feedback Message if bShowFeedback.
	 */
	bool TestCanDropOnCategoryRow(const TSharedPtr<SDMXCategoryRow>& CategoryRow, bool bShowFeedback = true);

	/**
	 * Tests if the drag drop op can be dropped on specified entity row.
	 * Updates Feedback Message if bShowFeedback.
	 */
	bool TestCanDropOnEntityRow(const TSharedPtr<SDMXEntityRow>& EntityRow, bool bShowFeedback = true);

	/**
	 * Tests if the drag drop op can be dropped on specified library.
	 * Updates Feedback Message if bShowFeedback.
	 */
	bool TestLibraryEquals(UDMXLibrary* DragToLibrary, bool bShowFeedback = true);

	/**
	 * Tests if the entity is valid.
	 * Updates Feedback Message if bShowFeedback.
	*/
	bool TestHasValidEntities(bool bShowFeedback = true);

	/**
	 * Tests if the entity is valid.
	 * Updates Feedback Message if bShowFeedback.
	*/
	bool TestAreDraggedEntitiesOfClass(TSubclassOf<UDMXEntity> EntityClass, bool bShowFeedback = true);

	/**
	 * Tests if the drag drop op can be dropped on the hovered fixture patch 
	 * Updates Feedback Message if bShowFeedback.
	 */
	bool TestCanDropOnFixturePatch(UDMXEntityFixturePatch* HoveredFixturePatch, bool bShowFeedback = true);

	/** The library that the entites were dragged out from */
	UDMXLibrary* DraggedFromLibrary;

	/** The entities being draged with this drag drop op */
	TArray<TWeakObjectPtr<UDMXEntity>> DraggedEntities;

	/** Name of the entity being dragged or entities type for several ones */
	FText DraggedEntitiesName;
};
