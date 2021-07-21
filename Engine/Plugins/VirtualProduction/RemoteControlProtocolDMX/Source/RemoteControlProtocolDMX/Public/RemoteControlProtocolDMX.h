// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlProtocol.h"

#include "DMXProtocolCommon.h"
#include "RemoteControlProtocolBinding.h"
#include "IO/DMXInputPortReference.h"
#include "Library/DMXEntityFixtureType.h"

#include "RemoteControlProtocolDMX.generated.h"

class FRemoteControlProtocolDMX;

/**
 * Using as an inner struct for details customization.
 * Useful to have type customization for the struct
 */
USTRUCT()
struct FRemoteControlDMXProtocolEntityExtraSetting
{
	GENERATED_BODY();

	/** Starting universe channel */
	UPROPERTY(EditAnywhere, Category = Mapping, meta = (ClampMin = "1", ClampMax = "512", UIMin = "1", UIMax = "512"))
	int32 StartingChannel = 1;
};

/**
 * DMX protocol entity for remote control binding
 */
USTRUCT()
struct FRemoteControlDMXProtocolEntity : public FRemoteControlProtocolEntity
{
	GENERATED_BODY()

	friend class FRemoteControlProtocolDMX;

	/** Destructor */
	virtual ~FRemoteControlDMXProtocolEntity();

public:
	//~ Begin FRemoteControlProtocolEntity interface
	virtual FName GetRangePropertyName() const override { return NAME_UInt32Property; }
	virtual uint8 GetRangePropertySize() const override;
	virtual const FString& GetRangePropertyMaxValue() const override;
	//~ End FRemoteControlProtocolEntity interface

	/** Initialize struct and delegates */
	void Initialize();

	/** Try to get the port ID from dmx protocol settings */
	void UpdateInputPort();

public:
	/** DMX universe id */
	UPROPERTY(EditAnywhere, Category = Mapping)
	int32 Universe = 1;

	/**
	 * Least Significant Byte mode makes the individual bytes (channels) of the function be
	 * interpreted with the first bytes being the lowest part of the number.
	 * Most Fixtures use MSB (Most Significant Byte).
	 */
	UPROPERTY(EditAnywhere, Category = Mapping)
	bool bUseLSB = false;

	/** Defines the used number of channels (bytes) */
	UPROPERTY(EditAnywhere, Category = Mapping)
	EDMXFixtureSignalFormat DataType = EDMXFixtureSignalFormat::E8Bit;

	/** Extra protocol settings. Primary using for customization */
	UPROPERTY(EditAnywhere, Category = Mapping, meta=(ShowOnlyInnerProperties))
	FRemoteControlDMXProtocolEntityExtraSetting ExtraSetting;

	/** DMX range input property template, used for binding. */
	UPROPERTY(Transient)
	uint32 RangeInputTemplate = 0;

	/** Reference of an input DMX port id */
	FGuid InputPortId;

private:
	/** DMX entity cache buffer. From 1 up to 4 channels, based on DataType */
	TArray<uint8> CacheDMXBuffer;

	/** A single, generic DMX signal. One universe of raw DMX data received */
	FDMXSignalSharedPtr LastSignalPtr;

	/** Delegate for port changes */
	FDelegateHandle PortsChangedHandle;
};

/**
 * DMX protocol implementation for Remote Control
 */
class FRemoteControlProtocolDMX : public FRemoteControlProtocol
{
public:
	FRemoteControlProtocolDMX()
		: FRemoteControlProtocol(ProtocolName)
	{}
	
	//~ Begin IRemoteControlProtocol interface
	virtual void Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void UnbindAll() override;
	virtual UScriptStruct* GetProtocolScriptStruct() const override { return FRemoteControlDMXProtocolEntity::StaticStruct(); }
	virtual void OnEndFrame() override;
	//~ End IRemoteControlProtocol interface

private:
	/**
	 * Apply dmx channel data to the bound property, potentially resize the cache buffer
	 * @param InSignal				DMX signal buffer pointer
	 * @param InDMXOffset			Byte offset in signal buffer
	 * @param InProtocolEntityPtr	Protocol entity pointer
	 */
	void ProcessAndApplyProtocolValue(const FDMXSignalSharedPtr& InSignal, int32 InDMXOffset, const FRemoteControlProtocolEntityPtr& InProtocolEntityPtr);

#if WITH_EDITOR
	/**
	 * Process the AutoBinding to the Remote Control Entity
	 * @param InProtocolEntityPtr	Protocol entity pointer
	 */
	void ProcessAutoBinding(const FRemoteControlProtocolEntityPtr& InProtocolEntityPtr);
#endif

private:
	/** Binding for the DMX protocol */
	TArray<FRemoteControlProtocolEntityWeakPtr> ProtocolsBindings;

#if WITH_EDITORONLY_DATA
	/** DMX universe cache buffer.*/
	TArray<uint8, TFixedAllocator<DMX_UNIVERSE_SIZE>> CacheUniverseDMXBuffer;
#endif

public:
	/** DMX protocol name */
	static const FName ProtocolName;
};
