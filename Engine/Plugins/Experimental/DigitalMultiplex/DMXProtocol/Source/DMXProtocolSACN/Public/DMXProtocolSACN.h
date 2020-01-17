// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "DMXProtocol.h"
#include "DMXProtocolSACNModule.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

class FInternetAddr;
class FSocket;

class DMXPROTOCOLSACN_API FDMXProtocolSACN
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

	//~ sACN specific implementation
	bool SendDiscovery(const TArray<uint16>& Universes);

	//~ Only the factory makes instances
	FDMXProtocolSACN() = delete;
	explicit FDMXProtocolSACN(FName InProtocolName, FJsonObject& InSettings)
		: FDMXProtocol(FDMXProtocolSACNModule::NAME_SACN, InSettings)
		, SenderSocket(nullptr)
	{}

public:
	static TSharedPtr<FInternetAddr> GetUniverseAddr(uint16 InUniverseID);

private:
	TSharedPtr<IDMXProtocolSender> SACNSender;

	/** Holds the network socket used to sender packages. */
	FSocket* SenderSocket;
};
