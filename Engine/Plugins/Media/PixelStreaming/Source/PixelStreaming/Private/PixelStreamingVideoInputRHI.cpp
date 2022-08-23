// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInputRHI.h"
#include "PixelStreamingPrivate.h"
#include "Settings.h"

#include "PixelCaptureBufferFormat.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureCapturerRHIToI420CPU.h"
#include "PixelCaptureCapturerRHIToI420Compute.h"

TSharedPtr<FPixelCaptureCapturer> FPixelStreamingVideoInputRHI::CreateCapturer(int32 FinalFormat, float FinalScale)
{
	switch (FinalFormat)
	{
		case PixelCaptureBufferFormat::FORMAT_RHI:
		{
			return FPixelCaptureCapturerRHI::Create(FinalScale);
		}
		case PixelCaptureBufferFormat::FORMAT_I420:
		{
			if (UE::PixelStreaming::Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread())
			{
				return FPixelCaptureCapturerRHIToI420Compute::Create(FinalScale);
			}
			else
			{
				return FPixelCaptureCapturerRHIToI420CPU::Create(FinalScale);
			}
		}
		default:
			UE_LOG(LogPixelStreaming, Error, TEXT("Unsupported final format %d"), FinalFormat);
			return nullptr;
	}
}
