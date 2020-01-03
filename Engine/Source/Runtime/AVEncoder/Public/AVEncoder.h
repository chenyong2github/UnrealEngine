// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SampleBuffer.h"
#include "RHI.h"

namespace AVEncoder
{

struct FEncoderVideoFrameCookie
{
	virtual ~FEncoderVideoFrameCookie() {}
};

struct FVideoEncoderConfig
{
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 Framerate = 0;
	uint32 MaxBitrate = 0;
	uint32 Bitrate = 0;

	enum class EPreset
	{
		LowLatency,
		HighQuality
	};

	/**
	* Provides an hint to the encoder of what's the intended use.
	*/
	EPreset Preset = EPreset::HighQuality;

	/** 
	* Options specific to an hardware vendor (e.g: AMF or NVidia), or specific to a codec (h264)
	* At the moment there are the following options (name and value)
	*
	* "qp" :
	*	H264 Quantization Parameter (0-51). Lower values result in better quality but higher bitrate.
	*	Default value is 20
	* "ratecontrolmode" :
	*	H264 rate control mode. Valid values are (constqp, vbr, cbr).
	*	Default value is cbr
	*/ 
	TArray<TPair<FString, FString>> Options;
};


struct FAudioEncoderConfig
{
	uint32 Samplerate = 0;
	uint32 NumChannels = 0;
	// Encoder bitrate (bits per second, NOT bytes per second)
	uint32 Bitrate = 0;
	TArray<TPair<FString, FString>> Options;
};

struct FAudioFrame
{
	FTimespan Timestamp;
	FTimespan Duration;
	Audio::TSampleBuffer<float> Data;
};

enum class EPacketType
{
	Audio,
	Video,
	Invalid
};

struct FAVPacket
{
	explicit FAVPacket(EPacketType TypeIn)
	{
		Type = TypeIn;
		if (Type == EPacketType::Audio)
		{
		}
		else if (Type == EPacketType::Video)
		{
			FMemory::Memzero(Video);
		}
	}

	FAVPacket(const FAVPacket& Other) = default;
	FAVPacket(FAVPacket&& Other) = default;
	FAVPacket& operator=(const FAVPacket& Other) = default;
	FAVPacket& operator=(FAVPacket&& Other) = default;

	EPacketType Type;
	FTimespan Timestamp;
	FTimespan Duration;
	struct
	{
		FTimespan EncodeStartTs;
		FTimespan EncodeFinishTs;
	} Timings;
	TArray<uint8> Data;

	union
	{
		struct
		{
			bool bKeyFrame;
			int32 Width;
			int32 Height;
			uint32 FrameAvgQP;
			uint32 Framerate;
		} Video;

		struct
		{
			// Nothing at the moment
		} Audio;
	};

	bool IsVideoKeyFrame() const
	{
		return (Type==EPacketType::Video && Video.bKeyFrame) ? true : false;
	}

	/**
	 * The encoder can fail to encode a given frame
	 * This might be expanded to an enum to show a reason, or removed entirely in the future
	 * once we make the encoders a bit sturdier. Ideally, failing to encode should be dealt internally by the encoder,
	 * correcting whatever failed. In turn a FVPacket received from the encoder should all be valid
	 */
	bool IsValid() const
	{
		return Data.Num() ? true : false;
	}

private:

};

class FVideoEncoder;
class FAudioEncoder;

class AVENCODER_API FVideoEncoderFactory
{
public:
	virtual ~FVideoEncoderFactory() {}
	virtual const TCHAR* GetName() const = 0;
	virtual TArray<FString> GetSupportedCodecs() const = 0;
	virtual TUniquePtr<FVideoEncoder> CreateEncoder(const FString& Codec) = 0;

	static void RegisterFactory(FVideoEncoderFactory& Factory);
	static void UnregisterFactory(FVideoEncoderFactory& Factory);
	static FVideoEncoderFactory* FindFactory(const FString& Codec);
	static const TArray<FVideoEncoderFactory*> GetAllFactories();

private:

