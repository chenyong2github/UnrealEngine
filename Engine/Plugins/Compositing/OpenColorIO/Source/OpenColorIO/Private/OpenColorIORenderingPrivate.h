// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "OpenColorIOColorSpace.h"


void ProcessOCIOColorSpaceTransform_RenderThread(
	FRHICommandListImmediate& InRHICmdList
	, ERHIFeatureLevel::Type InFeatureLevel
	, FOpenColorIOTransformResource* InOCIOColorTransformResource
	, FTextureResource* InLUT3dResource
	, FTextureRHIRef InputSpaceColorTexture
	, FTextureRHIRef OutputSpaceColorTexture
	, FIntPoint OutputResolution);