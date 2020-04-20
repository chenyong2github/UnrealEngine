// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"
#include "DMXEntityController.generated.h"

UCLASS(meta = (DisplayName = "DMX Controller"))
class DMXRUNTIME_API UDMXEntityController
	: public UDMXEntityUniverseManaged
{
	GENERATED_BODY()

public:
	/**  Defines where DMX data is sent to. */
	UPROPERTY(EditAnywhere, Category = "DMX") // Hidden. For now it has just a single value. More values will be added in the future.
	EDMXCommunicationTypes CommunicationMode;
	
	UPROPERTY(EditAnywhere, Category = "DMX")
	FString UnicastIP;

	/**  First Universe ID on this Controller's range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Universe Properties", meta = (DisplayName = "Universe Start", DisplayPriority = 1, ClampMin = 1))
	int32 UniverseLocalStart;

	/**  Number of Universe IDs on this Controller's range, starting from Universe Start value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Universe Properties", meta = (DisplayName = "Amount of Universes", DisplayPriority = 2, ClampMin = 1))
	int32 UniverseLocalNum;

	/**  Last Universe ID on this Controller's range, calculated from Universe Start and Amount of Universes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Universe Properties", meta = (DisplayName = "Universe End", DisplayPriority = 3))
	int32 UniverseLocalEnd;

	/**
	 * Offsets the Universe IDs range on this Controller before communication with other devices.
	 * Useful to solve conflicts with Universe IDs from other devices on the same network.
	 *
	 * All other DMX Library settings use the normal Universe IDs range.
	 * This allows the user to change all Universe IDs used by the Fixture Patches and
	 * avoid conflicts with other devices by updating only the Controller's Remote Offset.
	 */
	UPROPERTY()
	int32 RemoteOffset;

	/**
	 * First Universe ID on this Controller's range that is sent over the network.
	 * Universe Start + Remote Offset
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Universe Properties", meta = (DisplayName = "Remote Universe Range Start", DisplayPriority = 21))
	int32 UniverseRemoteStart;

	/**
	 * Last Universe ID in this Controller's range that is sent over the network.
	 * Universe End + Remote Offset
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Universe Properties", meta = (DisplayName = "Remote Universe Range End", DisplayPriority = 22))
	int32 UniverseRemoteEnd;

public:
	UDMXEntityController()
		: UnicastIP(TEXT("0.0.0.0"))
		, UniverseLocalStart(1)
		, UniverseLocalNum(1)
	{}

	/** Returns the currently assigned protocol for this controller */
	UFUNCTION(BlueprintPure, Category = "DMX")
	FName GetProtocol() const { return DeviceProtocol.Name; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
#endif // WITH_EDITOR
	virtual void PostInitProperties() override;

protected:
	/** Keep range valid */
	void ValidateRangeValues();
	void UpdateUniversesFromRange();
};
