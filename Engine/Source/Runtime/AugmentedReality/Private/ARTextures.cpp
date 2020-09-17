// Copyright Epic Games, Inc. All Rights Reserved.

#include "ARTextures.h"

UARTexture::UARTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UARTextureCameraImage::UARTextureCameraImage(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TextureType = EARTextureType::CameraImage;
}

UARTextureCameraDepth::UARTextureCameraDepth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TextureType = EARTextureType::CameraDepth;
}

UAREnvironmentCaptureProbeTexture::UAREnvironmentCaptureProbeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TextureType(EARTextureType::EnvironmentCapture)
{
}
