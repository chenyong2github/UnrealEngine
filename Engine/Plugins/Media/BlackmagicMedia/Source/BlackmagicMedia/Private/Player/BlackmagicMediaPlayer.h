// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaIOCorePlayerBase.h"

#include "BlackmagicMediaSource.h"
#include "GPUTextureTransferModule.h"
#include "GPUTextureTransfer.h"
#include "HAL/CriticalSection.h"
#include "MediaIOCoreAudioSampleBase.h"
#include "MediaIOCoreTextureSampleBase.h"
#include "MediaObjectPool.h"
#include "MediaShaders.h"

class IMediaEventSink;

enum class EMediaTextureSampleFormat;
enum class EMediaIOSampleType;

namespace BlackmagicMediaPlayerHelpers
{
	class FBlackmagicMediaPlayerEventCallback;
}

class FBlackmagicMediaTextureSample : public FMediaIOCoreTextureSampleBase
{
public:
	virtual const FMatrix& GetYUVToRGBMatrix() const override { return MediaShaders::YuvToRgbRec709Scaled; }

	virtual void ShutdownPoolable() override
	{
		if (DestructionCallback)
		{
			DestructionCallback(Texture);
		}

		FMediaIOCoreTextureSampleBase::FreeSample();
	}

	void SetTexture(TRefCountPtr<FRHITexture> InRHITexture)
	{
		Texture = MoveTemp(InRHITexture);
	}

	void SetDestructionCallback(TFunction<void(TRefCountPtr<FRHITexture2D>)> InDestructionCallback)
	{
		DestructionCallback = InDestructionCallback;
	}

	virtual const void* GetBuffer() override
	{
		// Don't return the buffer if we have a texture to force the media player to use the texture if available. 
		if (Texture)
		{
			return nullptr;
		}
		return Buffer.GetData();

	}

	void* GetBlackmagicBuffer()
	{
		return BlackmagicBufferPtr;
	}

	void SetBlackmagicBuffer(void* InBuffer)
	{
		BlackmagicBufferPtr = InBuffer;
	}

#if WITH_ENGINE
	virtual FRHITexture* GetTexture() const override
	{
		if (Texture)
		{
			return Texture.GetReference();
		}
		return nullptr;
	}
#endif //WITH_ENGINE

private:
	/** Hold a texture to be used for gpu texture transfers. */
	TRefCountPtr<FRHITexture2D> Texture;

	/** Called when the sample is destroyed by its pool. */
	TFunction<void(TRefCountPtr<FRHITexture2D>)> DestructionCallback;

	void* BlackmagicBufferPtr = nullptr;
};

class FBlackmagicMediaAudioSamplePool : public TMediaObjectPool<FMediaIOCoreAudioSampleBase> { };
class FBlackmagicMediaTextureSamplePool : public TMediaObjectPool<FBlackmagicMediaTextureSample> { };

/**
 * Implements a media player for Blackmagic.
 *
 * The processing of metadata and video frames is delayed until the fetch stage
 * (TickFetch) in order to increase the window of opportunity for receiving
 * frames for the current render frame time code.
 *
 * Depending on whether the media source enables time code synchronization,
 * the player's current play time (CurrentTime) is derived either from the
 * time codes embedded in frames or from the Engine's global time code.
 */
class FBlackmagicMediaPlayer : public FMediaIOCorePlayerBase, public TSharedFromThis<FBlackmagicMediaPlayer>
{
private:
	using Super = FMediaIOCorePlayerBase;

public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param InEventSink The object that receives media events from this player.
	 */
	FBlackmagicMediaPlayer(IMediaEventSink& InEventSink);

	/** Virtual destructor. */
	virtual ~FBlackmagicMediaPlayer();

public:

	//~ IMediaPlayer interface

	virtual void Close() override;
	virtual FGuid GetPlayerPluginGUID() const override;

	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;

	virtual void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;
	virtual void TickFetch(FTimespan DeltaTime, FTimespan Timecode) override;

	//~ ITimedDataInput interface
#if WITH_EDITOR
	virtual const FSlateBrush* GetDisplayIcon() const override;
#endif

public:

	/** Process pending audio and video frames, and forward them to the sinks. */
	void ProcessFrame();

	/** Verify if we lost some frames since last Tick. */
	void VerifyFrameDropCount();

	/** Is Hardware initialized */
	virtual bool IsHardwareReady() const override;

protected:
	/** Setup our different channels with the current set of settings */
	virtual void SetupSampleChannels() override;
	void OnSampleDestroyed(TRefCountPtr<FRHITexture> InTexture);
	void RegisterSampleBuffer(const TSharedPtr<FBlackmagicMediaTextureSample>& InSample);
	void UnregisterSampleBuffers();
	void CreateAndRegisterTextures(const IMediaOptions* Options);
	void UnregisterTextures();
private:


private:

	friend BlackmagicMediaPlayerHelpers::FBlackmagicMediaPlayerEventCallback;
	BlackmagicMediaPlayerHelpers::FBlackmagicMediaPlayerEventCallback* EventCallback;

	/** Audio, MetaData, Texture  sample object pool. */
	TUniquePtr<FBlackmagicMediaAudioSamplePool> AudioSamplePool;
	TUniquePtr<FBlackmagicMediaTextureSamplePool> TextureSamplePool;

	/** Log warning about the amount of audio/video frame can't could not be cached . */
	bool bVerifyFrameDropCount;

	/** Max sample count our different buffer can hold. Taken from MediaSource */
	int32 MaxNumAudioFrameBuffer = 0;
	int32 MaxNumVideoFrameBuffer = 0;

	/** Used to flag which sample types we advertise as supported for timed data monitoring */
	EMediaIOSampleType SupportedSampleTypes;

	FCriticalSection TexturesCriticalSection;

	/** GPU Texture transfer object */
	UE::GPUTextureTransfer::TextureTransferPtr GPUTextureTransfer;

	/** Pool of textures registerd with GPU Texture transfer. */
	TArray<TRefCountPtr<FRHITexture>> Textures;
	/** Buffers Registered with GPU Texture Transfer */
	TSet<void*> RegisteredBuffers;
	/** Pool of textures registerd with GPU Texture transfer. */
	TSet<TRefCountPtr<FRHITexture>> RegisteredTextures;

	EBlackmagicMediaSourceColorFormat ColorFormat = EBlackmagicMediaSourceColorFormat::YUV8;
};
