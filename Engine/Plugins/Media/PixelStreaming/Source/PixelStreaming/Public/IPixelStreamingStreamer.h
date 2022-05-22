// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingProtocolDefs.h"
#include "PixelStreamingPlayerId.h"
#include "PixelStreamingVideoInput.h"
#include "CoreMinimal.h"
#include "Slate/SceneViewport.h"
#include "IPixelStreamingInputDevice.h"
#include "IPixelStreamingAudioSink.h"
#include "IInputDevice.h"
#include "IInputDeviceModule.h"

class UTexture2D;

class PIXELSTREAMING_API IPixelStreamingStreamer : public IInputDeviceModule
{
public:
	virtual ~IPixelStreamingStreamer() = default;

	/**
	 * @brief Set the Stream FPS
	 * @param InFramesPerSecond The number of frames per second the streamer will stream at
	 */
	virtual void SetStreamFPS(int32 InFramesPerSecond) = 0;

	/**
	 * @brief Set the Video Input object
	 * @param Input The FPixelStreamingVideoInput that this streamer will stream
	 */
	virtual void SetVideoInput(TSharedPtr<FPixelStreamingVideoInput> Input) = 0;

	/**
	 * @brief Set the Target Viewport for this streamer. This is used to ensure input between the browser and application scales correctly
	 * @param InTargetViewport The target viewport
	 */
	virtual void SetTargetViewport(TSharedPtr<FSceneViewport> InTargetViewport) = 0;

	/**
	 * @brief Set the Signalling Server URL
	 * @param InSignallingServerURL 
	 */
	virtual void SetSignallingServerURL(const FString& InSignallingServerURL) = 0;

	/**
	 * @brief Start streaming this streamer
	 */
	virtual void StartStreaming() = 0;

	/**
	 * @brief Stop this streamer from streaming
	 */
	virtual void StopStreaming() = 0;

	/**
	 * @brief Get the current state of this streamer
	 * @return true 
	 * @return false 
	 */
	virtual bool IsStreaming() const = 0;

	/**
	 * @brief Send all players connected to this streamer a message
	 * @param Type The message type to be sent to the player
	 * @param Descriptor The contents of the message
	 */
	virtual void SendPlayerMessage(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) = 0;

	/**
	 * @brief Set the streamers input device
	 * @param InInputDevice 
	 */
	virtual void SetInputDevice(TSharedPtr<IPixelStreamingInputDevice> InInputDevice) = 0;

	/**
	 * @brief Freeze Pixel Streaming.
	 * @param Texture 		- The freeze frame to display. If null then the back buffer is captured.
	 */
	virtual void FreezeStream(UTexture2D* Texture) = 0;

	/**
	 * @brief Unfreeze Pixel Streaming.
	 */
	virtual void UnfreezeStream() = 0;

	/**
	 * @brief Send a file to the browser where we are sending video.
	 * @param ByteData	 	- The raw byte data of the file.
	 * @param MimeType	 	- The files Mime type. Used for reconstruction on the front end.
	 * @param FileExtension - The files extension. Used for reconstruction on the front end.
	 */
	virtual void SendFileData(TArray<uint8>& ByteData, FString& MimeType, FString& FileExtension) = 0;

	/**
	 * @brief Kick a player by player id.
	 * @param PlayerId		- The ID of the player to kick
	 */
	virtual void KickPlayer(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * @brief Get the audio sink associated with a specific peer/player.
	 */
	virtual IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * @brief Get an audio sink that has no peers/players listening to it.
	 */
	virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() = 0;

	/**
	 * @brief Event fired when the streamer has connected to a signalling server and is ready for peers.
	 */
	DECLARE_EVENT_OneParam(IPixelStreamingStreamer, FStreamingStartedEvent, IPixelStreamingStreamer*);

	/**
	 * @brief A getter for the OnStreamingStarted event. Intent is for users to call IPixelStreamingModule::Get().FindStreamer(ID)->OnStreamingStarted().AddXXX.
	 * @return - The bindable OnStreamingStarted event.
	 */
	virtual FStreamingStartedEvent& OnStreamingStarted() = 0;

	/**
	 * @brief Event fired when the streamer has disconnected from a signalling server and has stopped streaming.
	 */
	DECLARE_EVENT_OneParam(IPixelStreamingStreamer, FStreamingStoppedEvent, IPixelStreamingStreamer*);

	/**
	 * @brief A getter for the OnStreamingStopped event. Intent is for users to call IPixelStreamingModule::Get().FindStreamer(ID)->OnStreamingStopped().AddXXX.
	 * @return - The bindable OnStreamingStopped event.
	 */
	virtual FStreamingStoppedEvent& OnStreamingStopped() = 0;

	/**
	 * Register a lambda that returns a IInputDevice
	 * @param InCreateInputeDevice - A lambda that will return input device
	*/
	virtual void RegisterCreateInputDevice(IPixelStreamingInputDevice::FCreateInputDeviceFunc& InCreateInputDevice) = 0;
};
