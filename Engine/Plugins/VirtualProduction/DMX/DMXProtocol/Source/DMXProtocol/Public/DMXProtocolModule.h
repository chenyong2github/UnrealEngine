// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "ModuleDescriptor.h"
#include "Modules/ModuleInterface.h"


class IDMXProtocolFactory;

struct FDMXProtocolRegistrationParams
{
	/** The name to use for the protocol (also used in UIs) */
	FName ProtocolName;

	/** The factory used to create the protocol */
	IDMXProtocolFactory* ProtocolFactory = nullptr;
};


DECLARE_EVENT_OneParam(FDMXProtocolModule, FDMXOnRequestProtocolRegistrationDelegate, TArray<FDMXProtocolRegistrationParams>& /** InOutProtocolRegistrationParamsArray */);
DECLARE_EVENT_OneParam(FDMXProtocolModule, FDMXOnRequestProtocolBlacklistDelegate, TArray<FName>& /** InOutBlacklistedProtocols */);

/** Implements the Protocol Module, that enables specific Protocol implementations */
class DMXPROTOCOL_API FDMXProtocolModule 
	: public IModuleInterface
{

public:
	/** Delegate for Protocol implementations to register themself with the Protocol Module. Broadcast at the end of the PreDefault loading phase. See DMXProtocolArtNet for an example. */
	static FDMXOnRequestProtocolRegistrationDelegate& GetOnRequestProtocolRegistration()
	{
		static FDMXOnRequestProtocolRegistrationDelegate OnRequestProtocolRegistration;
		return OnRequestProtocolRegistration;
	};

	/** Delegate for other Plugins that want to disable a specific protocol */
	static FDMXOnRequestProtocolBlacklistDelegate& GetOnRequestProtocolBlacklistDelegate()
	{
		static FDMXOnRequestProtocolBlacklistDelegate OnRequestProtocolBlacklistDelegate;
		return OnRequestProtocolBlacklistDelegate;
	};

	UE_DEPRECATED(4.27, "Use the OnRequestProtocolRegistration delegate please.")
	void RegisterProtocol(const FName& ProtocolName, IDMXProtocolFactory* Factory);

	void UnregisterProtocol(const FName& ProtocolName);

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

	//~ Begin IModuleInterface implementation
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface implementation

private:
	/** Called after each loading phase during startup */
	void OnPluginLoadingPhaseComplete(ELoadingPhase::Type LoadingPhase, bool bPhaseSuccessful);

	void ShutdownDMXProtocol(const FName& ProtocolName);
	void ShutdownAllDMXProtocols();

private:
	static const FName DefaultProtocolArtNetName;
	static const FName DefaultProtocolSACNName;

	TMap<FName, IDMXProtocolFactory*> DMXProtocolFactories;
	TMap<FName, IDMXProtocolPtr> DMXProtocols;
};
