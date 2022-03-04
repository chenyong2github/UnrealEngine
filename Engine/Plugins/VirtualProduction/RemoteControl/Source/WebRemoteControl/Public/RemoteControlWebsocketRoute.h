// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FRemoteControlWebSocketMessage
{
	FString MessageName;
	int32 MessageId = -1;
	FGuid ClientId;
	TArrayView<uint8> RequestPayload;
	TMap<FString, TArray<FString>> Header;
};

DECLARE_DELEGATE_OneParam(FWebSocketMessageDelegate, const FRemoteControlWebSocketMessage& /** Message */);

struct FRemoteControlWebsocketRoute
{
	FRemoteControlWebsocketRoute(const FString& InRouteDescription, const FString& InMessageName, const FWebSocketMessageDelegate& InDelegate)
		: RouteDescription(InRouteDescription)
		, MessageName(InMessageName)
		, Delegate(InDelegate)
	{}

	/** A description of how the route should be used. */
	FString RouteDescription;
	/**  The message handled by this route. */
	FString MessageName;
	/** The handler called when the route is accessed. */
	FWebSocketMessageDelegate Delegate;

	friend uint32 GetTypeHash(const FRemoteControlWebsocketRoute& Route) { return GetTypeHash(Route.MessageName); }
	friend bool operator==(const FRemoteControlWebsocketRoute& LHS, const FRemoteControlWebsocketRoute& RHS) { return LHS.MessageName == RHS.MessageName; }
};
