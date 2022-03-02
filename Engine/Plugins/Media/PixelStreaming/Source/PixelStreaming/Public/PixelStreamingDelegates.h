// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPlayerId.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateSignatureImpl.inl"
#include "PixelStreamingDelegates.generated.h"


namespace webrtc
{
	class DataChannelInterface;
}

UCLASS()
class PIXELSTREAMING_API UPixelStreamingDelegates : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * A connection to the signalling server was made.
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FConnectedToSignallingServer);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streaming Delegates")
	FConnectedToSignallingServer OnConnectedToSignallingServer;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FConnectedToSignallingServerNative);
	FConnectedToSignallingServerNative OnConnectedToSignallingServerNative;


	/**
	 * A connection to the signalling server was lost.
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDisconnectedFromSignallingServer);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streaming Delegates")
	FDisconnectedFromSignallingServer OnDisconnectedFromSignallingServer;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FDisconnectedFromSignallingServerNative);
	FDisconnectedFromSignallingServerNative OnDisconnectedFromSignallingServerNative;


	/**
	 * A new connection has been made to the session.
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNewConnection, FString, PlayerId, bool, QualityController);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streaming Delegates")
	FNewConnection OnNewConnection;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_TwoParams(FNewConnectionNative, FString, bool);
	FNewConnectionNative OnNewConnectionNative;


	/**
	 * A connection to a player was lost.
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FClosedConnection, FString, PlayerId, bool, WasQualityController);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streaming Delegates")
	FClosedConnection OnClosedConnection;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_TwoParams(FClosedConnectionNative, FPixelStreamingPlayerId, bool);
	FClosedConnectionNative OnClosedConnectionNative;


	/**
	 * All connections have closed and nobody is viewing or interacting with
	 * the app. This is an opportunity to reset the app.
	 */
	// BP Delegate
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAllConnectionsClosed);
	UPROPERTY(BlueprintAssignable, Category = "Pixel Streaming Delegates")
	FAllConnectionsClosed OnAllConnectionsClosed;
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE(FAllConnectionsClosedNative);
	FAllConnectionsClosedNative OnAllConnectionsClosedNative;


	/**
	 * The quality controlling peer has changed
	 */
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_OneParam(FQualityControllerChanged, FPixelStreamingPlayerId)
	FQualityControllerChanged OnQualityControllerChangedNative;


	/**
	 * A new data channel has been opened
	 */
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDataChannelOpen, FPixelStreamingPlayerId, webrtc::DataChannelInterface*)
	FDataChannelOpen OnDataChannelOpenNative;
	
	/**
	 * An existing data channel has been closed
	 */
	 // C++ Delegate
	DECLARE_MULTICAST_DELEGATE_TwoParams(FDataChannelClosed, FPixelStreamingPlayerId, webrtc::DataChannelInterface*)
	FDataChannelClosed OnDataChannelClosedNative;

	/**
	 * A pixel streaming stat has changed
	 */
	// C++ Delegate
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FStatChanged, FPixelStreamingPlayerId, FName, float)
	FStatChanged OnStatChangedNative;

	/**
	 * Create the singleton.
	 */
	static UPixelStreamingDelegates* CreateInstance();

	static UPixelStreamingDelegates* GetPixelStreamingDelegates()
	{
		if (Singleton == nullptr)
		{
			return CreateInstance();
		}
		return Singleton;
	}

private:
	// The singleton object.
	static UPixelStreamingDelegates* Singleton;
};
