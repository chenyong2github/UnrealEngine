// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntityReference.h"

#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"

class FDMXPixelMappingToolkit;
class SDMXReadOnlyFixturePatchList;
class UDMXPixelMappingFixtureGroupComponent;
class UDMXPixelMappingDMXLibraryViewModel;
class UDMXPixelMappingRootComponent;

class FReply;
class IDetailsView;
class SScrollBox;


/** Displays the DMX Library of the currently selected fixture group component */
class SDMXPixelMappingDMXLibraryView
	: public SCompoundWidget
	, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingDMXLibraryView) { }
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit);

	/** Refreshes the widget on the next tick */
	void RequestRefresh();

protected:
	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("SDMXPixelMappingDMXLibraryView");
	}
	//~ End FGCObject interface

private:
	/** Refreshes the widget */
	void ForceRefresh();

	/** Called when entities were added to or removed from a DMX Library */
	void OnEntitiesAddedOrRemoved(UDMXLibrary* DMXLibrary, TArray<UDMXEntity*> Entities);

	/** Called when a component was selected */
	void OnComponentSelected();

	/** Called when the 'Add DMX Library' button was clicked */
	FReply OnAddDMXLibraryButtonClicked();

	/** Creates a details view for the DMX Library View Model */
	TSharedRef<SWidget> CreateDetailsViewForModel(UDMXPixelMappingDMXLibraryViewModel& Model) const;

	/** Selects the fixture group component */
	void SelectFixtureGroupComponent(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent);

	/** Returns the root component of the pixel mapping */
	UDMXPixelMappingRootComponent* GetPixelMappingRootComponent() const;

	/** Returns the fixture group components in the pixel mapping */
	TArray<UDMXPixelMappingFixtureGroupComponent*> GetFixtureGroupComponents() const;

	/** List of fixture patches the user can select from */
	TSharedPtr<SDMXReadOnlyFixturePatchList> FixturePatchList;

	/** Scrollbox that holds the details views */
	TSharedPtr<SScrollBox> DMXLibrariesScrollBox;

	/** Timer handle for the Request Refresh method */
	FTimerHandle RefreshTimerHandle;

	/** The view model of the focused DMX Library */
	TArray<UDMXPixelMappingDMXLibraryViewModel*> ViewModels;

	/** The toolkit of this editor */
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
};
