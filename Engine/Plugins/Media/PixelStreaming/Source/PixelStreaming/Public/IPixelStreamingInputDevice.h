// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformMisc.h"
#include "IInputDevice.h"
#include "Slate/SceneViewport.h"

// WEBRTC API
#include "api/data_channel_interface.h"
//


/* 
 * Use this interface to override the way input is processed from the WebRTC datachannels.
 * This can be done by passing a FCreateInputDeviceFunc that returns your implementation into
 * IPixelStreamingStreamer::RegisterCreateInputDevice.
 * 
 * See /PixelStreaming/Private/InputDevice.h for the default implimentation of an IPixelStreamingInputDevice.
*/ 
class IPixelStreamingInputDevice : public IInputDevice
{
public:
	/*
	 * Handle the message from the WebRTC data channel.
	*/
	virtual void OnMessage(const webrtc::DataBuffer& Buffer) = 0;
	
	/*
	 * Set the viewport this input device is associated with.
	*/
	virtual void SetTargetViewport(TSharedPtr<FSceneViewport> InTargetViewport) = 0;
	
	/*
	 * Set whether the input devices is faking touch events using keyboard and mouse
	 * this can be useful for debugging.
	*/
	virtual bool IsFakingTouchEvents() const = 0;

	/*
	 * Convinient typedef for a TFunction that Creates an Input device and is passed into the IPixelStreamingStreamer.
	*/
	typedef TFunction<TSharedPtr<IPixelStreamingInputDevice>(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)> FCreateInputDeviceFunc;
};
