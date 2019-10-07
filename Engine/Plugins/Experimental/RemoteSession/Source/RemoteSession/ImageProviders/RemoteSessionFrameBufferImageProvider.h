// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Channels/RemoteSessionImageChannel.h"


class FFrameGrabber;
class FSceneViewport;

/**
 *	Use the FrameGrabber on the host to provide an image to the image channel.
 */
class REMOTESESSION_API FRemoteSessionFrameBufferImageProvider : public IRemoteSessionImageProvider
{
public:

	FRemoteSessionFrameBufferImageProvider(TWeakPtr<FRemoteSessionImageChannel> Owner);
	~FRemoteSessionFrameBufferImageProvider();

	/** Specifies which viewport to capture */
	void SetCaptureViewport(TSharedRef<FSceneViewport> Viewport);

	/** Specifies the framerate at */
	void SetCaptureFrameRate(int32 InFramerate);

	/** Tick this channel */
	virtual void Tick(const float InDeltaTime) override;

	/** Signals that the viewport was resized */
	void OnViewportResized(FVector2D NewSize);

	/** Safely create the frame grabber */
	void CreateFrameGrabber(TSharedRef<FSceneViewport> Viewport);

protected:

	/** Release the FrameGrabber*/
	void ReleaseFrameGrabber();

	TWeakPtr<FRemoteSessionImageChannel> ImageChannel;

	TSharedPtr<FFrameGrabber> FrameGrabber;

	FThreadSafeCounter NumDecodingTasks;

	/** Time we last sent an image */
	double LastSentImageTime;

	/** Shows that the viewport was just resized */
	bool ViewportResized;

	/** Holds a reference to the scene viewport */
	TSharedPtr<FSceneViewport> SceneViewport;
};
