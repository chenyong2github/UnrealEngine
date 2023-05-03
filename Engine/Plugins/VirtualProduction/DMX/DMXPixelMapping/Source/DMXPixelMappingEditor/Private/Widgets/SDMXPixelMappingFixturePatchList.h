// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntityReference.h"

#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"

class FDMXPixelMappingToolkit;
class SDMXReadOnlyFixturePatchList;
class UDMXEntityFixturePatch;
class UDMXPixelMapping;
class UDMXPixelMappingBaseComponent;
class UDMXPixelMappingDMXLibraryViewModel;
class UDMXLibrary;

class FReply;


/** Displays the DMX Library of the currently selected fixture group component */
class SDMXPixelMappingFixturePatchList final
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingFixturePatchList) 
	{}

	SLATE_END_ARGS()

	/** Destructor */
	~SDMXPixelMappingFixturePatchList();

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit, UDMXPixelMappingDMXLibraryViewModel* InViewModel);

protected:
	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

private:
	/** Called when a row was dragged */
	FReply OnRowDragged(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Called when a component was added to or removed from the pixel mapping */
	void OnComponentAddedOrRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);
	
	/** Called when the 'Add selected Patches' button was clicked */
	FReply OnAddSelectedPatchesClicked();

	/** Called when the 'Add all Patches' button was clicked */
	FReply OnAddAllPatchesClicked();

	/** Selects the first patch after the specified fixture patch */
	void SelectAfter(const UDMXEntityFixturePatch* FixturePatch);

	/** Adds specified fixture patches to the pixel mapping */
	void AddFixturePatches(const TArray<TSharedPtr<FDMXEntityFixturePatchRef>>& FixturePatches);

	/** Layouts components evenly over the parent group */
	void LayoutEvenOverParent(const TArray<UDMXPixelMappingBaseComponent*> Components);

	/** Layouts components after the last patch in the parent group */
	void LayoutAfterLastPatch(const TArray<UDMXPixelMappingBaseComponent*> Components);

	/** The fixture patch list displayed in this widget */
	TSharedPtr<SDMXReadOnlyFixturePatchList> FixturePatchList;

	/** The view model used in this widget */
	TWeakObjectPtr<UDMXPixelMappingDMXLibraryViewModel> WeakViewModel;

	/** The toolkit of the editor that displays this widget */
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
};
