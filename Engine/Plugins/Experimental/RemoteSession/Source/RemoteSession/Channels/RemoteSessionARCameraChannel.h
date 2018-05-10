// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteSessionChannel.h"
#include "UObject/GCObject.h"

class FBackChannelOSCMessage;
class FBackChannelOSCDispatch;
class FAppleImageUtilsConversionTaskBase;
class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class ISceneViewExtension;

class REMOTESESSION_API FRemoteSessionARCameraChannel :
	public IRemoteSessionChannel,
	public FGCObject
{
public:

	FRemoteSessionARCameraChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection);

	~FRemoteSessionARCameraChannel();

	virtual void Tick(const float InDeltaTime) override;

	/* Begin IRemoteSessionChannel implementation */
	static FString StaticType() { return TEXT("rs.arcamera"); }
	virtual FString GetType() const override { return StaticType(); }
	/* End IRemoteSessionChannel implementation */

	/** Returns the post process material to render with. The textures are set on the fly */
	UMaterialInterface* GetPostProcessMaterial() const;

protected:
	/** Sends the most recent compressed AR camera image to the remote */
	void SendARCameraImage();
	/** Compresses in the background and queues for sending */
	void QueueARCameraImage();

	/** Handles data coming from the client */
	void ReceiveARCameraImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch);

	/** Updates the next texture to be rendered */
	void UpdateRenderingTexture();

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ FGCObject

private:
	/** A wrapper around the async compression task */
	struct FCompressionTask
	{
		FCompressionTask() : Width(0), Height(0) {}

		int32 Width;
		int32 Height;
		TSharedPtr<FAppleImageUtilsConversionTaskBase, ESPMode::ThreadSafe> AsyncTask;
	};

	struct FDeompressedImage
	{
		FDeompressedImage() : Width(0), Height(0) {}

		int32 Width;
		int32 Height;
		TArray<uint8> ImageData;
	};

	/** The timestamp of the last queued image to prevent from sending the same image repeatedly */
	float LastQueuedTimestamp;
	/** List of async image compression tasks in flight */
	TArray<TSharedPtr<FCompressionTask, ESPMode::ThreadSafe>> CompressionQueue;

	/** Images that have been received and are available for rendering */
	TArray<TSharedPtr<FDeompressedImage, ESPMode::ThreadSafe>> DecompressionQueue;
	FThreadSafeCounter DecompressionTaskCount;
	FCriticalSection DecompressionQueueLock;

	/** The post process material that the overlay will render with */
	UMaterialInterface* PPMaterial;
	/** To set the current texture for rendering */
	UMaterialInstanceDynamic* MaterialInstanceDynamic;
	/** Textures to use when rendering */
	UTexture2D* RenderingTextures[2];
	FThreadSafeCounter RenderingTextureIndex;
	FThreadSafeCounter RenderingTexturesUpdateCount[2];

	TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> SceneViewExtension;

	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> Connection;
	ERemoteSessionChannelMode Role;

	/** So we can manage callback lifetimes properly */
	FDelegateHandle MessageCallbackHandle;
};
