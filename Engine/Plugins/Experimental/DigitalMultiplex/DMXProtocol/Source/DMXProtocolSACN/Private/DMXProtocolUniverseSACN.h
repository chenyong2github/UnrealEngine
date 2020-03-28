// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"
#include "Packets/DMXProtocolE131PDUPacket.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Interfaces/IDMXNetworkInterface.h"

#include "Tickable.h"

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
	FDMXProtocolUniverseSACN(IDMXProtocolPtr InDMXProtocol, const FJsonObject& InSettings);
	virtual ~FDMXProtocolUniverseSACN();

	//~ Begin IDMXProtocolDevice implementation
	virtual IDMXProtocolPtr GetProtocol() const override;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const override;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const override;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint32 GetUniverseID() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual bool IsSupportRDM() const override;

	virtual void Tick(float DeltaTime) override;
	//~ End IDMXProtocolDevice implementation

	//~ Begin IDMXNetworkInterface implementation
	virtual void OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress) override;
	virtual bool RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage) override;
	virtual void ReleaseNetworkInterface() override;
	//~ End IDMXNetworkInterface implementation

	//~ SACN Getters functions
	const FDMXProtocolE131RootLayerPacket& GetIncomingDMXRootLayer() const { return IncomingDMXRootLayer; }
	const FDMXProtocolE131FramingLayerPacket& GetIncomingDMXFramingLayer() const { return IncomingDMXFramingLayer; }
	const FDMXProtocolE131DMPLayerPacket& GetIncomingDMXDMPLayer() const { return IncomingDMXDMPLayer; }

private:
	/** Called when new DMX packet is coming */
	bool OnDataReceived(const FArrayReaderPtr& Buffer);

	/** Handle and broadcasting incoming DMX packet */
	bool HandleReplyPacket(const FArrayReaderPtr& Buffer);

	/** Ask to read DMX from the Socket */
	bool ReceiveDMXBuffer();

	/** Parse incoming DMX packet into the SACN layers */
	void SetLayerPackets(const FArrayReaderPtr& Buffer);

	/** Checks if the socket exists, otherwise create a new one */
	FSocket* GetOrCreateListeningSocket();

private:
	IDMXProtocolPtrWeak WeakDMXProtocol;
	TSharedPtr<FDMXBuffer> OutputDMXBuffer;
	TSharedPtr<FDMXBuffer> InputDMXBuffer;
	uint8 Priority;
	uint32 UniverseID;
	bool bIsRDMSupport;
	TSharedPtr<FJsonObject> Settings;

	/** The universe listening socket */
	FSocket* ListeningSocket;

	/** The universe socket listening Addr */
	TSharedPtr<FInternetAddr> ListenerInternetAddr;

	/**
	 * Current number of ticks without DMX input buffer request.
	 * It the amount as same as MaxNumTicks the socket will be destroyed, which will save the resources.
	 * If (NumTicksWithoutInputBufferRequest == 0) there is no socket reading
	 * If (NumTicksWithoutInputBufferRequest > 0 && NumTicksWithoutInputBufferRequest < MaxNumTicksWithoutInputBufferRequest)
	 *   the socket is reading each tick, if it new read request the new socket will be created.
	 * If (NumTicksWithoutInputBufferRequest == MaxNumTicksWithoutInputBufferRequest) there is no socket will be destoyed
	 */
	mutable double TimeWithoutInputBufferRequestStart;

	/** Max number of ticks without DMX input buffer request. The socket will be destroyed after this point */
	mutable double TimeWithoutInputBufferRequestEnd;

	mutable bool bIsTicking;

	static const double TimeWithoutInputBufferRequest;


	/** Pointer to the socket sub-system. */
	ISocketSubsystem* SocketSubsystem;

	FDMXProtocolE131RootLayerPacket IncomingDMXRootLayer;
	FDMXProtocolE131FramingLayerPacket IncomingDMXFramingLayer;
	FDMXProtocolE131DMPLayerPacket IncomingDMXDMPLayer;

	FString InterfaceIPAddress;

	FDelegateHandle NetworkInterfaceChangedHandle;

	const TCHAR* NetworkErrorMessagePrefix;
};