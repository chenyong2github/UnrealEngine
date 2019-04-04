// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseVideoEncoder.h"

GAMEPLAYMEDIAENCODER_START

FBaseVideoEncoder::FBaseVideoEncoder(const FOutputSampleCallback& OutputCallback)
	: OutputCallback(OutputCallback)
{
}

bool FBaseVideoEncoder::GetOutputType(TRefCountPtr<IMFMediaType>& OutType)
{
	OutType = OutputType;
	return true;
}

bool FBaseVideoEncoder::Initialize(const FVideoEncoderConfig& InConfig)
{
	UE_LOG(GameplayMediaEncoder, Log, TEXT("VideoEncoder config: %dx%d, %d FPS, %.2f Mbps"), InConfig.Width, InConfig.Height, InConfig.Framerate, InConfig.Bitrate / 1000000.0f);

	CHECK_HR(MFCreateMediaType(OutputType.GetInitReference()));
	CHECK_HR(OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	CHECK_HR(OutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	CHECK_HR(OutputType->SetUINT32(MF_MT_AVG_BITRATE, InConfig.Bitrate));
	CHECK_HR(MFSetAttributeRatio(OutputType, MF_MT_FRAME_RATE, InConfig.Framerate, 1));
	CHECK_HR(MFSetAttributeSize(OutputType, MF_MT_FRAME_SIZE, InConfig.Width, InConfig.Height));
	CHECK_HR(MFSetAttributeRatio(OutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	CHECK_HR(OutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	Config = InConfig;
	return true;
}

bool FBaseVideoEncoder::SetBitrate(uint32 Bitrate)
{
	check(IsInRenderingThread()); // encoders apply these changes immediately and not thread-safely

	CHECK_HR(OutputType->SetUINT32(MF_MT_AVG_BITRATE, Bitrate));
	Config.Bitrate = Bitrate;

	return true;
}

bool FBaseVideoEncoder::SetFramerate(uint32 Framerate)
{
	check(IsInRenderingThread()); // encoders apply these changes immediately and not thread-safely

	CHECK_HR(MFSetAttributeRatio(OutputType, MF_MT_FRAME_RATE, Framerate, 1));
	Config.Framerate = Framerate;

	return true;
}

GAMEPLAYMEDIAENCODER_END

