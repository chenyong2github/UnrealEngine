// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayMediaEncoderCommon.h"

class FD3D11VideoProcessor
{
public:
	bool Initialize(uint32 Width, uint32 Height);
	bool ConvertTexture(const FTexture2DRHIRef& InTexture, const FTexture2DRHIRef& OutTexture);

private:
	TRefCountPtr<ID3D11VideoDevice> VideoDevice;
	TRefCountPtr<ID3D11VideoContext> VideoContext;
	TRefCountPtr<ID3D11VideoProcessor> VideoProcessor;
	TRefCountPtr<ID3D11VideoProcessorEnumerator> VideoProcessorEnumerator;
	TMap<ID3D11Texture2D*, TRefCountPtr<ID3D11VideoProcessorInputView>> InputViews;
	TMap<ID3D11Texture2D*, TRefCountPtr<ID3D11VideoProcessorOutputView>> OutputViews;
};

