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
 * Note: OutputPorts send DMX at a rate of 44Hz only, to comply with the DMX standard.
 *
 */
class DMXPROTOCOL_API FDMXPortManager
{
	////////////////////////////////
	// Commonly used methods

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FDMXEditorChangedPortArraysDelegate);
	DECLARE_MULTICAST_DELEGATE_OneParam(FDMXEditorEditedPortDelegate, const FGuid& /** PortGuid */)

public:
	/** Broadcast when port cgs array changed in Editor */
	FDMXEditorChangedPortArraysDelegate EditorChangedPorts;

	/** Broadcast when a port was edited in Editor, but ports arrays remain unchaned */
	FDMXEditorEditedPortDelegate EditorEditedPort;
#endif // WITH_EDITOR

public:
	static FDMXPortManager& Get();

	FORCEINLINE const TArray<FDMXInputPortSharedRef>& GetInputPorts() const { return InputPorts; }

	FORCEINLINE const TArray<FDMXOutputPortSharedRef>& GetOutputPorts() const { return OutputPorts; }

	/** Returns the port matching the port guid, checked version. */
	FDMXPortSharedRef FindPortByGuidChecked(const FGuid& PortGuid) const; 

private:
	/** Array of input ports */
	TArray<FDMXInputPortSharedRef> InputPorts;

	/** Array of output ports */
	TArray<FDMXOutputPortSharedRef> OutputPorts;


	////////////////////////////////////////////////////////////
	// Initialization and consistency with UDMXProtocolSettings
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

#if WITH_EDITOR
	/** Notifies the manager when a port changed, should be called when the ports changed */
	void NotifyPortConfigChanged(const FGuid& PortGuid);

	/** Notifies the manager when the port arrays changed, should be called when the ports changed */
	void NotifyPortConfigArraysChanged();
#endif // WITH_EDITOR

private:
	/** Non-static function to initialize the manager */
	void StartupManagerInternal();

	/** Non-static function to shut down the manager */
	void ShutdownManagerInternal();

	/** Sets up the input port and its config */
	void SetupInputPort(FDMXInputPortConfig& MutablePortConfig);
	
	/** Sets up the output port and its config */
	void SetupOutputPort(FDMXOutputPortConfig& MutablePortConfig);
};
