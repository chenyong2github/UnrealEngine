// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"

enum class EDMXCommunicationType : uint8;
class FDMXRawListener;
class FDMXTickedUniverseListener;

class FInternetAddr;


/** 
 * Base class for a higher level abstraction of a DMX input or output. 
 * Higher level abstraction of a DMX input hiding networking specific and protocol specific complexity.
 *
 * Note: Protected memeber variables needs to be initialized in classes that implement this
 */
class DMXPROTOCOL_API FDMXPort
	: public TSharedFromThis<FDMXPort, ESPMode::ThreadSafe>
{
	// Friend Inputs so they can add and remove themselves to the port
	friend FDMXRawListener;
	friend FDMXTickedUniverseListener;

	///////////////////////
	// ~Begin DMXPort Interface declaration
public:
	/** Returns true if the port is successfully registered with its protocol */
	virtual bool IsRegistered() const = 0;

	/** Initializes the port, called from DMXPortManager */
	virtual void Initialize(const FGuid& InPortGuid) = 0;

	/** Updates the DMXPort from the PortConfig with corresponding Guid */
	virtual void UpdateFromConfig() = 0;

	/** Returns the Guid of the Port */
	virtual const FGuid& GetPortGuid() const = 0;
	
protected:	
	/**
	 * Adds a Input that receives all raw signals received on this port. Returns the new Input
	 * Only useful for objects that want to process all data, and not just data on tick (e.g. Activity Monitor)
	 */
	virtual void AddRawInput(TSharedRef<FDMXRawListener> InRawInput) = 0;

	/**
	 * Removes the raw Input from the port.
	 * Usually doesn't need to be called, as this is called on destruction of the raw Inputs.
	 */
	virtual void RemoveRawInput(TSharedRef<FDMXRawListener> RawInput) = 0;

	/** Registers the port with its protocol. Returns true if successfully registered */
	virtual bool Register() = 0;

	/** Unregisteres the port if it was registered with its protocol */
	virtual void Unregister() = 0;
	
	// ~End DMXPort Interface declaration
	///////////////////////

public:
	virtual ~FDMXPort()
	{};

	/** Returns true if the Intern Universe is in this Port's Universe range */
	bool IsLocalUniverseInPortRange(int32 Universe) const;

	/** Returns true if the Extern Universe is in this Port's Universe range */
	bool IsExternUniverseInPortRange(int32 Universe) const;

	/** Returns the offset of the extern universe. LocalUniverse == ExternUniverse - ExternUniverseOffset */
	int32 GetExternUniverseOffset() const;

	/** Converts an extern Universe ID to a local Universe ID */
	int32 ConvertExternToLocalUniverseID(int32 ExternUniverseID) const;

	/** Converts a local Universe ID to an extern Universe ID */
	int32 ConvertLocalToExternUniverseID(int32 LocalUniverseID) const;

	FORCEINLINE const FString& GetPortName() const { return PortName; }
	
	FORCEINLINE const FString& GetAddress() const { return Address; }

	FORCEINLINE const IDMXProtocolPtr& GetProtocol() const { return Protocol; }

	FORCEINLINE EDMXCommunicationType GetCommunicationType() const { return CommunicationType; }

	FORCEINLINE int32 GetLocalUniverseStart() const { return LocalUniverseStart; }

	int32 GetLocalUniverseEnd() const { return LocalUniverseStart + NumUniverses - 1; }

	int32 GetExternUniverseStart() const { return ExternUniverseStart; }

	 int32 GetExternUniverseEnd() const { return ExternUniverseStart + NumUniverses - 1; }

	/** Broadcast when the port is updated */
	FSimpleMulticastDelegate OnPortUpdated;

protected:
	/** Tests whether the port is valid */
	bool IsValidPortSlow() const;

	////////////////////////////////////////////////////////////////
	// Variables that need be initialized from derived classes
protected:
	/** The name displayed wherever the port can be displayed */
	FString PortName;

	/** The protocol of this port */
	IDMXProtocolPtr Protocol;

	/** The communication type of this port */
	EDMXCommunicationType CommunicationType;

	/** The IP address of this port */
	FString Address;

	/** The Local Start Universe */
	int32 LocalUniverseStart;

	/** Number of Universes */
	int32 NumUniverses;

	/**
	 * The start address this being transposed to.
	 * E.g. if LocalUniverseStart is 1 and this is 100, Local Universe 1 is sent as Universe 100.
	 */
	int32 ExternUniverseStart;
};
