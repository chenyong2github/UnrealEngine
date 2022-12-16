// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXAttribute.h"
#include "DMXControlConsoleFaderBase.h"

#include "DMXControlConsoleFixturePatchFunctionFader.generated.h"

struct FDMXAttributeName;
struct FDMXFixtureFunction;
enum class EDMXFixtureSignalFormat : uint8;


/** A fader matching a Fixture Patch Function in the DMX Control Console. */
UCLASS()
class UDMXControlConsoleFixturePatchFunctionFader 
	: public UDMXControlConsoleFaderBase
{
	GENERATED_BODY()

public:
	/** Constructor */
	UDMXControlConsoleFixturePatchFunctionFader();

	/** Sets Fader's properties values using the given Fixture Function */
	void SetPropertiesFromFixtureFunction(const FDMXFixtureFunction& FixtureFunction, const int32 InUniverseID, const int32 StartingChannel);

	/** Returns the universe ID to which to should send DMX to */
	int32 GetUniverseID() const { return UniverseID; }

	/** Returns the universe ID to which to should send DMX to */
	EDMXFixtureSignalFormat GetDataType() const { return DataType; }

	/** Returns the Starting Channel of where to send DMX to */
	int32 GetStartingAddress() const { return StartingAddress; }

	/** Gets wheter this Fader uses LSB mode or not */
	bool GetUseLSBMode() const { return bUseLSBMode; }

	/** Returns the name of the attribute mapped to this fader */
	const FDMXAttributeName& GetAttributeName() const { return Attribute; }

	// Property Name getters
	FORCEINLINE static FName GetUniverseIDPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFixturePatchFunctionFader, UniverseID); }
	FORCEINLINE static FName GetStartingAddressPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFixturePatchFunctionFader, StartingAddress); }
	FORCEINLINE static FName GetAttributePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFixturePatchFunctionFader, Attribute); }

private:
	/** Sets a new universe ID */
	virtual void SetUniverseID(int32 InUniversID);

	/** Sets this fader's number of channels */
	virtual void SetDataType(EDMXFixtureSignalFormat InDataType);

	/** Sets min/max range, according to the number of channels */
	virtual void SetValueRange();

	/** Sets starting/ending address range, according to the number of channels  */
	virtual void SetAddressRange(int32 InStartingAddress);

	/** The universe the should send to fader */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "3"), Category = "DMX Fader")
	int32 UniverseID = 1;

	/** The starting channel Address to send DMX to */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "4"), Category = "DMX Fader")
	int32 StartingAddress = 1;

	/** Use Least Significant Byte mode. Individual bytes(channels) be interpreted with the first bytes being the lowest part of the number(endianness). */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "9"), Category = "DMX Fader")
	bool bUseLSBMode = false;

	UPROPERTY(VisibleAnywhere, meta = (DisplayName = "Attribute Mapping", DisplayPriority = "2"), Category = "DMX Fader")
	FDMXAttributeName Attribute;

	/** The number of channels this Fader uses */
	EDMXFixtureSignalFormat DataType;
};
