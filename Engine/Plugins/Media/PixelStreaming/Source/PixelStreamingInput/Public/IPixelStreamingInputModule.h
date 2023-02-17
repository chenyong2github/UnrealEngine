// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"
#include "PixelStreamingInputProtocol.h"
#include "IPixelStreamingInputHandler.h"

/**
 * The public interface of the Pixel Streaming Input module.
 */
class PIXELSTREAMINGINPUT_API IPixelStreamingInputModule : public IInputDeviceModule
{
public:
	/**
	 * Singleton-like access to this module's interface.
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IPixelStreamingInputModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IPixelStreamingInputModule>("PixelStreamingInput");
	}

	/**
	 * Checks to see if this module is loaded.
	 *
	 * @return True if the module is loaded.
	 */
	static inline bool IsAvailable() { return FModuleManager::Get().IsModuleLoaded("PixelStreamingInput"); }

	/**
	 * Register a new message type that peers can send and pixel streaming can receive
	 * @param MessageDirection The direction the message will travel. eg Streamer->Player or Player->Streamer
	 * @param MessageType The human readable identifier (eg "TouchStarted")
	 * @param Message The object used to define the structure of the message
	 * @param Handler The handler for this message type. This function will be executed whenever the corresponding message type is received
	 */
	virtual void RegisterMessage(EPixelStreamingMessageDirection MessageDirection, const FString& MessageType, FPixelStreamingInputMessage Message, const TFunction<void(FMemoryReader)>& Handler) = 0;

	/**
	 * @brief Find the function to be called whenever the specified message type is received.
	 *
	 * @param MessageType The human readable identifier for the message
	 * @return TFunction<void(FMemoryReader)> The function called when this message type is received.
	 */
	virtual TFunction<void(FMemoryReader)> FindMessageHandler(const FString& MessageType) = 0;

	/**
	 * @brief Get the Input Handler for this module
	 *
	 * @return TSharedPtr<IPixelStreamingInputHandler> the input handler
	 */
	virtual TSharedPtr<IPixelStreamingInputHandler> GetInputHandler() = 0;

	/**
	 * Attempts to create a new input device interface
	 *
	 * @return	Interface to the new input device, if we were able to successfully create one
	 */
	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override = 0;

	DECLARE_MULTICAST_DELEGATE(FOnProtocolUpdated);
	FOnProtocolUpdated OnProtocolUpdated;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSendMessage, FMemoryReader);
	FOnSendMessage OnSendMessage;
};
