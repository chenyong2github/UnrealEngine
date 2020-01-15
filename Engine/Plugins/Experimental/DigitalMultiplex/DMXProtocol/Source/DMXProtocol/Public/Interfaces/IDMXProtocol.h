// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Stats/Stats.h"
#include "Modules/ModuleManager.h"
#include "DMXProtocolModule.h"

#include "Interfaces/IDMXProtocolBase.h"
#include "Interfaces/IDMXProtocolRDM.h"
#include "Interfaces/IDMXProtocolTransport.h"

class FDMXProtocolDeviceManager;
class FDMXProtocolInterfaceManager;
class FDMXProtocolPortManager;
class FDMXProtocolUniverseManager;

DMXPROTOCOL_API DECLARE_LOG_CATEGORY_EXTERN(LogDMXProtocol, Log, All);

DECLARE_STATS_GROUP(TEXT("DMXProtocol"), STATGROUP_DMXProtocol, STATCAT_Advanced);

#ifndef DMXPROTOCOL_LOG_PREFIX
#define DMXPROTOCOL_LOG_PREFIX TEXT("DMX: ")
#endif

#define UE_LOG_DMXPROTOCOL(Verbosity, Format, ...) \
{ \
	UE_LOG(LogDMXProtocol, Verbosity, TEXT("%s%s"), DMXPROTOCOL_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define UE_CLOG_DMXPROTOCOL(Conditional, Verbosity, Format, ...) \
{ \
	UE_CLOG(Conditional, LogDMXProtocol, Verbosity, TEXT("%s%s"), DMXPROTOCOL_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

/**
 * Generic protocol interface, it should be inherited by all protocol implementations.
 */
class DMXPROTOCOL_API IDMXProtocol 
	: public IDMXProtocolBase
	, public IDMXProtocolRDM
{
public:
	virtual ~IDMXProtocol() {}

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol
	 */
	template <class TProtocol>
	static TProtocol* Get(const FName& ProtocolName = NAME_None)
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return static_cast<TProtocol*>(DMXProtocolModule.GetProtocol(ProtocolName));
	}

	static const TMap<FName, IDMXProtocolFactory*>& GetProtocolFactories()
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return DMXProtocolModule.GetProtocolFactories();
	}

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol
	 */
	static IDMXProtocol* Get(const FName& ProtocolName = NAME_None)
	{
		static const FName DMXProtocolModuleName = TEXT("DMXProtocol");
		FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>(DMXProtocolModuleName);
		return DMXProtocolModule.GetProtocol(ProtocolName);
	}

	/**
	 * Get the Protocol Name
	 * @return Return FName of the protocol
	 */
	virtual FName GetProtocolName() const = 0;
	
	/**
	 * Get the Protocol Sender Interface
	 * Sender interface holds the functionality to queue and physically send the DMX buffer
	 * @return Return the pointer to SenderInterface
	 */
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const = 0;

	/**
	 * Get the device manager for a protocol instance
	 * The device manager is responsible for hold and searches the physical devices
	 * Physical devices are nodes, controllers, consoles
	 * @return Return the pointer to DeviceManager
	 */
	virtual TSharedPtr<FDMXProtocolDeviceManager> GetDeviceManager() const = 0;
	
	/**
	 * Get the interface manager for a protocol instance
	 * The interface is the way how protocol interacts with Unreal Engine
	 * This could be done with ethernet, serial or fake communication
	 * @return Return the pointer to InterfaceManager
	 */
	virtual TSharedPtr<FDMXProtocolInterfaceManager> GetInterfaceManager() const = 0;

	/**
	 * Get the port manager for a protocol instance
	 * Port managers are responsible for hold the configuration instances of physical ports
	 */
	virtual TSharedPtr<FDMXProtocolPortManager> GetPortManager() const = 0;
	
	/**
	 * Get the universe manager for a protocol instance
	 * Universe managers hold the buffers of DMX for particular universes
	 * @return Return the pointer to UniverseManager
	 */
	virtual TSharedPtr<FDMXProtocolUniverseManager> GetUniverseManager() const = 0;

	/**
	 * Get the protocol settings
	 * @return Return the pointer to Protocol Settings
	 */
	virtual TSharedPtr<FJsonObject> GetSettings() const = 0;

	/**
	 * Reloads the protocol instance
	 */
	virtual void Reload() = 0;

	/**
	 * Whether protocol enabled
	 * @return Return true if the protocol is enabled
	 */
	virtual bool IsEnabled() const = 0;

	/**
	 * Sets the DMX fragment for a particular universe
	 * @param  UniverseID ID of universe to send
	 * @param  DMXFragment Map of DMX channel  and values
	 * @param  bShouldSend whether it should be sent or just store in the universe buffer
	 */
	virtual void SetDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment, bool bShouldSend = true) = 0;

	/**
	 * Send DMX to physical device port
	 * @param  UniverseID ID of universe to send
	 * @param  PortID Physical port ID
	 * @param  DMXBuffer DMX buffer to sent
	 * @return Return true if DMX buffer included in the send queue
	 */
	virtual bool SendDMX(uint16 UniverseID, uint8 PortID, const TSharedPtr<FDMXBuffer>& DMXBuffer) const = 0;
};


