// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaIOCorePlayerBase.h"

#include "MediaIOCoreAudioSampleBase.h"
#include "MediaIOCoreTextureSampleBase.h"
#include "MediaObjectPool.h"
#include "MediaShaders.h"

class IMediaEventSink;

enum class EMediaTextureSampleFormat;

namespace BlackmagicMediaPlayerHelpers
{
	class FBlackmagicMediaPlayerEventCallback;
}

class FBlackmagicMediaTextureSample : public FMediaIOCoreTextureSampleBase
{
	virtual const FMatrix& GetYUVToRGBMatrix() const override { return MediaShaders::YuvToRgbRec709Full; }
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
class FBlackmagicMediaPlayer : public FMediaIOCorePlayerBase
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
	virtual FName GetPlayerName() const override;

	virtual bool Open(const FString& Url, const IMediaOptions* Options) override;

	virtual void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;
	virtual void TickFetch(FTimespan DeltaTime, FTimespan Timecode) override;

	/** Process pending audio and video frames, and forward them to the sinks. */
	void ProcessFrame();

	/** Verify if we lost some frames since last Tick. */
	void VerifyFrameDropCount();

	/** Is Hardware initialized */
	virtual bool IsHardwareReady() const override;

private:

	friend BlackmagicMediaPlayerHelpers::FBlackmagicMediaPlayerEventCallback;
	BlackmagicMediaPlayerHelpers::FBlackmagicMediaPlayerEventCallback* EventCallback;

	/** Audio, MetaData, Texture  sample object pool. */
	FBlackmagicMediaAudioSamplePool* AudioSamplePool;
	FBlackmagicMediaTextureSamplePool* TextureSamplePool;

	/** Log warning about the amount of audio/video frame can't could not be cached . */
	bool bVerifyFrameDropCount;
};
