// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "FreezeFrame.generated.h"

class IPixelStreamingModule;

/**
 * This singleton object allows Pixel Streaming to be frozen and unfrozen from
 * Blueprint. When frozen, a freeze frame (a still image) will be used by the
 * browser instead of the video stream.
 */
UCLASS()
class UFreezeFrame : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Freeze Pixel Streaming.
	 * @param Texture - The freeze frame to display. If null then the back buffer is captured.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Freeze Frame")
	static void FreezeFrame(UTexture2D* Texture);

	/**
	 * Unfreeze Pixel Streaming. 
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Freeze Frame")
	static void UnfreezeFrame();

	/**
	 * Create the singleton.
	 */
	static void CreateInstance();

private:

	// The singleton object.
	static UFreezeFrame* Singleton;

	// For convenience we keep a reference to the Pixel Streaming plugin.
	IPixelStreamingModule* PixelStreamingModule;
};
