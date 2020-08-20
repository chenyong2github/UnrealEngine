// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebSocketNetDriver.h"
#include "UObject/StrongObjectPtr.h"
#include "INetworkingWebSocket.h"
#include "IWebSocketServer.h"
#include "RemoteControlRoute.h"

/**
 * Router used to dispatch messages received by a WebSocketServer.
 */
class FWebsocketMessageRouter
{
public:
	/**
	 * Add a handler to the Router's dispatch table.
	 * @param MessageName the name of the message to handle.
	 * @param FWebSocketMessageDelegate the handler to called upon receiving a message with the right name.
	 */
	void BindRoute(const FString& MessageName, FWebSocketMessageDelegate OnMessageReceived);

	/**
	 * Remove a route from the dispatch table.
	 * @param MessageName the name of the message handler to remove.
	 */
	void UnbindRoute(const FString& MessageName);

private:
	/**
	 * Invoke the handler bound to a message name.
	 * @param MessageName the name of the message to dispatch, used to find its handler.
	 * @param TCHARMessage the payload to dispatch.
	 */
	void Dispatch(const struct FWebSocketMessage& Message);

private:
	/** The dispatch table used to keep track of message handlers. */
	TMap<FString, FWebSocketMessageDelegate> DispatchTable;

	friend class FRCWebSocketServer;
};

/**
 * WebSocket server that allows handling and sending WebSocket messages.
 */
class FRCWebSocketServer
{
public:

	FRCWebSocketServer() = default;
	~FRCWebSocketServer();

	/**
	 * Start listening for WebSocket messages.
	 * @param Port the port to listen on.
	 * @param InRouter the router used to dispatch received messages.
	 * @return whether the server was successfully started. 
	 */
	bool Start(uint32 Port, TSharedPtr<FWebsocketMessageRouter> InRouter);
	
	/**
	 * Stop listening for WebSocket messages.
	 */
	void Stop();

	/**
	 * Send a message to all clients currently connected to the server.
	 * @param Message the message to broadcast to connected client
	 */
	void Broadcast(const FString& Message);

	/** Returns whether the server is currently listening for messages. */
	bool IsRunning() const;

private:
	bool Tick(float DeltaTime);

	/** Handles a new client connecting. */
	void OnWebSocketClientConnected(INetworkingWebSocket* Socket);

	/** Handles sending the received packet to the message router. */
	void ReceivedRawPacket(void* Data, int32 Size);

	void OnSocketClose(INetworkingWebSocket* Socket);

private:
	/** Holds a web socket connection to a client. */
	class FWebSocketConnection
	{
	public:

		explicit FWebSocketConnection(INetworkingWebSocket* InSocket)
			: Socket(InSocket)
		{
		}

		FWebSocketConnection(FWebSocketConnection&& WebSocketConnection)
		{
			Socket = WebSocketConnection.Socket;
			WebSocketConnection.Socket = nullptr;
		}

		~FWebSocketConnection()
		{
			if (Socket)
			{
				delete Socket;
				Socket = nullptr;
			}
		}

		FWebSocketConnection(const FWebSocketConnection&) = delete;
		FWebSocketConnection& operator=(const FWebSocketConnection&) = delete;
		FWebSocketConnection& operator=(FWebSocketConnection&&) = delete;

		/** Underlying WebSocket. */
		INetworkingWebSocket* Socket = nullptr;
	};

private:
	/** Handle to the tick delegate. */
	FDelegateHandle TickerHandle;
 
	/** Holds the LibWebSocket wrapper. */
	TUniquePtr<IWebSocketServer> Server;

	/** Holds all active connections. */
	TArray<FWebSocketConnection> Connections;

	/** Holds the router responsible for dispatching messages received by this server. */
	TSharedPtr<FWebsocketMessageRouter> Router;
};
