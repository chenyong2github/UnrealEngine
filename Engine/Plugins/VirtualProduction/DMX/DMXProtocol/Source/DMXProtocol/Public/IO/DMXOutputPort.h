// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPort.h"

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Containers/Queue.h" 
#include "Templates/SharedPointer.h"

struct FDMXOutputPortConfig;
class FDMXSignal;
class IDMXSender;


/**
 * Higher level abstraction of a DMX input hiding networking specific and protocol specific complexity.
 *
 * Use SendDMXFragment method to send DMX.
 *
 * To loopback outputs refer to DMXRawListener and DMXTickedUniverseListener.
 *
 * See DMXPortManager for an overview of the port system.
 */
class DMXPROTOCOL_API FDMXOutputPort
	: public FDMXPort
{
	// Friend Raw Listener so it can add and remove themselves to the port
	friend FDMXRawListener;

public:
	FDMXOutputPort();

	virtual ~FDMXOutputPort();

	// ~Begin DMXPort Interface 

public:
	/** Returns true if the port is successfully registered with its protocol */
	virtual bool IsRegistered() const override;

	/** Initializes the port, called from DMXPortManager */
	virtual void Initialize(const FGuid& InPortGuid) override;

	/** Updates the DMXPort from the PortConfig with corresponding Guid */
	virtual void UpdateFromConfig() override;

	/** Returns the Guid of the Port */
	virtual const FGuid& GetPortGuid() const override;
	
protected:
	/**
	 * Adds a Input that receives all raw signals received on this port. Returns the new Input
	 * Only useful for objects that want to process all data, and not just data on tick (e.g. Activity Monitor)
	 */
	virtual void AddRawInput(TSharedRef<FDMXRawListener> RawInput) override;

	/**
	 * Removes the raw Input from the port.
	 * Usually doesn't need to be called, this is called on destruction of the raw Inputs.
	 */
	virtual void RemoveRawInput(TSharedRef<FDMXRawListener> RawInput) override;

	/** Registers the port with its protocol. Returns true if successfully registered */
	virtual bool Register() override;

	/** Unregisteres the port if it was registered with its protocol */
	virtual void Unregister() override;

	// ~End DMXPort Interface

public:
	/** Sends DMX over the port */
	void SendDMX(int32 UniverseID, const TMap<int32, uint8>& ChannelToValueMap);

	/** DEPRECATED 4.27. Sends DMX over the port with an extern (remote) Universe ID. Soly here to support legacy functions that would send to an extern universe  */
	UE_DEPRECATED(4.27, "Use SenDMX instead. SendDMXToRemoteUniverse only exists to support deprecated blueprint nodes.")
	void SendDMXToRemoteUniverse(const TMap<int32, uint8>& ChannelToValueMap, int32 RemoteUniverse);

	/** Clears all buffers */
	void ClearBuffers();

	/** 
	 * Game-Thread only: Gets the last signal received in specified local universe. 
	 * 
	 * @param LocalUniverseID				The local universe that should be retrieved
	 * @param OutDMXSignal					The signal that is set if the opperation succeeds.
	 * @param bEvenIfNotLoopbackToEngine	Defaults to false. If true, succeeds even if the signal should not be looped back to engine (useful for monitoring).
	 * @return								True if the OutDMXSignal was set.
	 */
	bool GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine = false);

	/**  DEPRECATED 4.27. Gets the DMX signal from an extern (remote) Universe ID. */
	UE_DEPRECATED(4.27, "Use GameThreadGetDMXSignal instead. GameThreadGetDMXSignalFromRemoteUniverse only exists to support deprecated blueprint nodes.")
	bool GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine = false);

	/** Returns true if send DMX is enabled */
	FORCEINLINE bool IsSendDMXEnabled() const { return bSendDMXEnabled; }

protected:
	/** Called to set if DMX should be enabled */
	void OnSetSendDMXEnabled(bool bEnabled);

	/** Called to set if DMX should be enabled */
	void OnSetReceiveDMXEnabled(bool bEnabled);
	
	/** The DMX sender, or nullptr if not registered */
	TSharedPtr<IDMXSender> DMXSender;

	/** According to DMXProtocolSettings, true if DMX should be sent */
	bool bSendDMXEnabled;

	/** According to DMXProtocolSettings, true if DMX should be received */
	bool bReceiveDMXEnabled;
	
	/** If true, the port should be input to the engine */
	bool bLoopbackToEngine;

	/** Priority on which packets are being sent */
	int32 Priority;
		
private:
	/** Map of Universe Inputs with their Universes */
	TMap<int32, TSet<TSharedRef<FDMXTickedUniverseListener>>> LocalUniverseToListenerGroupMap;

	/** Map of latest Singals per Universe */
	TMap<int32, FDMXSignalSharedPtr> ExternUniverseToLatestSignalMap;

	/** Map of raw Inputs */
	TSet<TSharedRef<FDMXRawListener>> RawListeners;

	/** True if the port is registered with it its protocol */
	bool bRegistered;

private:
	/** Returns the port config with specified guid. Useful to find a port config by its port, as they share their guid */
	const FDMXOutputPortConfig* FindOutputPortConfigChecked() const;

	/** The unique identifier of this port, shared with the port config this was constructed from. Should not be changed after construction. */
	FGuid PortGuid;
};
