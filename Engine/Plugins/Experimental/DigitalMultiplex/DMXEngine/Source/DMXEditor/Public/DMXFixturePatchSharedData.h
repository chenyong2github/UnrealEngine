// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FDMXEditor;
class FDMXFixturePatchNode;
class SDMXEntityList;
class UDMXEntityFixturePatch;
class UDMXLibrary;

/** Shared data for fixture patch editors */
class FDMXFixturePatchSharedData
	: public TSharedFromThis<FDMXFixturePatchSharedData>
{
public:
	FDMXFixturePatchSharedData(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	/** Broadcast when a universe is selected by an editor */
	FSimpleMulticastDelegate OnUniverseSelectionChanged;

	/** Broadcast when a patch node is selected by an editor */
	FSimpleMulticastDelegate OnFixturePatchSelectionChanged;

	/** Selects the universe */
	void SelectUniverse(int32 UniverseID);

	/** Returns the selected universe */
	int32 GetSelectedUniverse();

	/** Selects the patch node */
	void SelectFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> Patch);

	/** Selects the patch node, does not clear selection */
	void AddFixturePatchToSelection(TWeakObjectPtr<UDMXEntityFixturePatch> Patch);

	/** Selects the patch nodes */
	void SelectFixturePatches(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& Patches);

	/** Returns the selected patch node or nullptr if nothing is selected */
	const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& GetSelectedFixturePatches() const;

private:
	/** The universe currently edited by editors */
	int32 SelectedUniverse = 1;

	/** Patch node currently selected or nullptr if nothing is selected */
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
