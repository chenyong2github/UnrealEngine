// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Modules/ModuleInterface.h"

/**
 * 
 */
class DMXPROTOCOL_API FDMXProtocolModule 
	: public IModuleInterface
{
public:
	void RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory);

	void UnregisterProtocol(const FName& FactoryName);

	/**
	 * If protocol exists return the pointer otherwise it create a new protocol first and then return the pointer.
	 * @param  InProtocolName Name of the requested protocol
	 * @return Return the pointer to protocol.
	 */
	virtual IDMXProtocol* GetProtocol(const FName InProtocolName = NAME_None);
	
	const TMap<FName, IDMXProtocolFactory*>& GetProtocolFactories() const;

	//~ Begin IModuleInterface implementation
	virtual void ShutdownModule() override;
	//~ End IModuleInterface implementation

	/** Get the instance of this module. */
	static FDMXProtocolModule& Get();

private:
	void ShutdownDMXProtocol(const FName& ProtocolName);
	void ShutdownAllDMXProtocols();

public:
	static const TCHAR* BaseModuleName;

private:
	TMap<FName, IDMXProtocolFactory*> DMXFactories;
	TMap<FName, TSharedPtr<IDMXProtocol>> DMXProtocols;
	TMap<FName, bool> DMXProtocolFailureNotes;
};
