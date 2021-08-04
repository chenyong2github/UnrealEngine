// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IMoviePlayerProxyServer;

/**
 * This provides a mechanism to connect the engine (the client) to a movie player (the server).
 * 
 * Engine code can call BlockingStarted/Tick/Finished around blocking areas.
 * 
 * The movie player can call RegisterServer/UnregisterServer
 * so it can receive the calls from the engine.
 */ 
class MOVIEPLAYERPROXY_API FMoviePlayerProxy
{
public:
	/** Call this before doing a blocking operation on the game thread so that the movie player can activate. */
	static void BlockingStarted();
	/** Call this periodically during a blocking operation on the game thread. */
	static void BlockingTick();
	/** Call this once the blocking operation is done to shut down the movie player. */
	static void BlockingFinished();

	/** Call this to hook up a server. */
	static void RegisterServer(IMoviePlayerProxyServer* InServer);
	/** Call this to unregister the current server. */
	static void UnregisterServer();
	
private:
	/** Our current worker that handles blocks. */
	static IMoviePlayerProxyServer* Server;
};

