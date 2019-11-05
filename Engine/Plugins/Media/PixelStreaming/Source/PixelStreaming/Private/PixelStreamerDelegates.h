// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamerDelegates.generated.h"

UCLASS()
class UPixelStreamerDelegates : public UObject
{
	GENERATED_BODY()

public:

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
	static void CreateInstance();

	/**
	 * Get the singleton. This allows application-specific blueprints to bind
	 * to delegates of interest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streamer Delegates")
	static UPixelStreamerDelegates* GetPixelStreamerDelegates()
	{
		return Singleton;
	}

private:

	// The singleton object.
	static UPixelStreamerDelegates* Singleton;
};
