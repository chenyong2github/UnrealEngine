// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPort.h"

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Containers/Queue.h" 
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"

struct FDMXInputPortConfig;
class FDMXPortManager;
class FDMXRawListener;
class IDMXInputPortTickedBufferOverride;

/**  
 * Higher level abstraction of a DMX input hiding networking specific and protocol specific complexity from the game. 
 *
 * To input DMX into your objects, refer to DMXRawListener and DMXTickedUniverseListener.
 *
 * Can only be constructed via DMXPortManger, see FDMXPortManager::CreateInputPort and FDMXPortManager::CreateInputPortFromConfig
 */
class DMXPROTOCOL_API FDMXInputPort
	: public FDMXPort
	, public FTickableGameObject
{
	// Friend DMXPortManager so it can create instances and unregister void instances
	friend FDMXPortManager;

	// Friend Raw Listener so it can add and remove itself to the port
	friend FDMXRawListener;

protected:
	/** Creates an output port tied to a specific config. Makes the config valid if it's invalid. */
	static FDMXInputPortSharedRef CreateFromConfig(FDMXInputPortConfig& InputPortConfig);

public:
	virtual ~FDMXInputPort();

	/** Updates the Port to use the config of the InputPortConfig. Makes the config valid if it's invalid. */
	void UpdateFromConfig(FDMXInputPortConfig& InputPortConfig);

public:
	// ~Begin DMXPort Interface 

	/** Returns true if the port is successfully registered with its protocol */
	virtual bool IsRegistered() const override;

	/** Returns the Guid of the Port */
	virtual const FGuid& GetPortGuid() const override;

protected:
	/**
	 * Adds a Raw Input that receives all raw signals received on this port. Returns the new Raw Input
	 * Only useful for objects that want to process all data, and not just data on tick (e.g. Activity Monitor)
	 */
	virtual void AddRawInput(TSharedRef<FDMXRawListener> InRawInput) override;

	/**
	 * Removes the raw Input from the port.
	 * Usually doesn't need to be called, as this is called on destruction of the raw Inputs.
	 */
	virtual void RemoveRawInput(TSharedRef<FDMXRawListener> RawInputToRemove) override;

	/** Registers the port with its protocol. Returns true if successfully registered */
	virtual bool Register() override;

	/** Unregisteres the port if it was registered with its protocol */
	virtual void Unregister() override;

	// ~End DMXPort Interface

protected:
	// ~Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// ~End FTickableGameObject interface

public:
	/** Clears all buffers */
	void ClearBuffers();

	/** Single Producer thread-safe: Pushes a DMX Signal into the buffer (For protocol only) */
	void SingleProducerInputDMXSignal(const FDMXSignalSharedRef& DMXSignal);

	/** Gets the last signal received in specified local universe. Returns false if no signal was received. Game-Thread only */
	bool GameThreadGetDMXSignal(int32 LocalUniverseID, FDMXSignalSharedPtr& OutDMXSignal);

	/** Gets all the last signal received. Game-Thread only */
	const TMap<int32, FDMXSignalSharedPtr>& GameThreadGetAllDMXSignals() const;

	/**  DEPRECATED 4.27. Gets the DMX signal from an extern (remote) Universe ID. */
	UE_DEPRECATED(4.27, "Use GameThreadGetDMXSignal instead. GameThreadGetDMXSignalFromRemoteUniverse only exists to support deprecated blueprint nodes.")
	bool GameThreadGetDMXSignalFromRemoteUniverse(FDMXSignalSharedPtr& OutDMXSignal, int32 RemoteUniverseID, bool bEvenIfNotLoopbackToEngine = false);

	/** Returns true if receive DMX is disabled */
	FORCEINLINE bool IsReceiveDMXEnabled() const { return bReceiveDMXEnabled; }

	/** 
	 * Injects a dmx signal into the game thread. 
	 * Note, only signals get from game thread are affected but not raw listeners. 
	 * Useful for nDisplay replication.
	 */
	void GameThreadInjectDMXSignal(const FDMXSignalSharedRef& DMXSignal);

	/** 
	 * Sets if the port should listen to its default queue. 
	 * If false, another raw listener can create the default queue by adding a raw listener and inject its data via GameThreadInjectDMXSignal
	 * Useful for nDisplay replication.
	 */
	void SetUseDefaultQueue(bool bUse);
	
protected:
	/** Called to set if receive DMX should be enabled */
	void OnSetReceiveDMXEnabled(bool bEnabled);

	/** According to DMXProtcolSettings, true if DMX should be received */
	bool bReceiveDMXEnabled;

private:
	/** The default buffer, which is being read on tick */
	TQueue<FDMXSignalSharedPtr> DefaultInputQueue;

	/** If true, adds data to the default input queue and fills the UniverseToLatestSignalMap with it */
	bool bUseDefaultInputQueue = true;

	/** Map of Universe Inputs with their Universes */
	TMap<int32, TSet<TSharedRef<FDMXTickedUniverseListener>>> LocalUniverseToListenerGroupMap;

	/** Map of latest Singals per extern Universe */
	TMap<int32, FDMXSignalSharedPtr> ExternUniverseToLatestSignalMap;

	/** Map of raw isteners */
	TSet<TSharedRef<FDMXRawListener>> RawListeners;

	/** True if the port is registered with it its protocol */
	bool bRegistered;

private:
	/** Returns the port config that corresponds to the guid of this port. */
	FDMXInputPortConfig* FindInputPortConfigChecked() const;

	/** The unique identifier of this port, shared with the port config this was constructed from. Should not be changed after construction. */
	FGuid PortGuid;
};
