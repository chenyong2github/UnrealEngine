// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInputVCam.h"

TSharedPtr<FPixelStreamingVideoInputVCam> FPixelStreamingVideoInputVCam::Create()
{
	TSharedPtr<FPixelStreamingVideoInputVCam> NewInput = TSharedPtr<FPixelStreamingVideoInputVCam>(new FPixelStreamingVideoInputVCam());
	return NewInput;
}

FPixelStreamingVideoInputVCam::FPixelStreamingVideoInputVCam()
{
}

FPixelStreamingVideoInputVCam::~FPixelStreamingVideoInputVCam()
{
}

FString FPixelStreamingVideoInputVCam::ToString()
{
	return TEXT("a Virtual Camera");
}
