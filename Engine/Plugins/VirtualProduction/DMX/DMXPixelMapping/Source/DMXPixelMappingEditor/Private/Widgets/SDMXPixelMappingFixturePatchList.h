// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntityReference.h"
#include "Widgets/SDMXReadOnlyFixturePatchList.h"

class FDMXPixelMappingToolkit;
class UDMXEntityFixturePatch;


/** Displays the DMX Library of the currently selected fixture group component */
class SDMXPixelMappingFixturePatchList final
	: public SDMXReadOnlyFixturePatchList
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingFixturePatchList) 
	{}
		/** Describes the initial state of the list */
		SLATE_ARGUMENT(FDMXReadOnlyFixturePatchListDescriptor, ListDescriptor)

		/** Called when a row of the list is right clicked */
		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)

		/** Delegate executed when a row was dragged */
		SLATE_EVENT(FOnDragDetected, OnRowDragged)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit);

	/** Selects the first patch after the specified fixture patches */
	void SelectAfter(const TArray<TSharedPtr<FDMXEntityFixturePatchRef>>& FixturePatches);

private:
	//~ Begin SDMXReadOnlyFixturePatchList interface
	virtual void RefreshList() override;
	//~ End SDMXReadOnlyFixturePatchList interface

	/** Holds the fixutre patches which are hidden the list */
	TArray<TSharedPtr<FDMXEntityFixturePatchRef>> HiddenFixturePatches;

	/** The toolkit of the editor that displays this widget */
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
};
