// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

struct FDMXInputPortConfig;
struct FDMXOutputPortConfig;
class FDMXPort;
class FDMXInputPort;
class FDMXOutputPort;


/** 
 * Manager for all DMX ports. Exposes available input and output ports anywhere.
 *

 * A) Overview of the IO System - for developers that want to send and receive DMX
 * ============================
 *
 * 1. Definition of Ports in Project Settings:
 * -------------------------------------------
 * DMX Protocol Settings (the DMX Project Settings) holds arrays of DMX Port Configs. This is where ports are defined.
 * Port Manager automatically creates DMXInputPorts and DMXOutput ports that match these settings. 
 * Generally this is self-contained and does not need any user code.
 *
 * 
 * 2. Acquire a DMX Input or Output Port:
 * -------------------------------------_
 * Get all input ports or all output ports available via the Port Managers GetInputPorts and GetOutputPorts methods.
 * Alternatively use the Editor-Only SDMXPortSelector widget to select a port from available ports.
 * 
 * Note: Creating and destroying ports can only be done in Project settings. 
 *		 Applications that want to offer dynamic ports should specify a fixed number of ports, e.g. 8, 16 ports and work with these at runtime.
 * 
 *
 * 3. Receive DMX in your object:
 * ----------------------------------
 * a) Latest (frame time relevant) data on the Game-Thread:
 * 
 * - Get the port you want to use (see 2. acquire a DMX Input or Output port). GameThreadGetDMXSignal to get a DMX Signal of a local universe.
 * 
 * b) All data on any thread:
 * 
 * - Create an instance of DMXRawListner. Use its constructor to specify which port it should use (see 2. acquire a DMX Input or Output port)
 * - Use DequeueSignal to receive DMX.
 *
 * Note: This applies for both input and output ports. This is to provide loopback functionality for outputs.
 *		 Generally you want to listen to all ports, not just the inputs. 
 *
 * Note: GetDMXData is the right method to use for almost any use-case.
 *		 DMXRawListener is only useful where the latest data isn't sufficient, e.g. to record all incoming data in Sequencer.
 *		 DMXRawListener is thread-safe, but may stall the engine when used in the game-thread due to the possibly infinite work load it leaves to the user.
 *
 * Note: DMX only runs at a rate of 44Hz by its own Standard, which may be a lower rate than the engine's frame rate.
 *       This may cause visible artifacts. It is recommended to interpolate between received values to overcome the issue.
 *
 *
 * 4. Send DMX from your object:
 * -------------------------------
 * Use the DMXOutputPort's SendDMX method to output DMX
 *
 */
class DMXPROTOCOL_API FDMXPortManager
{
	////////////////////////////////
	// Commonly used methods

	DECLARE_MULTICAST_DELEGATE(FDMXOnPortArraysChangedDelegate);
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXOnPortChangedDelegate, const FGuid& /** PortGuid */)
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDMXOnPortInputDequeuedDelegate, FDMXInputPortSharedRef /** DMXInputPortRef */, FDMXSignalSharedRef /** DMXSignalRef */)

public:
	/** Broadcast when port arrays or data changed */
	FDMXOnPortArraysChangedDelegate OnPortsChanged;

	/** Broadcast when a DMX Signal is dequeued from the port */
	FDMXOnPortInputDequeuedDelegate OnPortInputDequeued;

public:
	static FDMXPortManager& Get();

	FORCEINLINE const TArray<FDMXInputPortSharedRef>& GetInputPorts() const { return InputPorts; }

	FORCEINLINE const TArray<FDMXOutputPortSharedRef>& GetOutputPorts() const { return OutputPorts; }

	/** Adds a new input port */
	FDMXInputPortSharedRef CreateInputPort();

	/** 
	 * Adds a new input port that corresponds to the input port config. 
	 * Consumes (moves) the config and returns a copy that uses the port's guid.
	 * This is to prevent from adding a config (with the same guid) twice.
	 */
	FDMXInputPortSharedRef GetOrCreateInputPortFromConfig(const FDMXInputPortConfig& InputPortConfig);

	/** Removes the input port */
	void RemoveInputPortChecked(const FGuid& PortGuid);

	/** Adds a new output port */
	FDMXOutputPortSharedRef CreateOutputPort();

	/** 
	 * Adds a new output port that corresponds to the input port config. 
	 * Consumes (moves) the config and returns a copy that uses the port's guid 
	 * This is to prevent from adding a config (with the same guid) twice.
	 */
	FDMXOutputPortSharedRef GetOrCreateOutputPortFromConfig(const FDMXOutputPortConfig& OutputPortConfig);

	/** Removes the input port */
	void RemoveOutputPortChecked(const FGuid& PortGuid);

	/** Returns the port matching the port guid. Returns nullptr if the port doesn't exist. */
	FDMXPortSharedPtr FindPortByGuid(const FGuid& PortGuid) const;

	/** Returns the port matching the port guid, checked version. */
	FDMXPortSharedRef FindPortByGuidChecked(const FGuid& PortGuid) const; 

	/** Returns the input port matching the port guid. Returns nullptr if the port doesn't exist. */
	FDMXInputPortSharedPtr FindInputPortByGuid(const FGuid& PortGuid) const;

	/** Returns the input port matching the port guid, checkedversion.*/
	FDMXInputPortSharedRef FindInputPortByGuidChecked(const FGuid& PortGuid) const;

	/** Returns the output port matching the port guid. Returns nullptr if the port doesn't exist. */
	FDMXOutputPortSharedPtr FindOutputPortByGuid(const FGuid& PortGuid) const;

	/** Returns the output port matching the port guid, checked version. */
	FDMXOutputPortSharedRef FindOutputPortByGuidChecked(const FGuid& PortGuid) const;

	/** Updates ports from protocol settings, does not affect other ports added by the create port methods */
	void UpdateFromProtocolSettings();

private:
	/** Array of input ports */
	TArray<FDMXInputPortSharedRef> InputPorts;

	/** Array of output ports */
	TArray<FDMXOutputPortSharedRef> OutputPorts;

	/** Array of Port Guids added from protocol settings */
	TArray<FGuid> PortGuidsFromProtocolSettings;

	////////////////////////////////////////////////////////////
	// Initialization 
public:
	FDMXPortManager() = default;
	virtual ~FDMXPortManager();

	// Non-copyable
	FDMXPortManager(FDMXPortManager const&) = delete;
	void operator=(FDMXPortManager const& x) = delete;

private:
	static TUniquePtr<FDMXPortManager> CurrentManager;

public:
	/** Initializes the manager */
	static void StartupManager();

	/** Destroys the manager. */
	static void ShutdownManager();
};
