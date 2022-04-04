// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"
#include "Templates/SharedPointer.h"
#include "Templates/RefCounting.h"
#include "PixelStreamingPlayerId.h"
#include "IPixelStreamingAudioSink.h"
#include "PixelStreamingPumpable.h"
#include "IPixelStreamingTextureSourceFactory.h"
#include "IInputDevice.h"
#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

class UTexture2D;
class UPixelStreamingInput;
class FRHIGPUFence;

/**
 * The public interface of the Pixel Streaming module.
 */
class PIXELSTREAMING_API IPixelStreamingModule : public IInputDeviceModule
{
public:
	/**
	 * Singleton-like access to this module's interface.
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IPixelStreamingModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IPixelStreamingModule>("PixelStreaming");
	}

	/**
	 * Checks to see if this module is loaded.
	 *
	 * @return True if the module is loaded.
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("PixelStreaming");
	}

	/*
	 * Event fired when internal streamer is initialized and the methods on this module are ready for use.
	 */
	DECLARE_EVENT_OneParam(IPixelStreamingModule, FReadyEvent, IPixelStreamingModule&);

	/**
	 * A getter for the OnReady event. Intent is for users to call IPixelStreamingModule::Get().OnReady().AddXXX.
	 * @return The bindable OnReady event.
	 */
	virtual FReadyEvent& OnReady() = 0;

	/**
	 * Is the PixelStreaming module actually ready to use? Is the streamer created.
	 * @return True if Pixel Streaming module methods are ready for use.
	 */
	virtual bool IsReady() = 0;

	/**
	 * Connect to the specified signalling server and begin a streaming session
	 * @param SignallingServerUrl The url of the signalling server to use. eg. ws://localhost:8888
	 */
	virtual bool StartStreaming(const FString& SignallingServerUrl) = 0;

	/*
	 * Ends the current streaming session
	 */
	virtual void StopStreaming() = 0;

	/*
	 * Event fired when the streamer has connected to a signalling server and is ready for peers.
	 */
	DECLARE_EVENT_OneParam(IPixelStreamingModule, FStreamingStartedEvent, IPixelStreamingModule&);

	/*
	 * A getter for the OnStreamingStarted event. Intent is for users to call IPixelStreamingModule::Get().OnStreamingStarted().AddXXX.
	 * @return The bindable OnStreamingStarted event.
	 */
	virtual FStreamingStartedEvent& OnStreamingStarted() = 0;

	/*
	 * Event fired when the streamer has disconnected from a signalling server and has stopped streaming.
	 */
	DECLARE_EVENT_OneParam(IPixelStreamingModule, FStreamingStoppedEvent, IPixelStreamingModule&);

	/*
	 * A getter for the OnStreamingStopped event. Intent is for users to call IPixelStreamingModule::Get().OnStreamingStopped().AddXXX.
	 * @return The bindable OnStreamingStopped event.
	 */
	virtual FStreamingStoppedEvent& OnStreamingStopped() = 0;

	/**
	 * Returns a reference to the input device. The lifetime of this reference
	 * is that of the underlying shared pointer.
	 * @return A reference to the input device.
	 */
	virtual IInputDevice& GetInputDevice() = 0;

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
	 * Send a file to the browser where we are sending video.
	 * @param FilePath - The freeze frame to display. If null then the back buffer is captured.
	 */
	virtual void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension) = 0;

	/**
	 * Kick a player by player id.
	 * @param PlayerId - The ID of the player to kick
	 */
	virtual void KickPlayer(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * Get the audio sink associated with a specific peer/player.
	 */
	virtual IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * Get an audio sink that has no peers/players listening to it.
	 */
	virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() = 0;

	/**
	 * Tell the input device about a new pixel streaming input component.
	 * @param InInputComponent - The new pixel streaming input component.
	 */
	virtual void AddInputComponent(UPixelStreamingInput* InInputComponent) = 0;

	/**
	 * Tell the input device that a pixel streaming input component is no longer
	 * relevant.
	 * @param InInputComponent - The pixel streaming input component which is no longer relevant.
	 */
	virtual void RemoveInputComponent(UPixelStreamingInput* InInputComponent) = 0;

	/**
	 * Get the input components currently attached to Pixel Streaming.
	 * @return An array of input components.
	 */
	virtual const TArray<UPixelStreamingInput*> GetInputComponents() = 0;

	/**
	 * Create a webrtc::VideoEncoderFactory pointer.
	 * @return A WebRTC video encoder factory with its encoders populated by Pixel Streaming.
	 */
	virtual webrtc::VideoEncoderFactory* CreateVideoEncoderFactory() = 0;

	/**
	 * Register a "Pumpable" with the Pixel Streaming module and it will be pumped at the WebRTC max FPS (usually 60).
	 * @param Pumpable - A thing that will be called at a fixed interval (typically 60fps).
	 */
	virtual void RegisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable) = 0;

	/**
	 * Unregister a "Pumpable" from the Pixel Streaming module.
	 * @param Pumpable - The pumpable you no longer wish to pump.
	 */
	virtual void UnregisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable) = 0;

	/**
	 * Create a task that is continuously polled until it is done and can be short circuited.
	 * This is useful for in Pixel Streaming for checking fences during texture copies.
	 * @param Task - The task to execute one time, this will happen on the "Poller" thread.
	 * @param IsTaskFinished - A check that will be called as often as possible until the task is finished.
	 * @param bKeepRunning - A flag to short circuit the completion checking (useful if we no longer care because it took too long).
	 */
	virtual void AddPollerTask(TFunction<void()> Task, TFunction<bool()> IsTaskFinished, TSharedRef<bool, ESPMode::ThreadSafe> bKeepRunning) = 0;

	/**
	 * Create a WebRTC video source that is has its lifetime managed outside of Pixel Streaming.
	 * @return A ref-counted WebRTC video source, when WebRTC is done with it the ref count will decrement by one, if that is the only remaining ref the source will be destroyed.
	 */
	virtual rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> CreateExternalVideoSource(FName SourceType = FName(TEXT("Backbuffer"))) = 0;

	/**
	 * The Pixel Streaming module can have n active texture source types that dictate
	 * the type of video tracks that are made when a new peer joins.
	 * @param SourceTypes - An array of registered source types to make tracks from, e.g. "Backbuffer".
	 */
	virtual void SetActiveTextureSourceTypes(const TArray<FName>& SourceTypes) = 0;

	/**
	 * The n active texture source types that dictate
	 * how many video tracks of each source type are made when a new peer joins.
	 * @return The array of active registered source types that tracks are made from, e.g. "Backbuffer".
	 */
	virtual const TArray<FName>& GetActiveTextureSourceTypes() const = 0;

	/**
	 * Get the texture source factory which you can use to register new texture source types and create
	 * texture sources
	 * @return The texture source factory in use by the Pixel Streaming module
	 */
	virtual IPixelStreamingTextureSourceFactory& GetTextureSourceFactory() = 0;
};
