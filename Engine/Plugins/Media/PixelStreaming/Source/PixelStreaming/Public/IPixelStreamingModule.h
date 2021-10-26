// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"
#include "Templates/SharedPointer.h"
#include "PlayerId.h"
#include "IPixelStreamingAudioSink.h"

class UTexture2D;
class UPixelStreamerInputComponent;

/**
* The public interface to this module
*/
class IPixelStreamingModule : public IInputDeviceModule
{
public:

	/**
	* Singleton-like access to this module's interface.  This is just for convenience!
	* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	*
	* @return Returns singleton instance, loading the module on demand if needed
	*/
	static inline IPixelStreamingModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IPixelStreamingModule>("PixelStreaming");
	}

	/**
	* Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	*
	* @return True if the module is loaded and ready to use
	*/
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("PixelStreaming");
	}

	/**
	 * Returns a reference to the input device. The lifetime of this reference
	 * is that of the underlying shared pointer.
	 * @return A reference to the input device.
	 */
	virtual class FInputDevice& GetInputDevice() = 0;
	
	/**
	 * Add any player config JSON to the given object which relates to
	 * configuring the input system for the pixel streaming on the browser.
	 * @param JsonObject - The JSON object to add fields to.
	 */
	virtual void AddPlayerConfig(TSharedRef<class FJsonObject>& JsonObject) = 0;

	/**
	 * Send a data response back to the browser where we are sending video. This
	 * could be used as a response to a UI interaction, for example.
	 * @param Descriptor - A generic descriptor string.
	 */
	virtual void SendResponse(const FString& Descriptor) = 0;

	/**
	 * Send a data command back to the browser where we are sending video. This
	 * is different to a response as a command is low-level and coming from UE4
	 * rather than the pixel streamed application.
	 * @param Descriptor - A generic descriptor string.
	 */
	virtual void SendCommand(const FString& Descriptor) = 0;

	/**
	 * Freeze Pixel Streaming.
	 * @param Texture - The freeze frame to display. If null then the back buffer is captured.
	 */
	virtual void FreezeFrame(UTexture2D* Texture) = 0;

	/**
	 * Unfreeze Pixel Streaming.
	 */
	virtual void UnfreezeFrame() = 0;
	
	/**
	 * Get the audio sink associated with a specific peer/player.
	 */
	virtual IPixelStreamingAudioSink* GetPeerAudioSink(FPlayerId PlayerId) = 0;

	/**
	 * Get an audio sink that has no peers/players listening to it.
	 */
	virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() = 0;

	/**
	 * Tell the input device about a new pixel streaming input component.
	 * @param InInputComponent - The new pixel streaming input component.
	 */
	virtual void AddInputComponent(UPixelStreamerInputComponent* InInputComponent) = 0;

	/*
	 * Tell the input device that a pixel streaming input component is no longer
	 * relevant.
	 * @param InInputComponent - The pixel streaming input component which is no longer relevant.
	 */
	virtual void RemoveInputComponent(UPixelStreamerInputComponent* InInputComponent) = 0;

	/*
	 * Get the input components currently attached to Pixel Streaming.
	 */
	virtual const TArray<UPixelStreamerInputComponent*> GetInputComponents() = 0;

};

