// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Stats/Stats.h"
#include "Modules/ModuleManager.h"
#include "DMXProtocolModule.h"

#include "Interfaces/IDMXProtocolBase.h"
#include "Interfaces/IDMXProtocolRDM.h"
#include "Interfaces/IDMXProtocolTransport.h"

class FDMXSignal;

/**
 * Delegate used when a network interface has been changed
 *
 * @param InMessage Error message
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNetworkInterfaceChanged, const FString& /*InMessage*/);
typedef FOnNetworkInterfaceChanged::FDelegate FOnNetworkInterfaceChangedDelegate;

/**
 * Delegate used when a Receiving thread settings has been changed
 *
 * @param InRefreshRate						Receiving refresh rate
 * @param bInUseSeparateReceivingThread		If true, uses a separate thread to receive DMX
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnReceivingThreadChanged, int32 /*InRefreshRate*/, bool/*bInUseSeparateReceivingThread*/);
typedef FOnReceivingThreadChanged::FDelegate FOnReceivingThreadChangedDelegate;

struct FDMXCommunicationEndpoint;

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
	

	virtual const IDMXUniverseSignalMap& GameThreadGetInboundSignals() const = 0;

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
	 * Sets if DMX is sent to the network. 
	 * NOTE: Should be set to same for all protocols as. This is a globally accessible switch, see DMXProtocolSettings, DMXProtocolBlueprintLibrary.
	 * @param bEnabled	If true, sends DMX signals to the network.
	 */
	virtual void SetSendDMXEnabled(bool bEnabled) = 0;

	 /**
	 * Returns whether dmx is received from network.
	 * @return Return	If true, DMX is received from the network.
	 */
	virtual bool IsSendDMXEnabled() const = 0;

	/**
	 * Sets if DMX is received from the network. 
	 * NOTE: Should be set to same for all protocols. This is a globally accessible switch, see DMXProtocolSettings, DMXProtocolBlueprintLibrary.
	 * @param bEnabled	If true, receives inbound DMX signals, else ignores them.
	 */
	virtual void SetReceiveDMXEnabled(bool bEnabled) = 0;

	 /**
	 * Returns whether dmx is received from network.
	 * @return Return	If true, DMX is received from the network.
	 */
	virtual bool IsReceiveDMXEnabled() const = 0;

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
	virtual void CollectUniverses(const TArray<FDMXCommunicationEndpoint>& Endpoints) = 0;

	/**
	* Update the universe by id in universe manager
	* @param InUniverseId id of the universe we are going to update
	* @param InSettings FJsonObject of universe settings, such as UniverseId, Unicast IP, etc
	**/
	virtual void UpdateUniverse(uint32 InUniverseId, const FJsonObject& InSettings) = 0;

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
	 * Getting Universe from the Protocol Universe Manager.
	 * Creates a default new universe if the universe doesn't exist
	 *
	 * @param  InUniverseId unique number of universe
	 * @return Return the pointer to the universe.
	 */
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> GetUniverseByIdCreateDefault(uint32 InUniverseId);

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
	 * Injects a DMX fragment directly into the input buffers. No networking involved.
	 * @param  UniverseID ID of universe to input
	 * @param  DMXFragment Map of DMX channel  and values
	 * @return Return the status of sending
	 */
	virtual EDMXSendResult InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment) = 0;

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
	 * Sets zeroed DMX universe
	 *
	 * @param  UniverseID ID of universe to send
	 * @param  bForceSendDMX whether DMX should be sent over the network
	 * @return Return the status of sending
	 */
	virtual EDMXSendResult SendDMXZeroUniverse(uint16 UniverseID, bool bForceSendDMX = false) = 0;

	/**
	 * Gets the final protocol universe ID to send
	 * This is implemented protocol-specific offset
	 * @param  UniverseID ID of universe to send
	 * @return Return final Universe ID for sending
	 */
	virtual uint16 GetFinalSendUniverseID(uint16 InUniverseID) const = 0;

	/**
	 * Modify the FJsonObject passed in with the correct fields for Universe InUniverseID
	 * @param InUniverseID Universe ID 
	 * @param OutSettings is the FJsonObject to set the fields in
	 */
	virtual void GetDefaultUniverseSettings(uint16 InUniverseID, FJsonObject& OutSettings) const = 0;

	/**
	 * Zeroes Input DMX Buffers in all active Universes
	 */
	virtual void ClearInputBuffers() = 0;

	/**
	 * Zeroes Output DMX Buffers in all active Universes
	 */
	virtual void ZeroOutputBuffers() = 0;


	/**
	 * Called on when a Universe Input Buffer was updated
	 * Event Parameters: FName ProtocolName, uint16 UniverseID, const TArray<uint8>& InputBuffer
	 */
	DECLARE_EVENT_ThreeParams(IDMXProtocol, FOnUniverseInputBufferUpdated, FName, uint16, const TArray<uint8>&);
	virtual FOnUniverseInputBufferUpdated& GetOnUniverseInputBufferUpdated() = 0;

	/**
	 * Called on when a Universe Output Buffer was updated
	 * Event Parameters: FName ProtocolName, uint16 UniverseID, const TArray<uint8>& OutputBuffer
	 */
	DECLARE_EVENT_ThreeParams(IDMXProtocol, FOnUniverseOutputBufferUpdated, FName, uint16, const TArray<uint8>&);
	virtual FOnUniverseOutputBufferUpdated& GetOnUniverseOutputBufferUpdated() = 0;

	/**
	 * Called when a packet was Received
	 * Event Parameters: FName ProtocolName, uint16 UniverseID, const TArray<uint8>& Packet
	 */
	DECLARE_EVENT_ThreeParams(IDMXProtocol, FOnPacketReceived, FName, uint16, const TArray<uint8>&);
	virtual FOnPacketReceived& GetOnPacketReceived() = 0;

	/**
	 * Called when a packet was sent
	 * Event Parameters: FName Protocol Name, uint16 UniverseID, const TArray<uint8>& Packet
	 */
	DECLARE_EVENT_ThreeParams(IDMXProtocol, FOnPacketSent, FName, uint16, const TArray<uint8>&);
	virtual FOnPacketSent& GetOnPacketSent() = 0;

public:
	/** Delegate used for listening to a network interface changes  */
	static FOnNetworkInterfaceChanged OnNetworkInterfaceChanged;
};

