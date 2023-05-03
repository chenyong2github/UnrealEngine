// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntityReference.h"
#include "Widgets/SDMXReadOnlyFixturePatchList.h"

#include "EditorUndoClient.h"
#include "Delegates/Delegate.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "DMXPixelMappingDMXLibraryViewModel.generated.h"

class FDMXPixelMappingToolkit;
class UDMXEntityFixturePatch;
class UDMXLibrary;
class UDMXPixelMappingFixtureGroupComponent;
class UDMXPixelMappingRootComponent;


/** Model for the DMX Library View */
UCLASS(Config = DMXEditor)
class UDMXPixelMappingDMXLibraryViewModel
	: public UObject
	, public FEditorUndoClient
{
	GENERATED_BODY()

public:
	/** Creates a new model */
	static UDMXPixelMappingDMXLibraryViewModel* CreateNew(const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit, UDMXPixelMappingFixtureGroupComponent* InFixtureGroup);

	/** Returns the DMX Library */
	UDMXLibrary* GetDMXLibrary() const { return DMXLibrary;  }

	/** Sets the fixture group this model uses */ 
	UDMXPixelMappingFixtureGroupComponent* GetFixtureGroupComponent() const;

	/** Returns the root component of the pixel mapping */
	UDMXPixelMappingRootComponent* GetPixelMappingRootComponent() const;

	/** Returns true if the fixture group or one of its children is selected */
	bool IsFixtureGroupOrChildSelected() const;

	/** Returns the fixture patches in use in the pixel mapping */
	const TArray<FDMXEntityFixturePatchRef> GetFixturePatchesInUse() const;

	/** Returns the default fixture patch list descriptor */
	const FDMXReadOnlyFixturePatchListDescriptor& GetFixturePatchListDescriptor() const { return FixturePatchListDescriptor; }

	/** Saves the fixture patch list descriptor */
	void SaveFixturePatchListDescriptor(const FDMXReadOnlyFixturePatchListDescriptor& NewDescriptor);

	/** Delegate broadcast when the DMX library changed */
	FSimpleMulticastDelegate OnDMXLibraryChanged;

protected:
	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

	// Begin UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End UObject interface

private:
	/** Updates the DMX Library from the component */
	void UpdateDMXLibraryFromComponent();

	/** Removes invalid patches from pixel mapping */
	void RemoveInvalidPatches();

	/** Returns the fixture group components in the pixel mapping */
	TArray<UDMXPixelMappingFixtureGroupComponent*> GetFixtureGroupComponentsOfSameLibrary() const;

	/** The DMX library of this view */
	UPROPERTY(EditAnywhere, Transient, Category = "DMXLibrary", Meta = (AllowPrivateAccess = true))
	TObjectPtr<UDMXLibrary> DMXLibrary;

	/** The fixture group component that this model uses */
	UPROPERTY(Transient)
	TWeakObjectPtr<UDMXPixelMappingFixtureGroupComponent> WeakFixtureGroupComponent;

	/** Default fixture patch list descriptor */
	UPROPERTY(Config)
	FDMXReadOnlyFixturePatchListDescriptor FixturePatchListDescriptor;

	/** True while changing properties */
	bool bChangingProperties = false;

	/** The toolkit of the editor that uses this model */
	TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
};
