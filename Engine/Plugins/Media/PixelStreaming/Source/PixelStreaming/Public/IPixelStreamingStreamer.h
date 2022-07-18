// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingProtocolDefs.h"
#include "PixelStreamingPlayerId.h"
#include "IPixelStreamingVideoInput.h"
#include "CoreMinimal.h"
#include "Slate/SceneViewport.h"
#include "IPixelStreamingAudioSink.h"
#include "IPixelStreamingInputChannel.h"

class UTexture2D;

class PIXELSTREAMING_API IPixelStreamingStreamer
{
public:
	virtual ~IPixelStreamingStreamer() = default;

	/**
	 * @brief Set the Stream FPS
	 * @param InFramesPerSecond The number of frames per second the streamer will stream at
	 */
	virtual void SetStreamFPS(int32 InFramesPerSecond) = 0;

	/**
	 * @brief Get the Stream FPS
	 * @return - The number of frames per second the streamer will stream at
	 */
	virtual int32 GetStreamFPS() = 0;

	/**
	 * Setting this to true will cause the streamer to ignore the FPS value and instead push out frames
	 * as they are submitted from the video input. If the encode process takes longer than the time
	 * between frames, frames will be dropped.
	 * @param bCouple true to couple the streamer framerate to that of the video input.
	 */
	virtual void SetCoupleFramerate(bool bCouple) = 0;

	/**
	 * @brief Set the Video Input object
	 * @param Input The FPixelStreamingVideoInput that this streamer will stream
	 */
	virtual void SetVideoInput(TSharedPtr<IPixelStreamingVideoInput> Input) = 0;

	/**
	 * @brief Get the Video Input object
	 * @return The FPixelStreamingVideoInput that this streamer will stream
	 */
	virtual TWeakPtr<IPixelStreamingVideoInput> GetVideoInput() = 0;

	/**
	 * @brief Set the Target Viewport for this streamer. This is used to ensure input between the browser and application scales correctly
	 * @param InTargetViewport The target viewport
	 */
	virtual void SetTargetViewport(FSceneViewport* InTargetViewport) = 0;

	/**
	 * @brief Get the Target Viewport for this streamer
	 * @return The target viewport
	 */
	virtual FSceneViewport* GetTargetViewport() = 0;

	/**
	 * @brief Set the target window for this streamer. This is used to ensure mouse events are directed to the correct window if multiple
	 * windows are tiled on top of one another as seen in the editor
	 * @param InTargetWindow The target window
	 */
	virtual void SetTargetWindow(TSharedPtr<SWindow> InTargetWindow) = 0;

	/**
	 * @brief Get the target window for this streamer
	 * @return The target window
	 */
	virtual TWeakPtr<SWindow> GetTargetWindow() = 0;

	/**
	 * @brief Set the Signalling Server URL
	 * @param InSignallingServerURL
	 */
	virtual void SetSignallingServerURL(const FString& InSignallingServerURL) = 0;

	/**
	 * @brief Get the Signalling Server URL
	 * @return The Signalling Server URL
	 */
	virtual FString GetSignallingServerURL() = 0;

	/**
	 * @return True if the streamer is connected to the signalling mechanism (e.g. websocket for signalling server).
	 */
	virtual bool IsSignallingConnected() = 0;

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
	 * Broadcast when data comes in from the data channel.
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnInputReceived, FPixelStreamingPlayerId, uint8, TArray<uint8>);
	FOnInputReceived OnInputReceived;

	/**
	 * @brief A getter for the OnStreamingStopped event. Intent is for users to call IPixelStreamingModule::Get().FindStreamer(ID)->OnStreamingStopped().AddXXX.
	 * @return - The bindable OnStreamingStopped event.
	 */
	virtual FStreamingStoppedEvent& OnStreamingStopped() = 0;

	/**
	 * @brief Force a key frame to be sent.
	 */
	virtual void ForceKeyFrame() = 0;

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
	 * @brief Send all players connected to this streamer a message
	 * @param Type The message type to be sent to the player
	 * @param Descriptor The contents of the message
	 */
	virtual void SendPlayerMessage(UE::PixelStreaming::Protocol::EToPlayerMsg Type, const FString& Descriptor) = 0;

	/**
	 * @brief Send a file to the browser where we are sending video.
	 * @param ByteData	 	- The raw byte data of the file.
	 * @param MimeType	 	- The files Mime type. Used for reconstruction on the front end.
	 * @param FileExtension - The files extension. Used for reconstruction on the front end.
	 */
	virtual void SendFileData(const TArray64<uint8>& ByteData, FString& MimeType, FString& FileExtension) = 0;

	/**
	 * @brief Kick a player by player id.
	 * @param PlayerId		- The ID of the player to kick
	 */
	virtual void KickPlayer(FPixelStreamingPlayerId PlayerId) = 0;

	//virtual bool IsQualityController(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * @brief Set the streamer's input device
	 * @param InInputChannel
	 */
	virtual void SetInputChannel(TSharedPtr<IPixelStreamingInputChannel> InInputChannel) = 0;

	/**
	 * @brief Get the streamer's input device
	 * @return The streamer's input device
	 */
	virtual TWeakPtr<IPixelStreamingInputChannel> GetInputChannel() = 0;

	/**
	 * @brief Get the audio sink associated with a specific peer/player.
	 */
	virtual IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) = 0;

	/**
	 * @brief Get an audio sink that has no peers/players listening to it.
	 */
	virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() = 0;
};
