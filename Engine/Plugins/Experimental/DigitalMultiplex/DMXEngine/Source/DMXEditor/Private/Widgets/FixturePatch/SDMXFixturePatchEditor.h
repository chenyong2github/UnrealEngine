// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXEntityEditor.h"

#include "CoreMinimal.h"
#include "Widgets/SDMXEntityList.h"

class FDMXEditor;
class SDMXEntityInspector;
class SDMXFixturePatcher;
class UDMXEntityFixturePatch;

struct FPropertyChangedEvent;


/** Editor for Fixture Patches */
class SDMXFixturePatchEditor
	: public SDMXEntityEditor
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatchEditor)
		: _DMXEditor(nullptr)
	{}
	
		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

public:
	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

public:
	/** Begin SDMXEntityEditorTab interface */
	void RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType);
	void SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType = ESelectInfo::Type::Direct);
	void SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type SelectionType = ESelectInfo::Type::Direct);
	TArray<UDMXEntity*> GetSelectedEntities() const;
	/** ~End SDMXEntityEditorTab interface */

protected:
	/** Selects the patch */
	void SelectUniverse(int32 UniverseID);

	/** Callback for when entities list changes the auto assign flag of a patch */
	void OnEntitiyListChangedAutoAssignAddress(TArray<UDMXEntityFixturePatch*> ChangedPatches);

	/** Called when the entity list added entities to the library */
	void OnEntityListAddedEntities();

	/** Called when the entity list changed the entity order */
	void OnEntityListChangedEntityOrder();

	/** Called when the entity list removed entities from the library */
	void OnEntityListRemovedEntities();

	/** Called when the fixture patcher patched a fixture patch */
	void OnFixturePatcherPatchedFixturePatch();

	/** Callback for when some property has changed in the inspector */
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

protected:
	/** Makes an initial selection in shared data if required */
	void MakeInitialSelection();

	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> DMXEditorPtr;

	/** Left child widget */
	TSharedPtr<SDMXEntityList> EntityList;

	/** Right child widget */
	TSharedPtr<SDMXEntityInspector> InspectorWidget;

	/** Widget where the user can drag drop fixture patches */
	TSharedPtr<SDMXFixturePatcher> FixturePatcher;
}; 
