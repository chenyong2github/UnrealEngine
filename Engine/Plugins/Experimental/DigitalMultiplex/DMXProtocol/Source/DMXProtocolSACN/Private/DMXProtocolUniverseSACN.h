// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

class FSocket;

class DMXPROTOCOLSACN_API FDMXProtocolUniverseSACN
	: public IDMXProtocolUniverse
{
public:
	FDMXProtocolUniverseSACN(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolPort> InPort, uint16 InUniverseID);
	virtual ~FDMXProtocolUniverseSACN();

	//~ Begin IDMXProtocolDevice implementation
	virtual IDMXProtocol* GetProtocol() const override;
	virtual TWeakPtr<IDMXProtocolPort> GetCachedUniversePort() const override;
	virtual TSharedPtr<FDMXBuffer> GetOutputDMXBuffer() const override;
	virtual TSharedPtr<FDMXBuffer> GetInputDMXBuffer() const override;
	virtual bool SetDMXFragment(const IDMXFragmentMap& DMXFragment) override;
	virtual uint8 GetPriority() const override;
	virtual uint16 GetUniverseID() const override;
	//~ End IDMXProtocolDevice implementation

	//~ SACN Getters functions
	const FDMXProtocolE131RootLayerPacket& GetIncomingDMXRootLayer() const { return IncomingDMXRootLayer; }
	const FDMXProtocolE131FramingLayerPacket& GetIncomingDMXFramingLayer() const { return IncomingDMXFramingLayer; }
	const FDMXProtocolE131DMPLayerPacket& GetIncomingDMXDMPLayer() const { return IncomingDMXDMPLayer; }

private:
	void OnDataReceived(const FArrayReaderPtr & Buffer);

	bool HandleReplyPacket(const FArrayReaderPtr & Buffer);

private:
	IDMXProtocol* DMXProtocol;
	TWeakPtr<IDMXProtocolPort> Port;
	TSharedPtr<FDMXBuffer> OutputDMXBuffer;
	TSharedPtr<FDMXBuffer> InputDMXBuffer;
	uint8 Priority;
	uint16 UniverseID;

	FSocket* ListenerSocket;
	TSharedPtr<IDMXProtocolReceiver> SACNReceiver;

	FDMXProtocolE131RootLayerPacket IncomingDMXRootLayer;
	FDMXProtocolE131FramingLayerPacket IncomingDMXFramingLayer;
	FDMXProtocolE131DMPLayerPacket IncomingDMXDMPLayer;
};