// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXNetworkInterface.h"
#include "DMXProtocolSACNModule.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"

class FInternetAddr;
class FSocket;
class FDMXProtocolUniverseSACN;

template<class TUniverse>
class FDMXProtocolUniverseManager;

using FDMXProtocolUniverseSACNPtr = TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>;

class DMXPROTOCOLSACN_API FDMXProtocolSACN
	: public IDMXProtocol
	, public IDMXNetworkInterface
{
public:
	explicit FDMXProtocolSACN(const FName& InProtocolName, FJsonObject& InSettings);

	//~ Begin IDMXProtocolBase implementation
	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual bool Tick(float DeltaTime) override;
	//~ End IDMXProtocolBase implementation

	//~ Begin IDMXProtocol implementation
	virtual const FName& GetProtocolName() const override;
	virtual TSharedPtr<IDMXProtocolSender> GetSenderInterface() const override;
	virtual TSharedPtr<FJsonObject> GetSettings() const override;
	virtual bool IsEnabled() const override;
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> AddUniverse(const FJsonObject& InSettings) override;
	virtual void CollectUniverses(const TArray<FDMXUniverse>& Universes) override;
	virtual bool RemoveUniverseById(uint32 InUniverseId) override;
	virtual void RemoveAllUniverses() override;
	virtual TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> GetUniverseById(uint32 InUniverseId) const override;
	virtual EDMXSendResult SendDMXFragment(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment) override;
	virtual EDMXSendResult SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment) override;
	virtual uint16 GetFinalSendUniverseID(uint16 InUniverseID) const override;
	virtual uint32 GetUniversesNum() const override;
	virtual uint16 GetMinUniverseID() const override;
	virtual uint16 GetMaxUniverses() const override;
	virtual FOnUniverseInputUpdateEvent& GetOnUniverseInputUpdate() override
	{
		return OnUniverseInputUpdateEvent;
	}
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

	//~ sACN specific implementation
	bool SendDiscovery(const TArray<uint16>& Universes);

	//~ Only the factory makes instances
	FDMXProtocolSACN() = delete;

public:
	static TSharedPtr<FInternetAddr> GetUniverseAddr(uint16 InUniverseID);

private:
	EDMXSendResult SendDMXInternal(uint16 UniverseID, const TSharedPtr<FDMXBuffer>& DMXBuffer) const;

	/** Called each tick in LaunchEngineLoop */
	void OnEndFrame();

private:
	FName ProtocolName;

	TSharedPtr<FJsonObject> Settings;

	TSharedPtr<FDMXProtocolUniverseManager<FDMXProtocolUniverseSACN>> UniverseManager;

	TSharedPtr<IDMXProtocolSender> SACNSender;

	/** Holds the network socket used to sender packages. */
	FSocket* SenderSocket;

	FOnUniverseInputUpdateEvent OnUniverseInputUpdateEvent;

	/** Mutex protecting access to the listening socket. */
	mutable FCriticalSection SenderSocketCS;

	FString InterfaceIPAddress;

	FOnNetworkInterfaceChangedDelegate NetworkInterfaceChangedDelegate;

	const TCHAR* NetworkErrorMessagePrefix;

	/** Called at the end of a frame, Allow to tick universes */
	FDelegateHandle OnEndFrameHandle;
};
