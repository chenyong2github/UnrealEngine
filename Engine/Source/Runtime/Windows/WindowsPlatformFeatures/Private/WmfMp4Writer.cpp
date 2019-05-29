// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WmfMp4Writer.h"

#include "GameplayMediaEncoderSample.h"

#if WMFMEDIA_SUPPORTED_PLATFORM
	#pragma comment(lib, "mfplat")
	#pragma comment(lib, "mfuuid")
	#pragma comment(lib, "Mfreadwrite")
#endif

DECLARE_LOG_CATEGORY_EXTERN(MP4, Log, VeryVerbose);

DEFINE_LOG_CATEGORY(MP4);

WINDOWSPLATFORMFEATURES_START

bool FWmfMp4Writer::Initialize(const TCHAR* Filename)
{
	CHECK_HR(MFCreateSinkWriterFromURL(Filename, nullptr, nullptr, Writer.GetInitReference()));
	UE_LOG(WMF, Verbose, TEXT("Initialised Mp4Writer for %s"), Filename);
	return true;
}

bool FWmfMp4Writer::CreateStream(IMFMediaType* StreamType, DWORD& StreamIndex)
{
	CHECK_HR(Writer->AddStream(StreamType, &StreamIndex));
	// no transcoding here so input type is the same as output type
	CHECK_HR(Writer->SetInputMediaType(StreamIndex, StreamType, nullptr));
	return true;
}

bool FWmfMp4Writer::Start()
{
	CHECK_HR(Writer->BeginWriting());
	return true;
}

bool FWmfMp4Writer::Write(const FGameplayMediaEncoderSample& Sample, DWORD StreamIndex)
{
	CHECK_HR(Writer->WriteSample(StreamIndex, const_cast<IMFSample*>(Sample.GetSample())));

	UE_LOG(MP4, VeryVerbose, TEXT("stream #%d: time %.3f, duration %.3f%s"), static_cast<int>(Sample.GetType()), Sample.GetTime().GetTotalSeconds(), Sample.GetDuration().GetTotalSeconds(), Sample.IsVideoKeyFrame() ? TEXT(", key-frame") : TEXT(""));

	return true;
}

bool FWmfMp4Writer::Finalize()
{
	CHECK_HR(Writer->Finalize());
	UE_LOG(WMF, VeryVerbose, TEXT("Closed .mp4"));

	return true;
}

WINDOWSPLATFORMFEATURES_END


