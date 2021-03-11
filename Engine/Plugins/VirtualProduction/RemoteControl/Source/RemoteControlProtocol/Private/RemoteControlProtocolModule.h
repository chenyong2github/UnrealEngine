// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlProtocolModule.h"


class FRemoteControlProtocolModule : public IRemoteControlProtocolModule
{
public:
	//~ Begin IModuleInterface interface
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface
	
	//~ Begin IRemoteControlProtocolModule interface
	virtual int32 GetProtocolNum() const override { return Protocols.Num(); }
	virtual TArray<FName> GetProtocolNames() const override;
	virtual TSharedPtr<IRemoteControlProtocol> GetProtocolByName(FName InProtocolName) const override;
	virtual void AddProtocol(FName InProtocolName, TSharedRef<IRemoteControlProtocol> InProtocol) override;
	virtual void RemoveProtocol(FName InProtocolName, TSharedRef<IRemoteControlProtocol> InProtocol) override;
	virtual void EmptyProtocols() override;
	//~ End IRemoteControlProtocolModule interface

private:
	/** Map with all protocols */
	TMap<FName, TSharedRef<IRemoteControlProtocol>> Protocols;
};
