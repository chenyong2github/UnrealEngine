// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WmfPrivate.h"

#include "Math/NumericLimits.h"
#include "AVEncoder.h"
#include "Misc/Optional.h"

class FWmfMp4Writer final
{
public:
	bool Initialize(const TCHAR* Filename);

	/**
	 * Create an audio stream and return the its index on success
	 */
	TOptional<DWORD> CreateAudioStream(const FString& Codec, const AVEncoder::FAudioEncoderConfig& Config);

	/**
	 * Create a video stream and return the its index on success
	 */
	TOptional<DWORD> CreateVideoStream(const FString& Codec, const AVEncoder::FVideoEncoderConfig& Config);

	bool Start();
	bool Write(const AVEncoder::FAVPacket& InSample, DWORD StreamIndex);
	bool Finalize();

private:
	TRefCountPtr<IMFSinkWriter> Writer;
};

