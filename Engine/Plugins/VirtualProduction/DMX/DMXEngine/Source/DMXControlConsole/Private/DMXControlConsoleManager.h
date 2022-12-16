// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class UDMXControlConsole;
class FDMXControlConsoleSelection;

class FUICommandList;


/** Manages lifetime and provides access to the DMX Control Console */
class FDMXControlConsoleManager final
	: public FGCObject
	, public TSharedFromThis<FDMXControlConsoleManager>
{
public:
	/** Destructor */
	~FDMXControlConsoleManager();

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FDMXControlConsoleManager");
	}
	//~ End FGCObject interface

	/** Gets a reference to DMX Editor's DMX Control Console */
	static FDMXControlConsoleManager& Get();

	/** Gets a reference to the DMX Control Console */
	UDMXControlConsole* GetDMXControlConsole() const { return ControlConsole; }

	/** Gets a reference to the Selection Handler*/
	TSharedRef<FDMXControlConsoleSelection> GetSelectionHandler();

	/** Setups commands for this DMX Control Console */
	void SetupCommands();

	/** Returns DMX Control Console's command list */
	const TSharedPtr<FUICommandList>& GetControlConsoleCommands() const { return ControlConsoleCommands; }

private:
	/** Private constructor. Use FDMXControlConsoleManager::Get() instead. */
	FDMXControlConsoleManager();

	/** Destroys the manager instance */
	void Destroy();

	/** Resets DMX Control Console */
	void ClearAll();

	/** Reference to the DMX Control Console */
	TObjectPtr<UDMXControlConsole> ControlConsole;

	/** List of UI commands for this toolkit.  This should be filled in by the derived class! */
	TSharedPtr<FUICommandList> ControlConsoleCommands;

	/** Selection for the DMX Control Console */
	TSharedPtr<FDMXControlConsoleSelection> SelectionHandler;

	/** The DMX Control Console manager instance */
	static TSharedPtr<FDMXControlConsoleManager> Instance;
};
