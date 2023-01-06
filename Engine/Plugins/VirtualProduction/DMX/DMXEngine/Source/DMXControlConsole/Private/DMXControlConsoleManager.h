// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class FDMXControlConsoleSelection;
class UDMXControlConsole;
class UDMXControlConsolePreset;

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

	/** Creates a new Control Console Preset asset at the given path */
	UDMXControlConsolePreset* CreateNewPreset(const FString& InAssetPath, const FString& InAssetName);

	/** Loads Control Console's data from the given Preset */
	void LoadFromPreset(const UDMXControlConsolePreset* Preset);

	/** Sends DMX on the Control Console */
	void SendDMX();

	/** Stops sending DMX data from the Control Console */
	void StopDMX();

	/** Gets wheter the Control Console is sending DMX data or not */
	bool IsSendingDMX() const;

	/** True if DMX data sending can be played */
	bool CanSendDMX() const { return !IsSendingDMX(); }

	/** True if DMX data sending can be stopped */
	bool CanStopDMX() const { return IsSendingDMX(); };

	/** Resets DMX Control Console */
	void ClearAll();

	/** Gets a reference to OnControlConsoleLoaded delegate */
	FSimpleMulticastDelegate& GetOnControlConsoleLoaded() { return OnControlConsoleLoaded; }

private:
	/** Private constructor. Use FDMXControlConsoleManager::Get() instead. */
	FDMXControlConsoleManager();

	/** Destroys the manager instance */
	void Destroy();

	/** Called when the Control Console is restored by a preset */
	FSimpleMulticastDelegate OnControlConsoleLoaded;

	/** Reference to the DMX Control Console */
	TObjectPtr<UDMXControlConsole> ControlConsole;

	/** Selection for the DMX Control Console */
	TSharedPtr<FDMXControlConsoleSelection> SelectionHandler;

	/** The DMX Control Console manager instance */
	static TSharedPtr<FDMXControlConsoleManager> Instance;
};
