// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolMacros.h"

#include "Dom/JsonObject.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HAL/CriticalSection.h"
#include "Templates/Atomic.h"

#include "DMXProtocolTypes.generated.h"

UENUM(BlueprintType)
enum class EDMXSendResult : uint8
{
	Success UMETA(DisplayName = "Successfully sent"),
	ErrorGetUniverse UMETA(DisplayName = "Error Get Universe"),
	ErrorSetBuffer UMETA(DisplayName = "Error Set Buffer"),
	ErrorSizeBuffer UMETA(DisplayName = "Error Size Buffer"),
	ErrorEnqueuePackage UMETA(DisplayName = "Error Enqueue Package"),
	ErrorNoSenderInterface UMETA(DisplayName = "Error No Sending Interface")
};

UENUM()
enum class EDMXProtocolDirectionality : uint8
{
	EInput 	UMETA(DisplayName = "Input"),
	EOutput UMETA(DisplayName = "Output"),
};

UENUM()
enum class EDMXCommunicationTypes : uint8
{
	Broadcast UMETA(DisplayName = "Broadcast")
};

UENUM(BlueprintType)
enum class EDMXFixtureSignalFormat : uint8
{
	/** Uses 1 channel (byte) and allows subdivision into sub functions */
	E8BitSubFunctions UMETA(DisplayName = "8 Bit (Sub Functions)", Hidden),
	/** Uses 1 channel (byte). Range: 0 to 255 */
	E8Bit 	UMETA(DisplayName = "8 Bit"),
	/** Uses 2 channels (bytes). Range: 0 to 65.535 */
	E16Bit 	UMETA(DisplayName = "16 Bit"),
	/** Uses 3 channels (bytes). Range: 0 to 16.777.215 */
	E24Bit 	UMETA(DisplayName = "24 Bit"),
	/** Uses 4 channels (bytes). Range: 0 to 4.294.967.295 */
	E32Bit 	UMETA(DisplayName = "32 Bit"),
};

USTRUCT(BlueprintType)
struct DMXPROTOCOL_API FDMXProtocolName
{
public:
	GENERATED_BODY()

	/** Selected protocol name */
	UPROPERTY(BlueprintReadWrite, Category = "DMX|Protocol")
	FName Name;

	static TArray<FName> GetPossibleValues();

	FDMXProtocolName();
	/** Construct from a protocol name */
	explicit FDMXProtocolName(const FName& InName);
	/** Construct from a protocol */
	FDMXProtocolName(IDMXProtocolPtr InProtocol);

	/** Returns the Protocol this name represents */
	IDMXProtocolPtr GetProtocol() const;
	/** True if it has a valid Protocol name */
	bool IsValid() const;

	//~ FName operators
	operator FName&() { return Name; }
	operator const FName&() const { return Name; }

	//~ DMXProtocol operators
	operator IDMXProtocolPtr() { return GetProtocol(); }
	operator const IDMXProtocolPtr() const { return GetProtocol(); }

	/** Bool (is valid) operator */
	operator bool() const { return IsValid(); }

	//~ Comparison operators
	bool operator==(const FDMXProtocolName& Other) const { return Name == Other.Name; }
	bool operator==(const IDMXProtocolPtr& Other) const { return GetProtocol().Get() == Other.Get(); }
	bool operator==(const FName& Other) const { return Name == Other; }
};

USTRUCT(BlueprintType)
struct DMXPROTOCOL_API FDMXFixtureCategory
{
public:
	GENERATED_BODY()

	/** Selected protocol name */
	UPROPERTY(BlueprintReadWrite, Category = "DMX|Category")
	FName Name;

	static FSimpleMulticastDelegate OnPossibleValuesUpdated;
	static TArray<FName> GetPossibleValues();

	static FName GetFirstValue();

	FDMXFixtureCategory();

	explicit FDMXFixtureCategory(const FName& InName);

	//~ FName operators
	operator FName&() { return Name; }
	operator const FName&() const { return Name; }

	/** Bool (is valid) operator */
	operator bool() const { return !Name.IsNone(); }

	//~ Comparison operators
	bool operator==(const FDMXFixtureCategory& Other) const { return Name == Other.Name; }
	bool operator==(const FName& Other) const { return Name == Other; }
};

