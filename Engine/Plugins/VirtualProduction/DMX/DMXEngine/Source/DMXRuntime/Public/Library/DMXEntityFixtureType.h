// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXAttribute.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXEntity.h"
#include "Modulators/DMXModulator.h"

#include "DMXEntityFixtureType.generated.h"

class UDMXImport;

UENUM(BlueprintType)
enum class EDMXPixelMappingDistribution : uint8
{
	TopLeftToRight,
	TopLeftToBottom,
	TopLeftToClockwise,
	TopLeftToAntiClockwise,

	TopRightToLeft,
	BottomLeftToTop,
	TopRightToAntiClockwise,
	BottomLeftToClockwise,

	BottomLeftToRight,
	TopRightToBottom,
	BottomLeftAntiClockwise,
	TopRightToClockwise,

	BottomRightToLeft,
	BottomRightToTop,
	BottomRightToClockwise,
	BottomRightToAntiClockwise
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureFunction
{
	GENERATED_BODY()

	/**
	 * The Attribute name to map this Function to.
	 * This is used to easily find the Function in Blueprints, using an Attribute
	 * list instead of typing the Function name directly.
	 * The list of Attributes can be edited on
	 * Project Settings->Plugins->DMX Protocol->Fixture Settings->Fixture Function Attributes
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Attribute Mapping", DisplayPriority = "11"), Category = "DMX")
	FDMXAttributeName Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "10"), Category = "DMX")
	FString FunctionName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "20"), Category = "DMX")
	FString Description;

	/** Initial value for this function when no value is set */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "30"), Category = "DMX")
	int64 DefaultValue;

	/** This function's starting channel */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (DisplayName = "Channel Assignment", ClampMin = "1", ClampMax = "512", DisplayPriority = "2"), Category = "DMX")
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
	bool bUseLSBMode = false;

	/** Returns the number of channels the function spans, according to its data type */
	FORCEINLINE uint8 GetNumChannels() const { return static_cast<uint8>(DataType) + 1;	}

	FDMXFixtureFunction()
		: Attribute(FDMXNameListItem::None)
		, FunctionName()
		, Description()
		, DefaultValue(0)
		, Channel(1)
		, ChannelOffset(0)
		, DataType(EDMXFixtureSignalFormat::E8Bit)
		, bUseLSBMode(false)
	{}
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureCellAttribute
{
	GENERATED_BODY()

	/**
	 * The Attribute name to map this Function to.
	 * This is used to easily find the Function in Blueprints, using an Attribute
	 * list instead of typing the Function name directly.
	 * The list of Attributes can be edited on
	 * Project Settings->Plugins->DMX Protocol->Fixture Settings->Fixture Function Attributes
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "Attribute Mapping", DisplayPriority = "11"), Category = "DMX")
	FDMXAttributeName Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "20", DisplayName = "Description"), Category = "DMX")
	FString Description;

	/** Initial value for this function when no value is set */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "30", DisplayName = "Default Value"), Category = "DMX")
	int64 DefaultValue;

	/** This function's data type. Defines the used number of channels (bytes) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "5", DisplayName = "Data Type"), Category = "DMX")
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

	FDMXFixtureCellAttribute()
		: Attribute(FDMXNameListItem::None)
		, Description()
		, DefaultValue(0)
		, DataType(EDMXFixtureSignalFormat::E8Bit)
		, bUseLSBMode(false)
	{}

	/** Returns the number of channels of the attribute */
	uint8 GetNumChannels() const { return static_cast<uint8>(DataType) + 1; }
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXFixtureMatrix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "60", DisplayName = "Cell Attributes"), Category = "DMX")
	TArray<FDMXFixtureCellAttribute> CellAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "20", DisplayName = "First Cell Channel", ClampMin = "1", ClampMax = "512"), Category = "DMX")
	int32 FirstCellChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "30", DisplayName = "X Cells", ClampMin = "0"), Category = "DMX")
	int32 XCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "40", DisplayName = "Y Cells", ClampMin = "0"), Category = "DMX")
	int32 YCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "50", DisplayName = "PixelMapping Distribution"), Category = "DMX")
	EDMXPixelMappingDistribution PixelMappingDistribution;

	FDMXFixtureMatrix()
		: FirstCellChannel(1)
		, XCells(1)
		, YCells(1)
		, PixelMappingDistribution(EDMXPixelMappingDistribution::TopLeftToRight)
	{}

	bool GetChannelsFromCell(FIntPoint CellCoordinate, FDMXAttributeName Attribute, TArray<int32>& Channels) const;
	int32 GetFixtureMatrixLastChannel() const;
};

USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXCell
{
	GENERATED_BODY()

	/** The cell index in a 1D Array (row order), starting from 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "20", DisplayName = "Cell ID", ClampMin = "0"), Category = "DMX")
	int32 CellID;

	/** The cell coordinate in a 2D Array, starting from (0, 0) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (DisplayPriority = "30", DisplayName = "Coordinate"), Category = "DMX")
	FIntPoint Coordinate;

	FDMXCell()
		: CellID(0)
		, Coordinate(FIntPoint (-1,-1))
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", ClampMax = "512", DisplayPriority = "10", EditCondition = "!bAutoChannelSpan"), Category = "DMX")
	int32 ChannelSpan;

	/**
	 * When enabled, ChannelSpan is automatically set based on the created functions and their data types.
	 * If disabled, ChannelSpan can be manually set and functions and functions' channels beyond the
	 * specified span will be ignored.
	 */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "5"), Category = "DMX")
	bool bAutoChannelSpan;

	UPROPERTY(EditAnywhere, Category = "DMX")
	FDMXFixtureMatrix FixtureMatrixConfig;

	FDMXFixtureMode()
		: ChannelSpan(0)
		, bAutoChannelSpan(true)
	{}

#if WITH_EDITOR
	/**
	 * Adding the function into the mode, checking the channel offset
	 */
	int32 AddOrInsertFunction(int32 IndexOfFunction, const FDMXFixtureFunction& InFunction);
#endif
};

#if WITH_EDITOR
	/** Notification when data type changed */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDataTypeChangeDelegate, const UDMXEntityFixtureType*, const FDMXFixtureMode&);
#endif


UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DMX Fixture Type"))
class DMXRUNTIME_API UDMXEntityFixtureType
	: public UDMXEntity
{
	GENERATED_BODY()

public:
	UDMXEntityFixtureType()
		: bFixtureMatrixEnabled(false)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings")
	UDMXImport* DMXImport;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (DisplayName = "DMX Category"))
	FDMXFixtureCategory DMXCategory;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (DisplayName = "Matrix Fixture"))
	bool bFixtureMatrixEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings")
	TArray<FDMXFixtureMode> Modes;

	/** 
	 * Modulators applied right before a patch of this type is received. 
	 * NOTE: Modulators only affect the patch's normalized values! Untouched values are still available when accesing raw values. 
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Fixture Settings")
	TArray<UDMXModulator*> InputModulators;

public:
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Fixture Settings")
	void SetModesFromDMXImport(UDMXImport* DMXImportAsset);

	static void SetFunctionSize(FDMXFixtureFunction& InFunction, uint8 Size);

	static FDataTypeChangeDelegate& GetDataTypeChangeDelegate() { return DataTypeChangeDelegate; }
#endif // WITH_EDITOR

	/** Gets the last channel occupied by the Function */
	static uint8 GetFunctionLastChannel(const FDMXFixtureFunction& Function);

	/**
	 * Return true if a Function's occupied channels are within the Mode's Channel Span.
	 * Optionally add an offset to the function address
	 */
	static bool IsFunctionInModeRange(const FDMXFixtureFunction& InFunction, const FDMXFixtureMode& InMode, int32 ChannelOffset = 0);

	static bool IsFixtureMatrixInModeRange(const FDMXFixtureMatrix& InFixtureMatrix, const FDMXFixtureMode& InMode, int32 ChannelOffset = 0);

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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Use UpdateChannelSpan instead.")
	void UpdateModeChannelProperties(FDMXFixtureMode& Mode);

	/** Updates the channel span of the Mode */
	void UpdateChannelSpan(FDMXFixtureMode& Mode);

	/** Updates the FixtureMatrixConfig's YCells property given num XCells for the specified Mode */
	void UpdateYCellsFromXCells(FDMXFixtureMode& Mode);

	/** Updates the FixtureMatrixConfig's XCells property given num YCells for the specified Mode */
	void UpdateXCellsFromYCells(FDMXFixtureMode& Mode);

#endif // WITH_EDITOR

private:
	/** Rebuilds the cache of fixture patches that use this fixture type */
	void RebuildFixturePatchCaches();

#if WITH_EDITOR
	/** Editor only data type change delagate */
	static FDataTypeChangeDelegate DataTypeChangeDelegate;
#endif
};
