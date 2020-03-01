// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Stats/Stats.h"
#include "Modules/ModuleManager.h"
#include "DMXProtocolModule.h"

#include "Interfaces/IDMXProtocolBase.h"
#include "Interfaces/IDMXProtocolRDM.h"
#include "Interfaces/IDMXProtocolTransport.h"

/**
 * Delegate used when downloading of message contents has completed
 *
 * @param LocalUserNum the controller number of the associated user that made the request
 * @param bWasSuccessful true if the async action completed without error, false if there was an error
 * @param MessageId unique id of the message downloaded
 * @param ErrorStr string representing the error condition
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNetworkInterfaceChanged, const FString&);
typedef FOnNetworkInterfaceChanged::FDelegate FOnNetworkInterfaceChangedDelegate;

struct FDMXUniverse;

/**  Generic protocol interface, it should be inherited by all protocol implementations. */
class DMXPROTOCOL_API IDMXProtocol 
	: public IDMXProtocolBase
	, public IDMXProtocolRDM
{
public:
	static const TMap<FName, IDMXProtocolFactory*>& GetProtocolFactories()
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return DMXProtocolModule.GetProtocolFactories();
	}

	static const TMap<FName, IDMXProtocolPtr>& GetProtocols()
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return DMXProtocolModule.GetProtocols();
	}

	static const TArray<FName>& GetProtocolNames()
	{
		const TMap<FName, IDMXProtocolFactory*>& Protocols = GetProtocolFactories();
		static TArray<FName> ProtocolNames;
		Protocols.GenerateKeyArray(ProtocolNames);
		return ProtocolNames;
	}

	static FName GetFirstProtocolName()
	{
		const TMap<FName, IDMXProtocolFactory*>& ProtocolFactories = IDMXProtocol::GetProtocolFactories();

		for (const auto& Itt : ProtocolFactories)
		{
			return Itt.Key;
		}

		return FName();
	}

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol
	 */
	static IDMXProtocolPtr Get(const FName& ProtocolName = NAME_None)
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return DMXProtocolModule.GetProtocol(ProtocolName);
	}

	/**
	 * Get the Protocol Name
	 * @return Return FName of the protocol
	 */
	virtual const FName& GetProtocolName() const = 0;
	
	/**
	 * Get the Protocol Sender Interface
	 * Sender interface holds the functionality to queue and physically send the DMX buffer
	 * @return Return the pointer to SenderInterface
	 */
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const = 0;

	/**
	 * Get the protocol settings
	 * @return Return the pointer to Protocol Settings
	 */
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;

	/**
	 * Whether protocol enabled
	 * @return Return true if the protocol is enabled
	 */
	virtual bool IsEnabled() const = 0;

	/**
	 * Add universe to the manager
	 * @param  FJsonObject universe settings, such as UniverseID, Subnet, etc.
	 * This is unique to each protocol implementation
	 * @return Return the pointer to universe
	 */
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> AddUniverse(const FJsonObject& InSettings) = 0;

	/**
	 * Collects the universes related to a UniverseManger Entity and add them to
	 * the protocol to be used for communication.
	 * @param Universes The list of universes from the Entity.
	 */
	virtual void CollectUniverses(const TArray<FDMXUniverse>& Universes) = 0;

	/**
	 * Remove Universe from the Protocol Universe Manager.
	 * @param  InUniverseId unique number of universe
	 * @return Return true if it was successfully removed
	 */
	virtual bool RemoveUniverseById(uint32 InUniverseId) = 0;

	/**  Remove all universes from protocol manager */
	virtual void RemoveAllUniverses() = 0;

	/**
	 * Getting Universe from the Protocol Universe Manager.
	 * @param  InUniverseId unique number of universe
	 * @return Return the pointer to the universe, or nullptr if there is no Universe by the given ID
	 */
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> GetUniverseById(uint32 InUniverseId) const = 0;

	/**
	 * Get current amount of universes in the Map
	 * @return Return amount of universes in the Map
	 */
	virtual uint32 GetUniversesNum() const = 0;

	/**
	 * Get minimum supported universe ID for protocol
	 * @return Minimum supported universe ID for protocol
	 */
	virtual uint16 GetMinUniverseID() const = 0;
	
	/**
	 * Get maximum supported universes in protocol
	 * @return Maximum supported universes in protocol
	 */
	virtual uint16 GetMaxUniverses() const = 0;

	/**
	 * Sets the DMX fragment for a particular universe
	 * @param  UniverseID ID of universe to send
	 * @param  DMXFragment Map of DMX channel  and values
	 * @return Return the status of sending
	 */
	virtual EDMXSendResult SendDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment) = 0;

	/**
	 * Sets the DMX fragment for a particular universe
	 * Create protocol Universe if it does not exist
	 * @param  UniverseID ID of universe to send
	 * @param  DMXFragment Map of DMX channel  and values
	 * @return Return the status of sending
	 */
	virtual EDMXSendResult SendDMXFragmentCreate(uint16 UniverseID, const IDMXFragmentMap& DMXFragment) = 0;

	/**
	 * Gets the final protocol universe ID to send
	 * This is implemented protocol-specific offset
	 * @param  UniverseID ID of universe to send
	 * @return Return final Universe ID for sending
	 */
	virtual uint16 GetFinalSendUniverseID(uint16 InUniverseID) const = 0;

	/**
	 * Called on input universe.
	 * Parameters represent: Protocol Name, UniverseID and Buffer
	 */
	DECLARE_EVENT_ThreeParams(IDMXProtocol, FOnUniverseInputUpdateEvent, FName, uint16, const TArray<uint8>&);
	virtual FOnUniverseInputUpdateEvent& GetOnUniverseInputUpdate() = 0;

public:
	static FOnNetworkInterfaceChanged OnNetworkInterfaceChanged;
};


