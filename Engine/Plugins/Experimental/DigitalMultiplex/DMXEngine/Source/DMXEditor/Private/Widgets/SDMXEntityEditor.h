// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SDMXEntityList.h"

#include "Types/SlateEnums.h"

class FDMXEditor;
class SSplitter;
class SBox;
class SDMXEntityInspector;
struct FPropertyChangedEvent;

/** DMX Controllers tab root widget */
class SDMXEntityEditor
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXEntityEditor)
		: _DMXEditor(nullptr)
	{}
	
	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ARGUMENT(TSubclassOf<UDMXEntity>, EditorEntityType)

	SLATE_END_ARGS()

public:
	SDMXEntityEditor()
	{
		SetCanTick(false);
		bCanSupportFocus = false;
	}

	/** Constructs the sides container widgets. The child classes should use
	* SetLeftSideWidget and SetRightSideWidget to add the child widgets into the containers. */
	void Construct(const FArguments& InArgs);

	/** Get the current left side column's widget */
	const TSharedPtr<SDMXEntityList> GetListWidget() const { return ListWidget; }
	/** Get the current right side column's widget */
	const TSharedPtr<SDMXEntityInspector> GetInspectorWidget() const { return InspectorWidget; }

	/**
	 * Refreshes the list of entities to display any added entities, select the new entity by name
	 * and initiates a rename on the selected Entity node
	 */
	void RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType);

	/** Selects an entity in this editor tab's list */
	void SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType = ESelectInfo::Type::Direct);
	/** Selects Entities in this editor tab's list */
	void SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type SelectionType = ESelectInfo::Type::Direct);
	/** Returns the selected entities on this editor tab */
	TArray<UDMXEntity*> GetSelectedEntities() const;

protected:
	/** Callback for when entities list selection changes */
	virtual void OnSelectionUpdated(TArray<TSharedPtr<FDMXTreeNodeBase>> InSelectedNodes);

	/** Callback for when some property has changed in the inspector */
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

protected:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> DMXEditor;

	/** Contains both sides' columns */
	TSharedPtr<SSplitter> SidesSplitter;

	/** Left widget parent */
	TSharedPtr<SBox> ListContainer;

	/** Right widget parent */
	TSharedPtr<SBox> InspectorContainer;

	/** Left child widget */
	TSharedPtr<SDMXEntityList> ListWidget;

	/** Right child widget */
	TSharedPtr<SDMXEntityInspector> InspectorWidget;

	/** Indicates which tab we are */
	TSubclassOf<UDMXEntity> EditorEntityType;
};

/** DMX Controllers tab root widget */
class SDMXControllers
	: public SDMXEntityEditor
{
public:
	SLATE_BEGIN_ARGS(SDMXControllers)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};


/** DMX Controllers tab root widget */
class SDMXFixtureTypes
	: public SDMXEntityEditor
{
public:
	SLATE_BEGIN_ARGS(SDMXFixtureTypes)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	//~ SDMXEntityEditor interface
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent) override;
}; 


/** DMX Controllers tab root widget */
class SDMXFixturePatch
	: public SDMXEntityEditor
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatch)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);
};
