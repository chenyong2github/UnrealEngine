// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "api/video/video_frame_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/scoped_refptr.h"
#include "Templates/SharedPointer.h"
#include "PixelStreamingTextureWrapper.h"
#include "Delegates/Delegate.h"

//---------------------------------IPixelStreamingFrameCapturer--------------------------------------------------------

class PIXELSTREAMING_API IPixelStreamingFrameCapturer
{
public:
	virtual ~IPixelStreamingFrameCapturer() = default;
	virtual void CaptureTexture(FPixelStreamingTextureWrapper& TextureToCopy, TSharedPtr<FPixelStreamingTextureWrapper> DestinationTexture) = 0;
	virtual void OnCaptureFinished(TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture) = 0;
	virtual bool IsCaptureFinished() = 0;
};

//--------------------------------FPixelStreamingTextureSourceBase---------------------------------------------------------

/*
* Base class for all texture sources in Pixel Streaming.
* These texture sources are used to populate video sources for video tracks.
* At minimum a texture source must do the following:
* 1) Create destination textures to capture to.
* 2) Create a capturer object that does that capturing.
*/
class PIXELSTREAMING_API FPixelStreamingTextureSource
{
public:
	FPixelStreamingTextureSource() = default;
	virtual ~FPixelStreamingTextureSource() = default;

	/**
	* Create a blank staging texture that is used when the source texture is captured.
	* @param Width
	* @param Height
	* @return The blank "texture" - which does not have to be a UE texture.
	*/
	virtual TSharedPtr<FPixelStreamingTextureWrapper> CreateBlankStagingTexture(uint32 Width, uint32 Height) = 0;

	/**
	 * Creates the object that does the actual capturing of frames to textures.
	 * @return The capturer.
	*/
	virtual TSharedPtr<IPixelStreamingFrameCapturer> CreateFrameCapturer() = 0;

	/**
	 *  Convert a "texture" to a WebRTC YUV I420 buffer.
	 * @param Texture - The "texture" to convert.
	 * @return The WebRTC I420 buffer.
	 */
	virtual rtc::scoped_refptr<webrtc::I420Buffer> ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture) = 0;

	/* 
	* Parameters for the delegate are as follows: FOnNewTexture(FPixelStreamingTextureWrapper& NewFrame, uint32 FrameWidth, uint32 FrameHeight) 
	* The intent is for you to use whatever mechanism you want to broadcast new textures to through this delegate as they become available.
	*/
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnNewTexture, FPixelStreamingTextureWrapper&, uint32, uint32);
	FOnNewTexture OnNewTexture;
};
