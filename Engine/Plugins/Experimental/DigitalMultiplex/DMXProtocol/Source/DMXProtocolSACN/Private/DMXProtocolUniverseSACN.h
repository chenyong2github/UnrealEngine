// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Interfaces/IDMXNetworkInterface.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "HAL/CriticalSection.h"

class FSocket;

class DMXPROTOCOLSACN_API FDMXProtocolUniverseSACN
	: public IDMXProtocolUniverse
	, public IDMXNetworkInterface
{
public:
	FDMXProtocolUniverseSACN(TSharedPtr<IDMXProtocol> InDMXProtocol, const FJsonObject& InSettings);
	virtual ~FDMXProtocolUniverseSACN();

	//~ Begin IDMXProtocolDevice implementation
	virtual TSharedPtr<IDMXProtocol> GetProtocol() const override;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const override;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const override;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint32 GetUniverseID() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual bool IsSupportRDM() const override;
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
	void OnDataReceived(const FArrayReaderPtr & Buffer);

	bool HandleReplyPacket(const FArrayReaderPtr & Buffer);

private:
	TWeakPtr<IDMXProtocol> WeakDMXProtocol;
	TSharedPtr<FDMXBuffer> OutputDMXBuffer;
	TSharedPtr<FDMXBuffer> InputDMXBuffer;
	uint8 Priority;
	uint32 UniverseID;
	bool bIsRDMSupport;
	TSharedPtr<FJsonObject> Settings;

	mutable FCriticalSection OnDataReceivedCS;

	FSocket* ListeningSocket;
	TSharedPtr<IDMXProtocolReceiver> SACNReceiver;

	FDMXProtocolE131RootLayerPacket IncomingDMXRootLayer;
	FDMXProtocolE131FramingLayerPacket IncomingDMXFramingLayer;
	FDMXProtocolE131DMPLayerPacket IncomingDMXDMPLayer;

	/** Mutex protecting access to the sockets. */
	mutable FCriticalSection ListeningSocketsCS;

	FString InterfaceIPAddress;

	FDelegateHandle NetworkInterfaceChangedHandle;

	const TCHAR* NetworkErrorMessagePrefix;
};