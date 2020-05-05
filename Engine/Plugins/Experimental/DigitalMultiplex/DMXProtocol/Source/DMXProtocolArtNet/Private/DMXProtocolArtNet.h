// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXNetworkInterface.h"
#include "DMXProtocolArtNetModule.h"
#include "Packets/DMXProtocolArtNetPackets.h"

#include "HAL/ThreadSafeCounter.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/CriticalSection.h"

class FSocket;
class FDMXProtocolUniverseArtNet;

class DMXPROTOCOLARTNET_API FDMXProtocolArtNet
	: public IDMXProtocol
	, public IDMXNetworkInterface
{
public:
	explicit FDMXProtocolArtNet(const FName& InProtocolName, const FJsonObject& InSettings);

	//~ Begin IDMXProtocolBase implementation
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual bool Tick(float DeltaTime) override;
	//~ End IDMXProtocolBase implementation

	//~ Begin IDMXProtocol implementation
	virtual const FName& GetProtocolName() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const override;
	virtual EDMXSendResult SendDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment) override;
	virtual EDMXSendResult SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment) override;
	virtual uint16 GetFinalSendUniverseID(uint16 InUniverseID) const override;
	virtual bool IsEnabled() const override;
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> AddUniverse(const FJsonObject& InSettings) override;
	virtual void CollectUniverses(const TArray<FDMXUniverse>& Universes) override;
	virtual bool RemoveUniverseById(uint32 InUniverseId) override;
	virtual void RemoveAllUniverses() override;
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> GetUniverseById(uint32 InUniverseId) const override;
	virtual uint32 GetUniversesNum() const override;
	virtual uint16 GetMinUniverseID() const override;
	virtual uint16 GetMaxUniverses() const override;
	virtual FOnUniverseInputUpdateEvent& GetOnUniverseInputUpdate() override { return OnUniverseInputUpdateEvent; }
	//~ End IDMXProtocol implementation

	//~ Begin IDMXNetworkInterface implementation
	virtual void OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress) override;
	virtual bool RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage) override;
	virtual void ReleaseNetworkInterface() override;
	//~ End IDMXNetworkInterface implementation

	//~ Begin IDMXProtocolRDM implementation
	virtual void SendRDMCommand(const TSharedPtr<FJsonObject>& CMD) override;
	virtual void RDMDiscovery(const TSharedPtr<FJsonObject>& CMD) override;
	//~ End IDMXProtocol implementation

	//~ ArtNet Transmit functions
	bool TransmitPoll();
	bool TransmitTodRequestToAll();
	bool TransmitTodRequest(uint8 PortAddress);
	void WriteTodRequestAddress(FDMXProtocolArtNetTodRequest& TodRequest, uint8 PortAddress);

	//~ ArtNet Getters functions
	const FDMXProtocolArtNetPollPacket& GetIncomingPoll() const { return IncomingPollPacket; }
	const FDMXProtocolArtNetPacketReply& GetIncomingPacketReply() const { return PacketReply; }
	const FDMXProtocolArtNetTodRequest& GetIncomingTodRequest() const { return IncomingTodRequest; }
	const FDMXProtocolArtNetTodData& GetIncomingTodData() const { return IncomingTodData; }
	const FDMXProtocolArtNetTodControl& GetIncomingTodControl() const { return IncomingTodControl; }
	const FDMXProtocolArtNetRDM& GetIncomingRDM() const { return IncomingRDM; }

	/**
	 * Transmit TOD data
	 * @param InUniverseID The universe which we want to transmit
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitTodData(uint32 InUniverseID);

	/**
	 * Transmit TOD cocontrol package
	 * @param InUniverseID The universe which we want to transmit
	 * @param Action tod control action
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitTodControl(uint32 InUniverseID, uint8 Action);

	/**
	 * Send a RDM message
	 * @param InUniverseID The universe which we want to transmit
	 * @param Data Buffer array to send
	 * @return true if the call was successfully placed for sending queue.
	 */
	bool TransmitRDM(uint32 InUniverseID, const TArray<uint8>& Data);

private:
	EDMXSendResult SendDMXInternal(uint16 InUniverseID, uint8 InPort, const FDMXBufferPtr& DMXBuffer) const;

	//~ Only the factory makes instances
	FDMXProtocolArtNet() = delete;

public:
	const TSharedPtr<FInternetAddr>& GetBroadcastAddr() { return BroadcastAddr; }
	void OnDataReceived(const FArrayReaderPtr & Buffer);

private:
	//~ DMX Handlers
	bool HandlePool(const FArrayReaderPtr& Buffer);
	bool HandleReplyPacket(const FArrayReaderPtr & Buffer);
	bool HandleDataPacket(const FArrayReaderPtr & Buffer);

	//~ RDM Handlers
	bool HandleTodRequest(const FArrayReaderPtr & Buffer);
	bool HandleTodData(const FArrayReaderPtr & Buffer);
	bool HandleTodControl(const FArrayReaderPtr & Buffer);
	bool HandleRdm(const FArrayReaderPtr & Buffer);

private:
	FName ProtocolName;
	TSharedPtr<FJsonObject> Settings;

	TSharedPtr<FDMXProtocolUniverseManager<FDMXProtocolUniverseArtNet>> UniverseManager;

	TSharedPtr<IDMXProtocolSender> ArtNetSender;
	TSharedPtr<IDMXProtocolReceiver> ArtNetReceiver;

	/** Holds the network socket used to transport packages. */
	FSocket* BroadcastSocket;

	FSocket* ListeningSocket;

	TSharedPtr<FInternetAddr> BroadcastAddr;
	FIPv4Endpoint BroadcastEndpoint;

	TSharedPtr<FInternetAddr> SenderAddr;
	FIPv4Endpoint SenderEndpoint;

	FDMXProtocolArtNetPollPacket IncomingPollPacket;
	FDMXProtocolArtNetPacketReply PacketReply;
	FDMXProtocolArtNetTodRequest IncomingTodRequest;
	FDMXProtocolArtNetTodData IncomingTodData;
	FDMXProtocolArtNetTodControl IncomingTodControl;
	FDMXProtocolArtNetRDM IncomingRDM;

	FOnUniverseInputUpdateEvent OnUniverseInputUpdateEvent;

	/** Mutex protecting access to the sockets. */
	mutable FCriticalSection SocketsCS;

	FString InterfaceIPAddress;

	FDelegateHandle NetworkInterfaceChangedHandle;

	const TCHAR* NetworkErrorMessagePrefix;
};

