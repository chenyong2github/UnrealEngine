// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HttpConnectionTypes.h"
#include "HttpRouter.h"

struct FHttpConnection;
struct FHttpPath;
class FSocket;

DECLARE_LOG_CATEGORY_EXTERN(LogHttpListener, Log, All);

class FHttpListener final 
{

public:

	/**
	 * Constructor
	 *
	 *  @param ListenPort The port on which to listen for incoming connections
	 */
	FHttpListener(uint32 ListenPort);

	/**
	 * Destructor
	 */
	~FHttpListener();

	/**
	 * Starts listening for and accepting incoming connections
	 *
	 * @return true if the listener was able to start listening, false otherwise
	 */
	bool StartListening();

	/**
	 * Stops listening for and accepting incoming connections
	 */
	void StopListening();

	/**
	 * Tick the listener to otherwise commence connection flows
	 *
	 * @param DeltaTime The elapsed time since the last tick
	 */
	void Tick(float DeltaTime);

	/**
	 * Determines whether this listener has pending connections in-flight
	 *
	 * @return true if there are pending connections, false otherwise
	 */
	bool HasPendingConnections() const;

	/**
	 * Determines whether the listener has been initialized
	 * @return true if this listener is bound and listening, false otherwise
	 */
	FORCEINLINE bool IsListening() const
	{
		return bIsListening;
	}

	/**
	 * Gets the respective router
	 * @return The respective router
	 */
	FORCEINLINE TSharedPtr<IHttpRouter> GetRouter() const
	{
		return Router;
	}

private:

	/**
	 * Accepts a single available connection
	 * 
	 * @param MaxConnectionsToAccept The maximum number of connections to accept
	 */
	void AcceptConnections(uint32 MaxConnectionsToAccept);

	/**
	 * Ticks connections in reading/writing phases
	 * 
	 * @param DeltaTime The elapsed time since the last tick
	 */
	void TickConnections(float DeltaTime);

	/**
	 * Removes connections that have been destroyed
	 */
	void RemoveDestroyedConnections();

private:
	
	/** Whether this listeners has begun listening */
	bool bIsListening = false;

	/** The port on which the binding socket listens */
	uint32 ListenPort = 0;

	/** The binding socket which accepts incoming connections */
	FSocket* ListenSocket = nullptr;

	/** The mechanism that routes requests to respective handlers  */
	TSharedPtr<FHttpRouter> Router = nullptr;

	/** The collection of unique connections */
	FHttpConnectionPool Connections;

	/** The total number of connections accepted by this listener */
	uint32 NumConnectionsAccepted = 0;

	/** Maximum number of connections to accept per frame */
	static constexpr uint32 MaxConnectionsToAcceptPerFrame = 1;
	/** Maximum number of pending connections to queue */
	static constexpr uint32 ListenerConnectionBacklogSize = 16;
	/** Maximum send buffer size */
	static constexpr uint32 ListenerBufferSize = 512 * 1024;
};