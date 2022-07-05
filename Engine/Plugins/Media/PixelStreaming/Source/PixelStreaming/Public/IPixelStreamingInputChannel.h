// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformMisc.h"
#include "IInputDevice.h"
#include "Slate/SceneViewport.h"
#include "PixelStreamingWebRTCIncludes.h"

/**
 * IPixelStreamingInputChannel extends the IInputDevice interface and routes input messages to it's message handler, 
 * whilst handling application specifics in its wrapped application. Setting the target viewport allows for 
 * scaling of input from browser to application, and setting the target window ensure that if windows are tiled (eg editor)
 * that the streamed input only affect the target window.
 */
class IPixelStreamingInputChannel : public IInputDevice
{
public:
    /**
	 * @brief Handle the message from the WebRTC data channel.
	 * @param Buffer The data channel message
	*/
	virtual void OnMessage(const webrtc::DataBuffer& Buffer) = 0;

    /**
	 * @brief Set the viewport this input device is associated with.
	 * @param InTargetViewport The viewport to set
	*/
	virtual void SetTargetViewport(FSceneViewport* InTargetViewport) = 0;

	/**
	 * @brief Get the viewport this input device is associated with.
	 * @return The viewport this input device is associated with
	*/
	virtual FSceneViewport* GetTargetViewport() = 0;

    /**
	 * @brief Set the viewport this input device is associated with.
	 * @param InTargetWindow The viewport to set
	*/
	virtual void SetTargetWindow(TWeakPtr<SWindow> InTargetWindow) = 0;

	/**
	 * @brief Get the viewport this input device is associated with.
	 * @return The viewport this input device is associated with
	*/
	virtual TWeakPtr<SWindow> GetTargetWindow() = 0;

    /**
	 * @brief Set whether the input devices is faking touch events using keyboard and mouse this can be useful for debugging.
	 * @return true
	 * @return false
	*/
	virtual bool IsFakingTouchEvents() const = 0;

    /**
	 * @brief Convinient typedef for a TFunction that Creates an Input device and is passed into the IPixelStreamingStreamer.
	*/
	typedef TFunction<TSharedPtr<IPixelStreamingInputChannel>(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)> FCreateInputChannelFunc;
};