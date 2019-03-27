// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayMediaEncoderCommon.h"
#include "GameplayMediaEncoderSample.h"

struct FWmfAudioEncoderConfig
{
	uint32 NumChannels;
	uint32 SampleRate;
	uint32 Bitrate;
	// bits per sample must be 16 as the only value supported by WMF AAC encoder
};

class FWmfAudioEncoder
{
public:
	using FOutputSampleCallback = TFunction<bool(const FGameplayMediaEncoderSample&)>;

	explicit FWmfAudioEncoder(const FOutputSampleCallback& OutputCallback);

	bool Initialize(const FWmfAudioEncoderConfig& Config);
	bool Process(const int8* SampleData, uint32 Size, FTimespan Timestamp, FTimespan Duration);
	bool Flush();

	bool GetOutputType(TRefCountPtr<IMFMediaType>& OutType);

	const FWmfAudioEncoderConfig& GetConfig() const
	{
		return Config;
	}

private:
	bool SetInputType();
	bool SetOutputType();
	bool RetrieveStreamInfo();
	bool StartStreaming();

	bool CreateInputSample(const int8* SampleData, uint32 Size, FTimespan Timestamp, FTimespan Duration, FGameplayMediaEncoderSample& Sample);
	bool CreateOutputSample(FGameplayMediaEncoderSample& OutSample);
	bool GetOutputSample(FGameplayMediaEncoderSample& OutSample);

private:
	FOutputSampleCallback OutputCallback;

	FWmfAudioEncoderConfig Config;
	TRefCountPtr<IMFTransform> Encoder;
	TRefCountPtr<IMFMediaType> OutputType;
	MFT_INPUT_STREAM_INFO InputStreamInfo = {};
	MFT_OUTPUT_STREAM_INFO OutputStreamInfo = {};
};