// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingModule.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Containers/Array.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingBlueprints.generated.h"

UCLASS()
class PIXELSTREAMING_API UPixelStreamingBlueprints : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
     * Send a specified byte array over the WebRTC peer connection data channel. The extension and mime type are used for file reconstruction on the front end 
     * 
     * @param   ByteArray       The raw data that will be sent over the data channel
     * @param   MimeType        The mime type of the file. Used for reconstruction on the front end
     * @param   FileExtension   The file extension. Used for file reconstruction on the front end
     */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Transmit")
	static void SendFileAsByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension);

	/**
     * Send a specified file over the WebRTC peer connection data channel. The extension and mime type are used for file reconstruction on the front end 
     * 
     * @param   FilePath        The path to the file that will be sent
     * @param   MimeType        The mime type of the file. Used for file reconstruction on the front end
     * @param   FileExtension   The file extension. Used for file reconstruction on the front end
     */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Transmit")
	static void SendFile(FString Filepath, FString MimeType, FString FileExtension);

	/**
    * The functions allow Pixel Streaming to be frozen and unfrozen from
    * Blueprint. When frozen, a freeze frame (a still image) will be used by the
    * browser instead of the video stream.
    */

	/**
	 * Freeze Pixel Streaming.
	 * @param   Texture         The freeze frame to display. If null then the back buffer is captured.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Freeze Frame")
	static void FreezeFrame(UTexture2D* Texture);

	/**
	 * Unfreeze Pixel Streaming. 
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Freeze Frame")
	static void UnfreezeFrame();

	/**
	 * Kick a player.
	 * @param   PlayerId         The ID of the player to kick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming")
	static void KickPlayer(FString PlayerId);

	// PixelStreamingDelegates
	/**
	 * Get the singleton. This allows application-specific blueprints to bind
	 * to delegates of interest.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Delegates")
	static UPixelStreamingDelegates* GetPixelStreamingDelegates();
};