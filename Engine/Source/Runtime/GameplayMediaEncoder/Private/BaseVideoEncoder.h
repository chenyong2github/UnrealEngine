// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayMediaEncoderSample.h"
#include "GameplayMediaEncoderCommon.h"

struct FVideoEncoderConfig
{
	uint32 Width;
	uint32 Height;
	uint32 Framerate;
	uint32 Bitrate;

	FVideoEncoderConfig()
		: Width(0)
		, Height(0)
		, Framerate(0)
		, Bitrate(0)
	{}
};

class FBaseVideoEncoder
{
public:
	using FOutputSampleCallback = TFunction<bool(const FGameplayMediaEncoderSample&)>;

	explicit FBaseVideoEncoder(const FOutputSampleCallback& OutputCallback);
	virtual ~FBaseVideoEncoder() {}

	const FVideoEncoderConfig& GetConfig() const
	{ return Config; }

	virtual bool Initialize(const FVideoEncoderConfig& Config);
	bool GetOutputType(TRefCountPtr<IMFMediaType>& OutType);
	virtual bool Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration) = 0;

	// is called from same thread as `Process()`
	// dynamically changes bitrate in runtime to adjust to available bandwidth
	virtual bool SetBitrate(uint32 Bitrate) = 0; // has impl despite being "pure"

	// is called from same thread as `Process()`
	// dynamically changes framerate in runtime to adjust to available bandwidth
	virtual bool SetFramerate(uint32 Framerate) = 0; // has impl despite being "pure"

	virtual bool Start() = 0;
	virtual void Stop() = 0;
protected:
	FOutputSampleCallback OutputCallback;
	FVideoEncoderConfig Config;
	TRefCountPtr<IMFMediaType> OutputType;
	uint64 InputCount = 0;
	uint64 OutputCount = 0;
};

