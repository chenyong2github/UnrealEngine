// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlProtocol.h"
#include "RemoteControlProtocolBinding.h"

#include "RemoteControlProtocolOSC.generated.h"

struct FOSCMessage;

/**
 * OSC protocol entity for remote control binding
 */
USTRUCT()
struct FRemoteControlOSCProtocolEntity : public FRemoteControlProtocolEntity
{	
	GENERATED_BODY()

public:
	//~ Begin FRemoteControlProtocolEntity interface
	virtual FName GetRangePropertyName() const override { return NAME_FloatProperty; }
	//~ End FRemoteControlProtocolEntity interface

public:
	/** OSC address in the form '/Container1/Container2/Method' */
	UPROPERTY(EditAnywhere, Category = Mapping)
	FName PathName;

	/** OSC range input property template, used for binding. */
	UPROPERTY(Transient, meta = (ClampMin = 0.0, ClampMax = 1.0))
	float RangeInputTemplate = 0.0f;
};

/**
 * OSC protocol implementation for Remote Control
 */
class FRemoteControlProtocolOSC : public FRemoteControlProtocol
{
public:
	FRemoteControlProtocolOSC()
		: FRemoteControlProtocol(ProtocolName)
	{}
	
	//~ Begin IRemoteControlProtocol interface
	virtual void Bind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void Unbind(FRemoteControlProtocolEntityPtr InRemoteControlProtocolEntityPtr) override;
	virtual void UnbindAll() override;
	virtual UScriptStruct* GetProtocolScriptStruct() const override { return FRemoteControlOSCProtocolEntity::StaticStruct(); }
	//~ End IRemoteControlProtocol interface

	/** Recieve OSC server message handler */
	void OSCReceivedMessageEvent(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);

private:
	/** Map of the OSC bindings */
	TMap<FName, TArray<FRemoteControlProtocolEntityWeakPtr>> Bindings;

public:
	/** OSC protocol name */
	static const FName ProtocolName;
};
