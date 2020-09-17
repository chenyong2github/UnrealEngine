// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "Library/DMXEntityFixtureType.h"
#include "Widgets/SDMXNamedTypeRow.h"

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "EditorUndoClient.h"

class FDMXEditor;
class FDMXFixtureModeItem;
class FDMXFixtureTypeSharedData;
class UDMXEntityFixtureType;
class SDMXModeItemListViewBox;

class IPropertyHandle;
class IPropertyHandleArray;
class FUICommandList;
class IPropertyUtilities;


using SDMXModeItemListView = SListView<TSharedPtr<FDMXFixtureModeItem>>;

/** Details customization for 'FixtureType Modes' Details View, displaying mode names in a list */
class FDMXEntityFixtureTypeModesDetails
	: public IDetailCustomization
	, public FEditorUndoClient
{
public:
	/** Constructor */
	FDMXEntityFixtureTypeModesDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

protected:
	/** Creates the 'Add New Mode' Button */
	TSharedRef<SWidget> CreateAddModeButton() const;

	/** Called to determine wether the 'Add New Mode' button is enabled */
	bool GetIsAddModeButtonEnabled() const;

	/** Called to get the tooltip text of the 'Add New Mode' button */
	FText GetAddModeButtonTooltipText() const;

	/** Called when the 'Add New Mode' button was clicked */
	FReply OnAddModeButtonClicked() const;

	/** Called to get the text of the 'Add New Mode' button */
	FText GetAddModeButtonText() const;

protected:
	/** Modes Property Handle Array */
	TSharedPtr<IPropertyHandleArray> ModesHandleArray;

	/** Cached Property Utilities for this Details Customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** The widget holds list where the user can select modes names */
	TSharedPtr<SDMXModeItemListViewBox> ListViewBox;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
