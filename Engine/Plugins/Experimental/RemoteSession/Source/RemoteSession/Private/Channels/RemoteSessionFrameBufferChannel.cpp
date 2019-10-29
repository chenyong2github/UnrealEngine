// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "ImageProviders/RemoteSessionFrameBufferImageProvider.h"

#include "Misc/ConfigCacheIni.h"
#include "RemoteSessionUtils.h"

TSharedPtr<IRemoteSessionChannel> FRemoteSessionFrameBufferChannelFactoryWorker::Construct(ERemoteSessionChannelMode InMode, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) const
{
	TSharedPtr<FRemoteSessionImageChannel> Channel = MakeShared<FRemoteSessionImageChannel>(InMode, InConnection);

	if (InMode == ERemoteSessionChannelMode::Write)
	{
		TSharedPtr<FRemoteSessionFrameBufferImageProvider> ImageProvider = MakeShared<FRemoteSessionFrameBufferImageProvider>(Channel->GetImageSender());

		{
			int32 Quality = 85;
			int32 Framerate = 30;
			GConfig->GetInt(TEXT("RemoteSession"), TEXT("Quality"), Quality, GEngineIni);
			GConfig->GetInt(TEXT("RemoteSession"), TEXT("Framerate"), Framerate, GEngineIni);

			ImageProvider->SetCaptureFrameRate(Framerate);
			Channel->SetCompressQuality(Quality);
		}

		{
			TWeakPtr<SWindow> InputWindow;
			TWeakPtr<FSceneViewport> SceneViewport;
			FRemoteSessionUtils::FindSceneViewport(InputWindow, SceneViewport);

			if (TSharedPtr<FSceneViewport> SceneViewPortPinned = SceneViewport.Pin())
			{
				ImageProvider->SetCaptureViewport(SceneViewPortPinned.ToSharedRef());
			}
		}

		Channel->SetImageProvider(ImageProvider);
	}
	return Channel;
}
