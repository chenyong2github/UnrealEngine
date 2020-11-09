// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"
#include "DMXProtocolSACN.h"
#include "Packets/DMXProtocolE131PDUPacket.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Interfaces/IDMXNetworkInterface.h"

class FDMXProtocolSACN;

class FInternetAddr;
class FSocket;
class ISocketSubsystem;

/**
 * SACN universe implementation
 * It holds the input and output buffer
 */
class DMXPROTOCOLSACN_API FDMXProtocolUniverseSACN
	: public IDMXProtocolUniverse
	, public IDMXNetworkInterface
{
public:
	FDMXProtocolUniverseSACN(TWeakPtr<FDMXProtocolSACN, ESPMode::ThreadSafe> DMXProtocolSACN, const FJsonObject& InSettings);
	virtual ~FDMXProtocolUniverseSACN();

	//~ Begin IDMXProtocolUniverse implementation
	virtual IDMXProtocolPtr GetProtocol() const override;
	virtual FDMXBufferPtr GetInputDMXBuffer() const override;
	virtual FDMXBufferPtr GetOutputDMXBuffer() const override;
	virtual void ZeroInputDMXBuffer();
	virtual void ZeroOutputDMXBuffer();
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint32 GetUniverseID() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual void UpdateSettings(const FJsonObject& InSettings) override;
	virtual bool IsSupportRDM() const override;

	virtual void HandleReplyPacket(const FArrayReaderPtr& Buffer) override;
	//~ End IDMXProtocolUniverse implementation

	//~ Begin IDMXNetworkInterface implementation
	virtual void OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress) override;
	virtual bool RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage) override;
	virtual void ReleaseNetworkInterface() override;
	virtual void Tick(float DeltaTime) override;
	//~ End IDMXNetworkInterface implementation

	/**
	 * Creates a listener for DMX packets, uses current network interface
	 * @param OutErrorMessage				String error message
	 */
	void CreateDMXListener();

	/**
	 * Creates a listener for DMX packets, uses current network interface
	 * @param OutErrorMessage				String error message
	 */
	void DestroyDMXListener();

	//~ SACN Getters functions
	const FDMXProtocolE131RootLayerPacket& GetIncomingDMXRootLayer() const { return IncomingDMXRootLayer; }
	const FDMXProtocolE131FramingLayerPacket& GetIncomingDMXFramingLayer() const { return IncomingDMXFramingLayer; }
	const FDMXProtocolE131DMPLayerPacket& GetIncomingDMXDMPLayer() const { return IncomingDMXDMPLayer; }

	// Returns current IP Addresses broadcast or unicast
	TArray<uint32> GetIpAddresses() const { return IpAddresses; }

	// Returns SACN Protocol Ethernet port
	uint16 GetPort() const { return EthernetPort; }

	/** Receiving for the universe incoming data updates */
 	void ReceiveIncomingData();

private:

	/** Parse incoming DMX packet into the SACN layers */
	void SetLayerPackets(const FArrayReaderPtr& Buffer);

	/** Called when new DMX packet is coming */
	void OnDataReceived(const FArrayReaderPtr& Buffer);

	FSocket* GetOrCreateListeningSocket();

private:
	TWeakPtr<FDMXProtocolSACN, ESPMode::ThreadSafe> WeakDMXProtocol;
	FDMXBufferPtr OutputDMXBuffer;
	FDMXBufferPtr InputDMXBuffer;
	uint8 Priority;
	uint32 UniverseID;
	bool bIsRDMSupport;
	TSharedPtr<FJsonObject> Settings;

	/** Defines if DMX should be received */
	bool bShouldReceiveDMX;

	/** 
	 * The highest priority of a packet received in this universe. Packets with lower priority will be ignored.  
	 * This is to support certain hard- and software such as the ETC series, that send 'alive' signals at a lower prio (see UE-100002).
	 */
	uint8 HighestReceivedPriority;

	/** The universe listening socket */
	FSocket* ListeningSocket;

	/** The universe socket listening Addr */
	TSharedPtr<FInternetAddr> ListenerInternetAddr;

	/** Pointer to the socket sub-system. */
	ISocketSubsystem* SocketSubsystem;

	FDMXProtocolE131RootLayerPacket IncomingDMXRootLayer;
	FDMXProtocolE131FramingLayerPacket IncomingDMXFramingLayer;
	FDMXProtocolE131DMPLayerPacket IncomingDMXDMPLayer;

	FString InterfaceIPAddress;

	FDelegateHandle NetworkInterfaceChangedHandle;

	const TCHAR* NetworkErrorMessagePrefix;

	/** Universe IP Addresses */
	TArray<uint32> IpAddresses;

	/** Universe Ethernet Port*/
	uint16 EthernetPort;
};
