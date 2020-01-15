// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "DMXProtocol.h"
#include "DMXProtocolArtNetModule.h"
#include "Packets/DMXProtocolArtNetPackets.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

class FSocket;

class DMXPROTOCOLARTNET_API FDMXProtocolArtNet
	: public FDMXProtocol
{
public:
	//~ Begin IDMXProtocolBase implementation
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual bool Tick(float DeltaTime) override;
	//~ End IDMXProtocolBase implementation

	//~ Begin IDMXProtocol implementation
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const override;
	virtual FName GetProtocolName() const override;
	virtual void Reload() override;
	virtual bool SendDMX(uint16 UniverseID, uint8 PortID, const TSharedPtr<FDMXBuffer>& DMXBuffer) const override;
	virtual bool IsEnabled() const override;
	//~ End IDMXProtocol implementation

	//~ Begin IDMXProtocolRDM implementation
	virtual void SendRDMCommand(const TSharedPtr<FJsonObject>& CMD) override;
	virtual void RDMDiscovery(const TSharedPtr<FJsonObject>& CMD) override;
	//~ End IDMXProtocol implementation

	//~ ArtNet Transmit functions
	bool TransmitPoll();
	bool TransmitTodRequestToAll();
	bool TransmitTodRequest(const TSharedPtr<IDMXProtocolDevice>& InDevice);

	//~ ArtNet Getters functions
	const FDMXProtocolArtNetTodRequest& GetIncomingTodRequest() const { return IncomingTodRequest; }
	const FDMXProtocolArtNetTodData& GetIncomingTodData() const { return IncomingTodData; }
	const FDMXProtocolArtNetTodControl& GetIncomingTodControl() const { return IncomingTodControl; }
	const FDMXProtocolArtNetRDM& GetIncomingRDM() const { return IncomingRDM; }

	/**
	 * Transmit TOD data
	 * @param InDevice Pointer for the physical device
	 * @param InPortID The number of the port to send data for
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitTodData(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID);

	/**
	 * Transmit TOD cocontrol package
	 * @param InDevice Pointer for the physical device
	 * @param InPortID The number of the port to send data for
	 * @param Action tod control action
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitTodControl(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, uint8 Action);

	/**
	 * Send a RDM message
	 * @param InDevice Pointer for the physical device
	 * @param InPortID The number of the port to send data for
	 * @param Data Buffer array to send
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitRDM(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, const TArray<uint8>& Data);

	//~ Only the factory makes instances
	FDMXProtocolArtNet() = delete;
	explicit FDMXProtocolArtNet(FName InProtocolName, FJsonObject& InSettings)
		: FDMXProtocol(FDMXProtocolArtNetModule::NAME_Artnet, InSettings)
	{}

public:
	const TSharedPtr<FInternetAddr>& GetBroadcastAddr() { return BroadcastAddr; }
	void OnDataReceived(const FArrayReaderPtr & Buffer);

private:
	//~ DMX Handlers
	bool HandleReplyPacket(const FArrayReaderPtr & Buffer);
	bool HandleDataPacket(const FArrayReaderPtr & Buffer);

	//~ RDM Handlers
	bool HandleTodRequest(const FArrayReaderPtr & Buffer);
	bool HandleTodData(const FArrayReaderPtr & Buffer);
	bool HandleTodControl(const FArrayReaderPtr & Buffer);
	bool HandleRdm(const FArrayReaderPtr & Buffer);

private:
	TSharedPtr<IDMXProtocolSender> ArtNetSender;
	TSharedPtr<IDMXProtocolReceiver> ArtNetReceiver;

	/** Holds the network socket used to transport packages. */
	FSocket* BroadcastSocket;

	TSharedPtr<FInternetAddr> BroadcastAddr;
	FIPv4Endpoint BroadcastEndpoint;

	TSharedPtr<FInternetAddr> SenderAddr;
	FIPv4Endpoint SenderEndpoint;

	FDMXProtocolArtNetTodRequest IncomingTodRequest;
	FDMXProtocolArtNetTodData IncomingTodData;
	FDMXProtocolArtNetTodControl IncomingTodControl;
	FDMXProtocolArtNetRDM IncomingRDM;
};

