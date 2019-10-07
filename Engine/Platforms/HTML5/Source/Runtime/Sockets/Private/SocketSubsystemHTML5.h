// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BSDSockets/SocketSubsystemBSD.h"
#include "BSDSockets/SocketsBSD.h"
#include "SocketSubsystemPackage.h"

/**
 * HTML5 sockets, sub-classed from BSD sockets, needed to override a couple of functions
 */
class FHTML5Socket
	: public FSocketBSD
{
public:
	FHTML5Socket(SOCKET InSocket, ESocketType InSocketType, const FString& InSocketDescription, const FName& InProtocolType, ISocketSubsystem * InSubsystem)
		: FSocketBSD(InSocket, InSocketType, InSocketDescription, InProtocolType, InSubsystem)
	{ }

	virtual bool SetNonBlocking(bool bIsNonBlocking) override;
};

/**
 * HTML5 specific socket subsystem implementation
 */
class FSocketSubsystemHTML5 : public FSocketSubsystemBSD
{
protected:

	/** Single instantiation of this subsystem */
	static FSocketSubsystemHTML5* SocketSingleton;

	/** Whether Init() has been called before or not */
	bool bTriedToInit;

public:

	/** 
	 * Singleton interface for this subsystem
	 * @return the only instance of this subsystem
	 */
	static FSocketSubsystemHTML5* Create();

	/**
	 * Performs Android specific socket clean up
	 */
	static void Destroy();

public:

	FSocketSubsystemHTML5()
		: bTriedToInit(false)
	{
	}

	virtual ~FSocketSubsystemHTML5()
	{
	}

	/**
	 * Does Android platform initialization of the sockets library
	 *
	 * @param Error a string that is filled with error information
	 *
	 * @return TRUE if initialized ok, FALSE otherwise
	 */
	virtual bool Init(FString& Error) override;

	/**
	 * Performs platform specific socket clean up
	 */
	virtual void Shutdown() override;

	/**
	 * @return Whether the device has a properly configured network device or not
	 */
	virtual bool HasNetworkDevice() override;

	/**
	 * Create FHTML5Socket as a FSocketBSD object
	 */
	virtual class FSocketBSD* InternalBSDSocketFactory(SOCKET Socket, ESocketType SocketType, const FString& SocketDescription, const FName& SocketProtocol) override;

	/**
	 * Translates an ESocketAddressInfoFlags into a value usable by getaddrinfo
	 */
	virtual int32 GetAddressInfoHintFlag(EAddressInfoFlags InFlags) const override;
};
