// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"

#include "DMXControlConsole.generated.h"

class FDMXOutputPort;
class UDMXControlConsoleFaderGroup;
class UDMXControlConsoleFaderGroupRow;
class UDMXLibrary;


/** The DMX Control Console */
UCLASS()
class UDMXControlConsole 
	: public UObject
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Adds a Fader Group Row to this DMX Control Console */
	UDMXControlConsoleFaderGroupRow* AddFaderGroupRow(const int32 RowIndex);

	/** Removes a Fader Group Row from this DMX Control Console */
	void DeleteFaderGroupRow(const TObjectPtr<UDMXControlConsoleFaderGroupRow>& FaderGroupRow);

	/** Gets this DMX Control Console's Fader Group Rows array */
	const TArray<UDMXControlConsoleFaderGroupRow*>& GetFaderGroupRows() const { return FaderGroupRows; }

	/** Generates sorted Fader Groups based on the DMX Control Console's current DMX Library */
	void GenarateFromDMXLibrary();

	/** Gets this DMX Control Console's DMXLibrary */
	UDMXLibrary* GetDMXLibrary() const { return DMXLibrary.Get(); }

	/** Plays DMX on this DMX Control Console */
	void PlayDMX();

	/** Stops DMX on this DMX Control Console */
	void StopDMX();

	/** Gets if DMX is playing or not */
	bool IsPlayingDMX() const { return bIsPlayingDMX; }

	/** Updates DMX Output Ports */
	void UpdateOutputPorts(const TArray<FDMXOutputPortSharedRef> InOutputPorts);

	/** Resets the DMX Control Console to its default */
	void Reset();

	/** Sets bForceRefresh condition */
	void SetForceRefresh(bool bRefresh);

	/** True if this Control Console needs to be refreshed */
	bool HasForceRefresh() const { return bForceRefresh; }

	// Property Name getters
	FORCEINLINE static FName GetDMXLibraryPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsole, DMXLibrary); }
	FORCEINLINE static FName GetFaderGroupRowsPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsole, FaderGroupRows); }

protected:
	//~ Begin of UObject interface
	virtual void PostInitProperties() override;
	//~ End of UObject interface

	// ~ Begin FTickableGameObject interface
	virtual void Tick(float InDeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// ~ End FTickableGameObject interface

private:
	/** Gets an array of alla Fader Groups in this DMX Control Console */
	TArray<UDMXControlConsoleFaderGroup*> GetAllFaderGroups() const;

	/** Clears FaderGroupRows array */
	void ClearFaderGroupRows();

	/** Library used to generate Fader Groups */
	UPROPERTY(EditAnywhere, Category = "DMX Control Console")
	TWeakObjectPtr<UDMXLibrary> DMXLibrary;

	/** DMX Control Console's Fader Group Rows array */
	UPROPERTY(VisibleAnywhere, Category = "DMX Control Console")
	TArray<TObjectPtr<UDMXControlConsoleFaderGroupRow>> FaderGroupRows;

	/** Output ports to output dmx to */
	TArray<FDMXOutputPortSharedRef> OutputPorts;

	/** Shows if DMX is playing on this DMX Control Console or not */
	bool bIsPlayingDMX = false;

	/** Shows wheter this ControlConsole needs to be refreshed or not */
	bool bForceRefresh = false;
};
