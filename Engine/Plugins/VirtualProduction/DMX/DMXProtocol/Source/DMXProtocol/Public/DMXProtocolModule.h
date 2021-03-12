// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"


class IDMXProtocolFactory;

/** Implements the Protocol Module, that enables specific Protocol implementations */
class DMXPROTOCOL_API FDMXProtocolModule 
	: public IModuleInterface
{
public:
	FDMXProtocolModule();

	void RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory);

	void UnregisterProtocol(const FName& FactoryName);

	/** Delegate called when all protocols are registered */
	FSimpleMulticastDelegate OnProtocolsRegisteredDelegate;

	/** Needs to be set to the number of protocol implementations */
	static const int32 NumProtocols;

private:
	/** The number of protocols registered */
	int32 NumRegisteredProtocols;

public:
	/** Get the instance of this module. */
	static FDMXProtocolModule& Get();

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol.
	 */
	virtual IDMXProtocolPtr GetProtocol(const FName InProtocolName = NAME_None);
	
	/**  Get the reference to all protocol factories map */
	const TMap<FName, IDMXProtocolFactory*>& GetProtocolFactories() const;

	/**  Get the reference to all protocols map */
	const TMap<FName, IDMXProtocolPtr>& GetProtocols() const;

public:
	//~ Begin IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface implementation

private:
	/** Called when all protocols are registered */
	void OnProtocolsRegistered();

	void ShutdownDMXProtocol(const FName& ProtocolName);
	void ShutdownAllDMXProtocols();

private:
	TMap<FName, IDMXProtocolFactory*> DMXProtocolFactories;
	TMap<FName, IDMXProtocolPtr> DMXProtocols;
	TMap<FName, bool> DMXProtocolFailureNotes;
};
