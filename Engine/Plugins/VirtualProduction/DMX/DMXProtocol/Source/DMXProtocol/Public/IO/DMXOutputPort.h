// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPort.h"

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Containers/Queue.h" 
#include "Templates/SharedPointer.h"

struct FDMXOutputPortConfig;
class FDMXPortManager;
class FDMXSignal;
class IDMXSender;


/** Helper to determine how DMX should be communicated (loopback, send) */
struct FDMXOutputPortCommunicationDeterminator
{
	FDMXOutputPortCommunicationDeterminator()
		: bLoopbackToEngine(false)
		, bReceiveEnabled(true)
		, bSendEnabled(true)
		, bHasValidSender(false)
	{}

	/** Set the variable from the port config in project settings */
	FORCEINLINE void SetLoopbackToEngine(bool bInLoopbackToEngine) { bLoopbackToEngine = bInLoopbackToEngine; }

	/** Sets if receive is enabled */
	FORCEINLINE void SetReceiveEnabled(bool bInReceiveEnabled) { bReceiveEnabled = bInReceiveEnabled; }

	/** Sets if send is enabled */
	FORCEINLINE void SetSendEnabled(bool bInSendEnabled) { bSendEnabled = bInSendEnabled; }

	/** Sets if there is a valid sender obj */
	FORCEINLINE void SetHasValidSender(bool bInHasValidSender) { bHasValidSender = bInHasValidSender; }

	/** Determinates if loopback to engine is needed. If true, loopback is needed */
	FORCEINLINE bool NeedsLoopbackToEngine() const { return bLoopbackToEngine || !bReceiveEnabled || !bSendEnabled; }

	/** Determinates if loopback to engine is needed. If true, loopback is needed */
	FORCEINLINE bool NeedsSendDMX() const { return bSendEnabled && bHasValidSender; }

private:
	bool bLoopbackToEngine;
	bool bReceiveEnabled;
	bool bSendEnabled;
	bool bHasValidSender;
};


/**
 * Higher level abstraction of a DMX input hiding networking specific and protocol specific complexity.
 *
 * Use SendDMXFragment method to send DMX.
 *
 * To loopback outputs refer to DMXRawListener and DMXTickedUniverseListener.
 *
 * Can only be constructed via DMXPortManger, see FDMXPortManager::CreateOutputPort and FDMXPortManager::CreateOutputPortFromConfig
 */
class DMXPROTOCOL_API FDMXOutputPort
	: public FDMXPort
{
	// Friend Raw Listener so it can add and remove themselves to the port
	friend FDMXPortManager;

	// Friend Raw Listener so it can add and remove themselves to the port
	friend FDMXRawListener;

protected:
	/** Creates an output port that is not tied to a specific config. Hidden on purpose, use FDMXPortManager to create instances */
	static FDMXOutputPortSharedRef Create();

	/** Creates an output port tied to a specific config. Hidden on purpose, use FDMXPortManager to create instances */
	static FDMXOutputPortSharedRef CreateFromConfig(const FDMXOutputPortConfig& OutputPortConfig);

public:
	virtual ~FDMXOutputPort();

	/** Updates the Port to use the config of the OutputPortConfig */
	void UpdateFromConfig(const FDMXOutputPortConfig& OutputPortConfig);

public:
	// ~Begin DMXPort Interface 

	/** Returns true if the port is successfully registered with its protocol */
	virtual bool IsRegistered() const override;

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

	/** Returns the Destination Address */
	FORCEINLINE const FString& GetDestinationAddress() const { return DestinationAddress; }

protected:
	/** Called to set if DMX should be enabled */
	void OnSetSendDMXEnabled(bool bEnabled);

	/** Called to set if DMX should be enabled */
	void OnSetReceiveDMXEnabled(bool bEnabled);
	
	/** The DMX sender, or nullptr if not registered */
	TSharedPtr<IDMXSender> DMXSender;

	/** The Destination Address to send to, can be irrelevant, e.g. for art-net broadcast */
	FString DestinationAddress;

	/** Helper to determine how dmx should be communicated (loopback, send) */
	FDMXOutputPortCommunicationDeterminator CommunicationDeterminator;

	/** Priority on which packets are being sent */
	int32 Priority;
		
private:
	/** Map of latest Singals per Universe */
	TMap<int32, FDMXSignalSharedPtr> ExternUniverseToLatestSignalMap;

	/** Map of raw Inputs */
	TSet<TSharedRef<FDMXRawListener>> RawListeners;

	/** True if the port is registered with it its protocol */
	bool bRegistered;

private:
	/** Returns the port config that corresponds to the guid of this port. */
	const FDMXOutputPortConfig* FindOutputPortConfigChecked() const;

	/** The unique identifier of this port, shared with the port config this was constructed from. Should not be changed after construction. */
	FGuid PortGuid;
};
