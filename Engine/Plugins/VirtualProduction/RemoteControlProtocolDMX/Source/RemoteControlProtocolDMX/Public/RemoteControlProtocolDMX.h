// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlProtocol.h"

#include "DMXProtocolCommon.h"
#include "RemoteControlProtocolBinding.h"
#include "Tickable.h"

#include "RemoteControlProtocolDMX.generated.h"

class FRemoteControlProtocolDMX;
enum class EDMXFixtureSignalFormat : uint8;

/**
 * DMX protocol entity for remote control binding
 */
USTRUCT()
struct FRemoteControlDMXProtocolEntity : public FRemoteControlProtocolEntity
{
	GENERATED_BODY()

	friend class FRemoteControlProtocolDMX;

public:
	//~ Begin FRemoteControlProtocolEntity interface
	virtual FName GetRangePropertyName() const override { return NAME_UInt32Property; }
	//~ End FRemoteControlProtocolEntity interface

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
	bool bUseLSB;

	/** Starting universe channel */
	UPROPERTY(EditAnywhere, Category = Mapping, meta = (ClampMin = "1", ClampMax = "509", UIMin = "1", UIMax = "509"))
	int32 StartingChannel = 1;

	/** Defines the used number of channels (bytes) */
	UPROPERTY(EditAnywhere, Category = Mapping)
	EDMXFixtureSignalFormat DataType;

	/** DMX range input property template, used for binding. */
	UPROPERTY(Transient)
	uint32 RangeInputTemplate = 0;

private:
	/** DMX entity cache buffer. From 1 up to 4 channels, based on DataType */
	TArray<uint8> CacheDMXBuffer;

	/** A single, generic DMX signal. One universe of raw DMX data received */
	FDMXSignalSharedPtr LastSignalPtr;
};

/**
 * DMX protocol implementation for Remote Control
 */
class FRemoteControlProtocolDMX : public FRemoteControlProtocol, public FTickableGameObject
{
public:
	//~ Begin IRemoteControlProtocol interface
	virtual void Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void UnbindAll() override;
	virtual UScriptStruct* GetProtocolScriptStruct() const override { return FRemoteControlDMXProtocolEntity::StaticStruct(); }
	//~ End IRemoteControlProtocol interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject interface

private:
	/**
	 * Apply dmx channel data to the bound property, potentually resize the cache buffer
	 * @param InChannelData DMX channel bytes
	 * @param InProtocolEntityPtr protocol entity
	 */
	void ProcessAndApplyProtocolValue(const uint8* InChannelData, const FRemoteControlProtocolEntityPtr& InProtocolEntityPtr);

private:
	/** Binding for the DMX protocol */
	TArray<FRemoteControlProtocolEntityWeakPtr> ProtocolsBindings;
};
