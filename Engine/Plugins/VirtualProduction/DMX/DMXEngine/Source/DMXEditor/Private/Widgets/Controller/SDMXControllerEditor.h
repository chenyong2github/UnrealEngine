// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXEntityEditor.h"

#include "CoreMinimal.h"
#include "Widgets/SDMXEntityList.h"

class FDMXEditor;
class SDMXEntityInspector;
struct FPropertyChangedEvent;


/** Editor for Controllers */
class SDMXControllerEditor
	: public SDMXEntityEditor
{
public:
	SLATE_BEGIN_ARGS(SDMXControllerEditor)
		: _DMXEditor(nullptr)
	{}

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

	SLATE_END_ARGS()

public:
	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Begin SDMXEntityEditorTab interface */
	void RequestRenameOnNewEntity(const UDMXEntity* InEntity, ESelectInfo::Type SelectionType);
	void SelectEntity(UDMXEntity* InEntity, ESelectInfo::Type InSelectionType = ESelectInfo::Type::Direct);
	void SelectEntities(const TArray<UDMXEntity*>& InEntities, ESelectInfo::Type SelectionType = ESelectInfo::Type::Direct);
	TArray<UDMXEntity*> GetSelectedEntities() const;
	/** ~End SDMXEntityEditorTab interface */

protected:
	/** Callback for when entities list selection changes */
	void OnSelectionUpdated(TArray<UDMXEntity*> InSelectedEntities);

	/** Callback for when some property has changed in the inspector */
	virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

protected:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> DMXEditor;

	/** Left child widget */
	TSharedPtr<SDMXEntityList> EntityList;

	/** Right child widget */
	TSharedPtr<SDMXEntityInspector> InspectorWidget;
};
