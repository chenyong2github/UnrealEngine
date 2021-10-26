// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamerDelegates.generated.h"

UCLASS()
class UPixelStreamerDelegates : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * A connection to the signalling server was made.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConnecedToSignallingServer);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streamer Delegates")
	FOnConnecedToSignallingServer OnConnecedToSignallingServer;

	/**
	 * A connection to the signalling server was lost.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDisconnectedFromSignallingServer);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streamer Delegates")
	FOnDisconnectedFromSignallingServer OnDisconnectedFromSignallingServer;

	/**
	 * A new connection has been made to the session.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNewConnection, FString, PlayerId, bool, QualityController);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streamer Delegates")
	FOnNewConnection OnNewConnection;

	/**
	 * A connection to a player was lost.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClosedConnection, FString, PlayerId, bool, WasQualityController);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streamer Delegates")
	FOnNewConnection OnClosedConnection;

	/**
	 * All connections have closed and nobody is viewing or interacting with
	 * the app. This is an opportunity to reset the app.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllConnectionsClosed);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streamer Delegates")
	FOnAllConnectionsClosed OnAllConnectionsClosed;

	/**
	 * Create the singleton.
	 */
	static UPixelStreamerDelegates* CreateInstance();

	/**
	 * Get the singleton. This allows application-specific blueprints to bind
	 * to delegates of interest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streamer Delegates")
	static UPixelStreamerDelegates* GetPixelStreamerDelegates()
	{
		if(Singleton == nullptr)
		{
			return CreateInstance();
		}
		return Singleton;
	}

private:

	// The singleton object.
	static UPixelStreamerDelegates* Singleton;
};
