// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlProtocol.h"
#include "RemoteControlProtocolBinding.h"

#include "UObject/StrongObjectPtr.h"

#include "RemoteControlProtocolMIDI.generated.h"

class UMIDIDeviceInputController;

/**
 * MIDI protocol entity for remote control binding
 */
USTRUCT()
struct FRemoteControlMIDIProtocolEntity : public FRemoteControlProtocolEntity
{
	GENERATED_BODY()

public:
	//~ Begin FRemoteControlProtocolEntity interface
	virtual FName GetRangePropertyName() const override { return NAME_ByteProperty; }
	//~ End FRemoteControlProtocolEntity interface

public:
	/** Midi Device Id */
	UPROPERTY(EditAnywhere, Category = Mapping)
	int32 DeviceId = 1;

	/** Midi Event type */
	UPROPERTY(EditAnywhere, Category = Mapping)
	int32 EventType = 11;

	/** Midi button event message data id for binding */
	UPROPERTY(EditAnywhere, Category = Mapping, meta = (DisplayName = "Mapped channel Id"))
	int32 MessageData1 = 0;

	/** Midi device channel */
	UPROPERTY(EditAnywhere, Category = Mapping)
	int32 Channel = 1;

	/** Midi range input property template, used for binding. */
	UPROPERTY(Transient, meta = (ClampMin = 0, ClampMax = 127))
	uint8 RangeInputTemplate = 0;
};

/**
 * MIDI protocol implementation for Remote Control
 */
class FRemoteControlProtocolMIDI : public FRemoteControlProtocol
{
public:
	//~ Begin IRemoteControlProtocol interface
	virtual void Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void UnbindAll() override;
	virtual UScriptStruct* GetProtocolScriptStruct() const override { return FRemoteControlMIDIProtocolEntity::StaticStruct(); }
	//~ End IRemoteControlProtocol interface

private:
	/** On receive MIDI buffer callback */
	void OnReceiveEvent(UMIDIDeviceInputController* MIDIDeviceController, int32 Timestamp, int32 Type, int32 Channel, int32 MessageData1, int32 MessageData2);

	/** Binding for the MIDI protocol */
	TMap< UMIDIDeviceInputController*, TMap<int32, TArray<FRemoteControlProtocolEntityWeakPtr> > > MIDIDeviceBindings;

	/** MIDI devices */
	TMap<int32, TStrongObjectPtr<UMIDIDeviceInputController>> MIDIDevices;
};
