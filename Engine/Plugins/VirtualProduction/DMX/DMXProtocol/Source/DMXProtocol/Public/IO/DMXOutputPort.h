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

	/** Determinates if loopback to engine is needed. If true, loopback is needed */
	FORCEINLINE bool IsSendDMXEnabled() const { return bSendEnabled; }

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
	// Friend DMXPortManager so it can create instances and unregister void instances
	friend FDMXPortManager;

	// Friend Raw Listener so it can add and remove itself to the port
	friend FDMXRawListener;

protected:
	/** Creates an output port tied to a specific config. Hidden on purpose, use FDMXPortManager to create instances */
	static FDMXOutputPortSharedRef CreateFromConfig(FDMXOutputPortConfig& OutputPortConfig);

public:
	virtual ~FDMXOutputPort();

	/** Updates the Port to use the config of the OutputPortConfig */
	void UpdateFromConfig(FDMXOutputPortConfig& OutputPortConfig);

	//~ Begin DMXPort Interface 
	virtual bool IsRegistered() const override;
	virtual const FGuid& GetPortGuid() const override;
protected:
	virtual void AddRawListener(TSharedRef<FDMXRawListener> InRawListener) override;
	virtual void RemoveRawListener(TSharedRef<FDMXRawListener> InRawListenerToRemove) override;
	virtual bool Register() override;
	virtual void Unregister() override;
	//~ End DMXPort Interface

public:
	/** Sends DMX over the port */
	void SendDMX(int32 LocalUniverseID, const TMap<int32, uint8>& ChannelToValueMap);

	/** DEPRECATED 4.27. Sends DMX over the port with an extern (remote) Universe ID. Soly here to support legacy functions that would send to an extern universe  */
	UE_DEPRECATED(4.27, "Use SenDMX instead. SendDMXToRemoteUniverse only exists to support deprecated blueprint nodes.")
	void SendDMXToRemoteUniverse(const TMap<int32, uint8>& ChannelToValueMap, int32 RemoteUniverse);

	/** Clears all buffers */
	void ClearBuffers();

	/** Returns true if the port loopsback to engine */
	bool IsLoopbackToEngine() const;

	/** 
	 * Game-Thread only: Gets the last signal received in specified local universe. 
	 * 
	 * @param LocalUniverseID				The local universe that should be retrieved
	 * @param OutDMXSignal					The signal that is set if the opperation succeeds.
	 * @param bEvenIfNotLoopbackToEngine	Defaults to false. If true, succeeds even if the signal should not be looped back to engine (useful for monitoring).
	 * @return								True if the OutDMXSignal was set.
	 */
	bool GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal, bool bEvenIfNotLoopbackToEngine);

	/**  DEPRECATED 4.27. Gets the DMX signal from an extern (remote) Universe ID. */
	UE_DEPRECATED(4.27, "Use GameThreadGetDMXSignal instead. GameThreadGetDMXSignalFromRemoteUniverse only exists to support deprecated blueprint nodes.")
	bool GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine);

	/** Returns the Destination Address */
	FORCEINLINE const FString& GetDestinationAddress() const { return DestinationAddress; }

private:
	/** Called to set if DMX should be enabled */
	void OnSetSendDMXEnabled(bool bEnabled);

	/** Called to set if DMX should be enabled */
	void OnSetReceiveDMXEnabled(bool bEnabled);

	/** Returns the port config that corresponds to the guid of this port. */
	FDMXOutputPortConfig* FindOutputPortConfigChecked() const;
	
	/** The DMX sender, or nullptr if not registered */
	TSharedPtr<IDMXSender> DMXSender;

	/** The Destination Address to send to, can be irrelevant, e.g. for art-net broadcast */
	FString DestinationAddress;

	/** Helper to determine how dmx should be communicated (loopback, send) */
	FDMXOutputPortCommunicationDeterminator CommunicationDeterminator;

	/** Priority on which packets are being sent */
	int32 Priority;

	/** Map of latest Singals per Universe */
	TMap<int32, FDMXSignalSharedPtr> ExternUniverseToLatestSignalMap;

	/** Map of raw Inputs */
	TSet<TSharedRef<FDMXRawListener>> RawListeners;

	/** True if the port is registered with it its protocol */
	bool bRegistered = false;

	/** The unique identifier of this port, shared with the port config this was constructed from. Should not be changed after construction. */
	FGuid PortGuid;
};
