// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolSACNModule.h"
#include "Interfaces/IDMXProtocol.h"

#include "CoreMinimal.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"

class FInternetAddr;
class FSocket;
class FDMXProtocolUniverseSACN;
class FDMXProtocolSACNReceivingRunnable;

template<class TUniverse>
class FDMXProtocolUniverseManager;

using FDMXProtocolUniverseSACNPtr = TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>;

class DMXPROTOCOLSACN_API FDMXProtocolSACN
	: public IDMXProtocol
{
public:
	explicit FDMXProtocolSACN(const FName& InProtocolName);

public:
	//~ Begin IDMXProtocolBase implementation
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual bool Tick(float DeltaTime) override;
	//~ End IDMXProtocolBase implementation

public:
	// ~Begin IDMXProtocol implementation
	virtual bool IsEnabled() const override;
	virtual const FName& GetProtocolName() const override;
	virtual const TArray<EDMXCommunicationType> GetInputPortCommunicationTypes() const override;
	virtual const TArray<EDMXCommunicationType> GetOutputPortCommunicationTypes() const override;
	virtual int32 GetMinUniverseID() const override;
	virtual int32 GetMaxUniverseID() const override;
	virtual bool IsValidUniverseID(int32 UniverseID) const override;
	virtual int32 MakeValidUniverseID(int32 DesiredUniverseID) const override;
	virtual bool RegisterInputPort(const TSharedRef<FDMXInputPort, ESPMode::ThreadSafe>& InputPort) override;
	virtual void UnregisterInputPort(const TSharedRef<FDMXInputPort, ESPMode::ThreadSafe>& InputPort) override;
	virtual TSharedPtr<IDMXSender> RegisterOutputPort(const TSharedRef<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort) override;
	virtual bool IsCausingLoopback(EDMXCommunicationType InCommunicationType) override;
	virtual void UnregisterOutputPort(const TSharedRef<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort) override;
	// ~End IDMXProtocol implementation

private:
	/** The supported input port communication types */
	static const TArray<EDMXCommunicationType> InputPortCommunicationTypes;

	/** The supported output port communication types */
	static const TArray<EDMXCommunicationType> OutputPortCommunicationTypes;

	/** Set of all DMX input ports currently in use */
	TSet<TSharedPtr<FDMXInputPort, ESPMode::ThreadSafe>> CachedInputPorts;

private:
	FName ProtocolName;
};
