// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"

#include "DMXControlConsoleData.generated.h"

class UDMXControlConsoleFaderGroup;
class UDMXControlConsoleFaderGroupRow;
class UDMXEntityFixturePatch;
class UDMXLibrary;


DECLARE_MULTICAST_DELEGATE_OneParam(FDMXControlConsoleFaderGroupDelegate, const UDMXControlConsoleFaderGroup*);

/** The DMX Control Console */
UCLASS()
class DMXCONTROLCONSOLE_API UDMXControlConsoleData 
	: public UObject
	, public FTickableGameObject
{
	GENERATED_BODY()

	// Allow the DMXControlConsoleFaderGroupRow to read Control Console Data
	friend UDMXControlConsoleFaderGroupRow;

public:
	/** Adds a Fader Group Row to this DMX Control Console */
	UDMXControlConsoleFaderGroupRow* AddFaderGroupRow(const int32 RowIndex);

	/** Removes a Fader Group Row from this DMX Control Console */
	void DeleteFaderGroupRow(const TObjectPtr<UDMXControlConsoleFaderGroupRow>& FaderGroupRow);

	/** Gets this DMX Control Console's Fader Group Rows array */
	const TArray<UDMXControlConsoleFaderGroupRow*>& GetFaderGroupRows() const { return FaderGroupRows; }

	/** Gets an array of all Fader Groups in this DMX Control Console */
	TArray<UDMXControlConsoleFaderGroup*> GetAllFaderGroups() const;

#if WITH_EDITOR
	/** Gets an array of all active Fader Groups in this DMX Control Console */
	TArray<UDMXControlConsoleFaderGroup*> GetAllActiveFaderGroups() const;
#endif // WITH_EDITOR 

	/** Generates sorted Fader Groups based on the DMX Control Console's current DMX Library */
	void GenerateFromDMXLibrary();

	/** Finds the Fader Group matching the given Fixture Patch, if valid */
	UDMXControlConsoleFaderGroup* FindFaderGroupByFixturePatch(const UDMXEntityFixturePatch* InFixturePatch) const;

	/** Gets this DMX Control Console's DMXLibrary */
	UDMXLibrary* GetDMXLibrary() const { return CachedWeakDMXLibrary.Get(); }

	/** Sends DMX on this DMX Control Console on tick */
	void StartSendingDMX();

	/** Stops DMX on this DMX Control Console on tick */
	void StopSendingDMX();

	/** Gets if DMX is sending DMX data or not */
	bool IsSendingDMX() const { return bSendDMX; }

#if WITH_EDITOR
	/** Sets if the console can send DMX in Editor */
	void SetSendDMXInEditorEnabled(bool bSendDMXInEditorEnabled) { bSendDMXInEditor = bSendDMXInEditorEnabled; }
#endif // WITH_EDITOR 

	/** Updates DMX Output Ports */
	void UpdateOutputPorts(const TArray<FDMXOutputPortSharedRef> InOutputPorts);

	/** Clears all FaderGroupRows from this ControlConsole */
	void Clear();

	/** Resets the DMX Control Console to its default */
	void Reset();

	/** Gets a reference to OnFaderGroupAdded delegate */
	FDMXControlConsoleFaderGroupDelegate& GetOnFaderGroupAdded() { return OnFaderGroupAdded; }

	/** Gets a reference to OnFaderGroupRemoved delegate */
	FDMXControlConsoleFaderGroupDelegate& GetOnFaderGroupRemoved() { return OnFaderGroupRemoved; }

#if WITH_EDITORONLY_DATA
	/** The current editor filter string */
	UPROPERTY()
	FString FilterString;
#endif // WITH_EDITORONLY_DATA

	// Property Name getters
	FORCEINLINE static FName GetDMXLibraryPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleData, SoftDMXLibraryPtr); }
	FORCEINLINE static FName GetFaderGroupRowsPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleData, FaderGroupRows); }

protected:
	//~ Begin UObject interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject interface

	// ~ Begin FTickableGameObject interface
	virtual void Tick(float InDeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// ~ End FTickableGameObject interface

private:
	/** Called when a Fader Group is added to the Control Console */
	FDMXControlConsoleFaderGroupDelegate OnFaderGroupAdded;

	/** Called when a Fader Group is removed from the Control Console */
	FDMXControlConsoleFaderGroupDelegate OnFaderGroupRemoved;

	/** Library used to generate Fader Groups */
	UPROPERTY(EditAnywhere, meta = (DisplayName = "DMX Library", ShowDisplayNames), Category = "DMX Control Console")
	TSoftObjectPtr<UDMXLibrary> SoftDMXLibraryPtr;

	/** Cached DMX Library for faster access */
	UPROPERTY()
	TWeakObjectPtr<UDMXLibrary> CachedWeakDMXLibrary;

	/** DMX Control Console's Fader Group Rows array */
	UPROPERTY(VisibleAnywhere, Category = "DMX Control Console")
	TArray<TObjectPtr<UDMXControlConsoleFaderGroupRow>> FaderGroupRows;

	/** Output ports to output dmx to */
	TArray<FDMXOutputPortSharedRef> OutputPorts;

	/** True when this object is ticking */
	bool bSendDMX = false;

#if WITH_EDITORONLY_DATA
	/** True if the Control Console ticks in Editor */
	bool bSendDMXInEditor = true;
#endif // WITH_EDITORONLY_DATA
};
