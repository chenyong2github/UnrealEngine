// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSessionChannel.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"


class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;
class FRemoteSessionImageChannel;
class IRemoteSessionImageProvider;
class UTexture2D;

class REMOTESESSION_API IRemoteSessionImageProvider
{
public:
	virtual ~IRemoteSessionImageProvider() = default;
	virtual void Tick(const float InDeltaTime) = 0;
};

/**
 *	A channel that takes an image (created by an IRemoteSessionImageProvider), then sends it to the client (with a FRemoteSessionImageSender).
 *	On the client images are decoded into a double-buffered texture that can be accessed via GetHostScreen.
 */
class REMOTESESSION_API FRemoteSessionImageChannel : public IRemoteSessionChannel, FRunnable
{
public:

	/**
	*	A helper object responsible to take the raw data, encode it to jpg and send it to the client for the RemoteSessionImageChannel
	*/
	class FImageSender
	{
	public:
  
		FImageSender(TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection);
  
		/** Set the jpg compression quality */
		void SetCompressQuality(int32 InQuality);
  
		/** Send an image to the connected clients */
		void SendRawImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData);
  
		/** Send an image to the connected clients */
		void SendRawImageToClients(int32 Width, int32 Height, const void* ImageData, int32 AllocatedImageDataSize);
  
	private:
  
		/** Underlying connection */
		TWeakPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> Connection;
  
		/** Compression quality of the raw image we wish to send to client */
		TAtomic<int32> CompressQuality;
  
		int32 NumSentImages;
	};

public:

	FRemoteSessionImageChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection);

	~FRemoteSessionImageChannel();

	/** Tick this channel */
	virtual void Tick(const float InDeltaTime) override;

	/** Get the client Texture2D to display */
	UTexture2D* GetHostScreen() const;

	/** Set the ImageProvider that will produce the images that will be sent to the client */
	void SetImageProvider(TSharedPtr<IRemoteSessionImageProvider> ImageProvider);

	/** Set the jpg compression quality */
	void SetCompressQuality(int32 InQuality);

	/** Return the image sender connected to the clients */
	TSharedPtr<FRemoteSessionImageChannel::FImageSender, ESPMode::ThreadSafe> GetImageSender() { return ImageSender; }

	/* Begin IRemoteSessionChannel implementation */
	static const TCHAR* StaticType() { return TEXT("FRemoteSessionImageChannel"); }
	virtual const TCHAR* GetType() const override { return StaticType(); }
	/* End IRemoteSessionChannel implementation */

protected:

	/** Underlying connection */
	TWeakPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> Connection;

	/** Our role */
	ERemoteSessionChannelMode Role;

	/** */
	TSharedPtr<IRemoteSessionImageProvider> ImageProvider;

	/** Bound to receive incoming images */
	void ReceiveHostImage(FBackChannelOSCMessage & Message, FBackChannelOSCDispatch & Dispatch);

	/** Creates a texture to receive images into */
	void CreateTexture(const int32 InSlot, const int32 InWidth, const int32 InHeight);
	
	struct FImageData
	{
		FImageData() :
			Width(0)
			, Height(0)
			, ImageIndex(0)
		{
		}
		int32				Width;
		int32				Height;
		TArray<uint8>		ImageData;
		int32				ImageIndex;
	};

	FCriticalSection										IncomingImageMutex;
	TArray<TUniquePtr<FImageData>>							IncomingEncodedImages;

	FCriticalSection										DecodedImageMutex;
	TArray<TUniquePtr<FImageData>>							IncomingDecodedImages;

	UTexture2D*												DecodedTextures[2];
	TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe>			DecodedTextureIndex;

	/** Image sender used by the channel */
	TSharedPtr<FImageSender, ESPMode::ThreadSafe> ImageSender;

	/** So we can manage callback lifetimes properly */
	FDelegateHandle MessageCallbackHandle;

	/* Last processed incoming image */
	int32 LastIncomingImageIndex;

	/* Last processed decode image */
	int32 LastDecodedImageIndex;

	/** Decode the incoming images on a dedicated thread */
	void ProcessIncomingTextures();

protected:
	
	/** Background thread for decoding incoming images */
	uint32 Run();
	void StartBackgroundThread();
	void ExitBackgroundThread();

	FRunnableThread*	BackgroundThread;
	FEvent *			ScreenshotEvent;

	FThreadSafeBool		ExitRequested;
};

class REMOTESESSION_API FRemoteSessionImageChannelFactoryWorker : public IRemoteSessionChannelFactoryWorker
{
public:
	virtual const TCHAR* GetType() const override { return FRemoteSessionImageChannel::StaticType(); }
	virtual TSharedPtr<IRemoteSessionChannel> Construct(ERemoteSessionChannelMode InMode, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) const override;
};