	static TArray<FVideoEncoderFactory*> Factories;
};

class AVENCODER_API FAudioEncoderFactory
{
public:
	virtual ~FAudioEncoderFactory() {}
	virtual const TCHAR* GetName() const = 0;
	virtual TArray<FString> GetSupportedCodecs() const = 0;
	virtual TUniquePtr<FAudioEncoder> CreateEncoder(const FString& Codec) = 0;

	static void RegisterFactory(FAudioEncoderFactory& Factory);
	static void UnregisterFactory(FAudioEncoderFactory& Factory);
	static FAudioEncoderFactory* FindFactory(const FString& Codec);
	static const TArray<FAudioEncoderFactory*> GetAllFactories();

private:
	static TArray<FAudioEncoderFactory*> Factories;
};

class IVideoEncoderListener
{
public:
	virtual void OnEncodedVideoFrame(const FAVPacket& Packet, FEncoderVideoFrameCookie* Cookie) = 0;
};

class IAudioEncoderListener
{
public:
	virtual void OnEncodedAudioFrame(const FAVPacket& Packet) = 0;
};

using FBufferId = uint32;

class AVENCODER_API FVideoEncoder
{
public:
	FVideoEncoder() {}
	virtual ~FVideoEncoder() {}
	virtual const TCHAR* GetName() const = 0;
	virtual const TCHAR* GetType() const = 0;
	virtual bool Initialize(const FVideoEncoderConfig& Config) = 0;
	virtual void Shutdown() = 0;

	//
	// To account for how webrtc works (for PixelStreaming), encoding a frame is a two step process.
	// 1st - CopyTexture initiates a copy if the texture to the internal buffers, and returns an Id the caller can use to reference that internal buffer.
	// 2nd - Either a Drop or Encode is required for each CopyTexture call, otherwise the respective internal buffer will stay
	// @param Texture Texture to copy
	// @param CaptureTs Capture timestamp
	// @param Duration Delta time from the previous frame
	// @param OutBufferId Buffer id on return
	// @param Resolution If {0,0}, the copy will be the same size as the passed texture. If not {0,0}, it will use that specified resolution
	// marked as used.
	virtual bool CopyTexture(FTexture2DRHIRef Texture, FTimespan CaptureTs, FTimespan Duration, FBufferId& OutBufferId, FIntPoint Resolution = {0, 0}) = 0;
	virtual void Drop(FBufferId BufferId) = 0;
	virtual void Encode(FBufferId BufferId, bool bForceKeyFrame, uint32 Bitrate = 0, TUniquePtr<AVEncoder::FEncoderVideoFrameCookie> Cookie = nullptr) = 0;

	// We return a copy instead of a reference, to simplify multithread  access
	virtual FVideoEncoderConfig GetConfig() const = 0;

	// Allows setting bitrate and framerate at runtime.
	virtual bool SetBitrate(uint32 Bitrate) = 0;
	virtual bool SetFramerate(uint32 Framerate) = 0;
	virtual bool SetParameter(const FString& Parameter, const FString& Value) = 0;

	virtual void RegisterListener(IVideoEncoderListener& Listener);
	virtual void UnregisterListener(IVideoEncoderListener& Listener);

protected:
	void OnEncodedVideoFrame(const FAVPacket& Packet, TUniquePtr<FEncoderVideoFrameCookie> Cookie);

private:

	FCriticalSection ListenersMutex;
	TArray<IVideoEncoderListener*> Listeners;

};

class AVENCODER_API FAudioEncoder
{
public:
	virtual ~FAudioEncoder() {}
	virtual const TCHAR* GetName() const = 0;
	virtual const TCHAR* GetType() const = 0;
	virtual bool Initialize(const FAudioEncoderConfig& Config) = 0;
	virtual void Shutdown() = 0;

	virtual void Encode(const FAudioFrame& Frame) = 0;
	virtual FAudioEncoderConfig GetConfig() const = 0;

	virtual void RegisterListener(IAudioEncoderListener& Listener);
	virtual void UnregisterListener(IAudioEncoderListener& Listener);

protected:
	void OnEncodedAudioFrame(const FAVPacket& Packet);
private:

	FCriticalSection ListenersMutex;
	TArray<IAudioEncoderListener*> Listeners;

};

void UnregisterDefaultFactories();

}