UCLASS()
class UDMXNameContainersConversions
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (DMX Protocol Name)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FString Conv_DMXProtocolNameToString(const FDMXProtocolName& InProtocolName);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToName (DMX Protocol Name)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FName Conv_DMXProtocolNameToName(const FDMXProtocolName& InProtocolName);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToString (DMX Fixture Category)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FString Conv_DMXFixtureCategoryToString(const FDMXFixtureCategory& InFixtureCategory);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToName (DMX Fixture Category)", CompactNodeTitle = "->", BlueprintAutocast), Category = "Utilities|DMX")
	static FName Conv_DMXFixtureCategoryToName(const FDMXFixtureCategory& InFixtureCategory);
};


USTRUCT()
struct DMXPROTOCOL_API FDMXUniverse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "DMX")
	uint32 UniverseNumber;

	UPROPERTY(EditAnywhere, meta = (DisplayName = "Address", ClampMin = 1, ClampMax = 512, UIMin = 1, UIMax = 512), Category = "DMX")
	uint32 Channel;

	UPROPERTY()
	EDMXProtocolDirectionality DMXProtocolDirectionality;

	FDMXUniverse()
		: UniverseNumber(0)
		, Channel(1)
		, DMXProtocolDirectionality(EDMXProtocolDirectionality::EInput)
	{}
};

struct DMXPROTOCOL_API FDMXBuffer
{
public:
	FDMXBuffer()
		: SequenceID(0)
	{
		DMXData.AddZeroed(DMX_UNIVERSE_SIZE);
	}

	/** @return Gets actual SequanceID  */
	uint32 GetSequenceID() const { return SequenceID; }

	/** @return DMX buffer address value */
	uint8 GetDMXDataAddress(uint32 InAddress) const;

	/**
	 * Calls InFunction with a reference to the DMX data buffer passed in on a thread-safe manner.
	 * This method locks execution and calls InFunction as soon as the buffer can be accessed,
	 * so it's safe to reference locally scoped variables inside InFunction.
	 */
	void AccessDMXData(TFunctionRef<void(TArray<uint8>&)> InFunction);

	/**
	 * Updates the fragment in the DMX buffer
	 * This is set Map values, by specifying channels into the 512 DMX buffer
	 * 
	 * @param IDMXFragmentMap DMX fragment map
	 * @return True if it was successfully set
	 */
	bool SetDMXFragment(const IDMXFragmentMap& InDMXFragment);

	/**
	 * Sets the DMX buffer from input buffer
	 * 
	 * @param InBuffer Pointer to DMX buffer array
	 * @param InSize Size of the buffer to copy 
	 * @return True if it was successfully set
	 */
	bool SetDMXBuffer(const uint8* InBuffer, uint32 InSize);

private:
	/** DMX bytes buffer array */
	TArray<uint8> DMXData;

	/** Synchronizations sequence id */
	TAtomic<uint32> SequenceID;

	/** Mutex to make sure no two threads write to the buffer concurrently */
	mutable FCriticalSection BufferCritSec;
};

struct DMXPROTOCOL_API FDMXPacket
{
	FDMXPacket(FJsonObject& InSettings, const TArray<uint8>& InData)
	{
		Settings = InSettings;
		Data = InData;
	}

	FDMXPacket(const TArray<uint8>& InData)
	{
		Data = InData;
	}

	FJsonObject Settings;
	TArray<uint8> Data;
};

struct DMXPROTOCOL_API FRDMUID
{
	FRDMUID()
	{
	}

	FRDMUID(uint8 InBuffer[RDM_UID_WIDTH])
	{
		FMemory::Memcpy(Buffer, InBuffer, RDM_UID_WIDTH);
	}

	FRDMUID(const TArray<uint8>& InBuffer)
	{
		// Only copy if we have the requested amount of data
		if (InBuffer.Num() == RDM_UID_WIDTH)
		{
			FMemory::Memmove(Buffer, InBuffer.GetData(), RDM_UID_WIDTH);
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Verbose, TEXT("Size of the TArray buffer is wrong"));
		}
	}

	uint8 Buffer[RDM_UID_WIDTH] = { 0 };
};


