// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPort.h"

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Containers/Queue.h" 
#include "HAL/Runnable.h"
#include "Misc/ScopeLock.h" 
#include "Misc/SingleThreadRunnable.h"
#include "Templates/Atomic.h"

struct FDMXOutputPortConfig;
struct FDMXOutputPortDestinationAddress;
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


/** Helper to access a Map of Signals per Universe in a thread-safe way */
class FDMXThreadSafeUniverseToSignalMap
{
public:
	using TUniverseSignalPredicate = TFunctionRef<void(int32, const FDMXSignalSharedPtr&)>;
	
	/** Gets a signal, returns an invalid shared pointer if there is no signal at given Universe ID */
	FDMXSignalSharedPtr GetSignal(int32 UniverseID) const;

	/** Gets or creates a signal */
	FDMXSignalSharedRef GetOrCreateSignal(int32 UniverseID);

	/** Adds a signal to the map */
	void AddSignal(const FDMXSignalSharedRef& Signal);

	/** Loops through all signals by given Predicate  */
	void ForEachSignal(TUniverseSignalPredicate Predicate);

	/** Resets the Map */
	void Reset();

private:
	/** Buffer of all the latest DMX Signals that were sent */
	TMap<int32, FDMXSignalSharedPtr> UniverseToSignalMap;

	/** Critical sequestion required to be used when the Map is accessed */
	FCriticalSection CriticalSection;
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
	, public FRunnable
	, public FSingleThreadRunnable
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

	/** Returns the Destination Addresses */
	TArray<FString> GetDestinationAddresses() const;

	/**  DEPRECATED 4.27. Gets the DMX signal from an extern (remote) Universe ID. */
	UE_DEPRECATED(4.27, "Use GameThreadGetDMXSignal instead. GameThreadGetDMXSignalFromRemoteUniverse only exists to support deprecated blueprint nodes.")
	bool GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine);

	/** DEPRECATED 5.0 */
	UE_DEPRECATED(5.0, "Output Ports now support many destination addresses. Please use FDMXOutputPort::GetDestinationAddresses instead.")
	FString GetDestinationAddress() const;

public:
	//~ Begin DMXPort Interface 
	virtual bool IsRegistered() const override;
	virtual const FGuid& GetPortGuid() const override;

protected:
	virtual void AddRawListener(TSharedRef<FDMXRawListener> InRawListener) override;
	virtual void RemoveRawListener(TSharedRef<FDMXRawListener> InRawListenerToRemove) override;
	virtual bool Register() override;
	virtual void Unregister() override;
	//~ End DMXPort Interface

		/** Called to set if DMX should be enabled */
	void OnSetSendDMXEnabled(bool bEnabled);

	/** Called to set if DMX should be enabled */
	void OnSetReceiveDMXEnabled(bool bEnabled);

	/** Returns the port config that corresponds to the guid of this port. */
	FDMXOutputPortConfig* FindOutputPortConfigChecked() const;

	//~ Begin FRunnable implementation
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable implementation

	//~ Begin FSingleThreadRunnable implementation
	virtual void Tick() override;
	virtual class FSingleThreadRunnable* GetSingleThreadInterface() override;
	//~ End FSingleThreadRunnable implementation

	/** Updates the thread, sending DMX */
	void ProcessSendDMX();

private:
	/** The DMX senders in use */
	TArray<TSharedPtr<IDMXSender>> DMXSenderArray;

	/** Buffer of the signals that are to be sent in the next frame */
	TQueue<FDMXSignalSharedPtr> NewDMXSignalsToSend;

	/** Map that holds the latest Signal per Universe */
	FDMXThreadSafeUniverseToSignalMap LatestDMXSignals;

	/** The Destination Address to send to, can be irrelevant, e.g. for art-net broadcast */
	TArray<FDMXOutputPortDestinationAddress> DestinationAddresses;

	/** Helper to determine how dmx should be communicated (loopback, send) */
	FDMXOutputPortCommunicationDeterminator CommunicationDeterminator;

	/** Priority on which packets are being sent */
	int32 Priority = 0;

	/** Map of raw Inputs */
	TSet<TSharedRef<FDMXRawListener>> RawListeners;

	/** True if the port is registered with it its protocol */
	bool bRegistered = false;

	/** The unique identifier of this port, shared with the port config this was constructed from. Should not be changed after construction. */
	FGuid PortGuid;

	/** Delay to apply on packets being sent */
	double DelaySeconds = 0.0;

	/** Holds the thread object. */
	FRunnableThread* Thread = nullptr;

	/** Flag indicating that the thread is stopping. */
	TAtomic<bool> bStopping;
};
