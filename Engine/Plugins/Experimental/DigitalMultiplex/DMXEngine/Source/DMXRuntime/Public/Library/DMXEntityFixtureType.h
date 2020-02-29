// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"
#include "DMXProtocolTypes.h"

#include "DMXEntityFixtureType.generated.h"

class UDMXImport;

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureSubFunction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX")
	FString FunctionName;

	/** Minimum value in the range of values that represent this sub function */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX")
	uint8 MinValue;

	/** Maximum value in the range of values that represent this sub function */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX")
	uint8 MaxValue;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureFunction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "10"), Category = "DMX")
	FString FunctionName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "20"), Category = "DMX")
	FString Description;

	/** Ranges of values that each represent one sub function */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "40"), Category = "DMX")
	TArray<FDMXFixtureSubFunction> SubFunctions;

	/** Initial value for this function when no value is set */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "30"), Category = "DMX")
	int64 DefaultValue;

	/** This function's starting channel */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (DisplayName = "Channel Assignment", ClampMin = "1", ClampMax = "512", DisplayPriority = "1"), Category = "DMX")
	int32 Channel;

	/**
	 * This function's channel offset.
	 * E.g.: if the function's starting channel is supposed to be 10
	 * and ChannelOffset = 5, the function's starting channel becomes 15
	 * and all following functions follow it accordingly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Channel Offset", ClampMin = "0", ClampMax = "511", DisplayPriority = "1"), Category = "DMX")
	int32 ChannelOffset;

	/** This function's data type. Defines the used number of channels (bytes) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "5"), Category = "DMX")
	EDMXFixtureSignalFormat DataType;

	/**
	 * Least Significant Byte mode makes the individual bytes (channels) of the function be
	 * interpreted with the first bytes being the lowest part of the number.
	 * 
	 * E.g., given a 16 bit function with two channel values set to [0, 1],
	 * they would be interpreted as the binary number 00000001 00000000, which means 256.
	 * The first byte (0) became the lowest part in binary form and the following byte (1), the highest.
	 * 
	 * Most Fixtures use MSB (Most Significant Byte) mode, which interprets bytes as highest first.
	 * In MSB mode, the example above would be interpreted in binary as 00000000 00000001, which means 1.
	 * The first byte (0) became the highest part in binary form and the following byte (1), the lowest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Use LSB Mode", DisplayPriority = "29"), Category = "DMX")
	bool bUseLSBMode;

	FDMXFixtureFunction()
		: FunctionName()
		, Description()
		, SubFunctions()
		, DefaultValue(0)
		, Channel(1)
		, ChannelOffset(0)
		, DataType(EDMXFixtureSignalFormat::E8Bit)
	{}
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureMode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "1"), Category = "DMX")
	FString ModeName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "15"), Category = "DMX")
	TArray<FDMXFixtureFunction> Functions;

	/** Number of channels (bytes) used by this mode's functions */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1", ClampMax = "512", DisplayPriority = "10", EditCondition = "!bAutoChannelSpan"), Category = "DMX")
	int32 ChannelSpan;

	/**
	 * When enabled, ChannelSpan is automatically set based on the created functions and their data types.
	 * If disabled, ChannelSpan can be manually set and functions and functions' channels beyond the
	 * specified span will be ignored.
	 */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "5"), Category = "DMX")
	bool bAutoChannelSpan;

	FDMXFixtureMode()
		: ChannelSpan(1)
		, bAutoChannelSpan(true)
	{}
};

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DMX Fixture Type"))
class DMXRUNTIME_API UDMXEntityFixtureType
	: public UDMXEntity
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings")
	UDMXImport* DMXImport;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (DisplayName = "DMX Category"))
	FDMXFixtureCategory DMXCategory;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings")
	TArray<FDMXFixtureMode> Modes;

public:
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Fixture Settings")
	void SetModesFromDMXImport(UDMXImport* DMXImportAsset);

	static void SetFunctionSize(FDMXFixtureFunction& InFunction, uint8 Size);
#endif // WITH_EDITOR

	/** Gets the last channel occupied by the Function */
	static uint8 GetFunctionLastChannel(const FDMXFixtureFunction& Function);

	/**
	 * Return true if a Function's occupied channels are within the Mode's Channel Span.
	 * Optionally add an offset to the function address
	 */
	static bool IsFunctionInModeRange(const FDMXFixtureFunction& InFunction, const FDMXFixtureMode& InMode, int32 ChannelOffset = 0);

	static void ClampDefaultValue(FDMXFixtureFunction& InFunction);

	static uint8 NumChannelsToOccupy(EDMXFixtureSignalFormat DataType);

	static uint32 ClampValueToDataType(EDMXFixtureSignalFormat DataType, uint32 InValue);

	static uint32 GetDataTypeMaxValue(EDMXFixtureSignalFormat DataType);

	//~ Conversions to/from Bytes, Int and Normalized Float values.
	static void FunctionValueToBytes(const FDMXFixtureFunction& InFunction, uint32 InValue, uint8* OutBytes);
	static void IntToBytes(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, uint32 InValue, uint8* OutBytes);

	static uint32 BytesToFunctionValue(const FDMXFixtureFunction& InFunction, const uint8* InBytes);
	static uint32 BytesToInt(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, const uint8* InBytes);

	static void FunctionNormalizedValueToBytes(const FDMXFixtureFunction& InFunction, float InValue, uint8* OutBytes);
	static void NormalizedValueToBytes(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, float InValue, uint8* OutBytes);

	static float BytesToFunctionNormalizedValue(const FDMXFixtureFunction& InFunction, const uint8* InBytes);
	static float BytesToNormalizedValue(EDMXFixtureSignalFormat InSignalFormat, bool bUseLSB, const uint8* InBytes);

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	static void UpdateModeChannelProperties(FDMXFixtureMode& Mode);
#endif // WITH_EDITOR
};
