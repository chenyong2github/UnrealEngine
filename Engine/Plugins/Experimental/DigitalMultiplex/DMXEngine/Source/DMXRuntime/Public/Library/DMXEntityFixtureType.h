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
	UFUNCTION(BlueprintCallable, Category = "Fixture Settings")
	void SetModesFromDMXImport(UDMXImport* DMXImportAsset);

	static void SetFunctionSize(FDMXFixtureMode& InMode, FDMXFixtureFunction& InFunction, uint8 Size);

	/** Gets the last channel occupied by the Function */
	static uint8 GetFunctionLastChannel(const FDMXFixtureFunction& Function);

	static void ClampDefaultValue(FDMXFixtureFunction& InFunction);

	static uint8 NumChannelsToOccupy(EDMXFixtureSignalFormat DataType);

	static uint32 ClampValueToDataType(EDMXFixtureSignalFormat DataType, uint32 InValue);

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	static void UpdateModeChannelProperties(FDMXFixtureMode& Mode);
#endif // WITH_EDITOR
};
