// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingRendererShader.h"

IMPLEMENT_GLOBAL_SHADER(FDMXPixelMappingRendererVS, "/Plugin/DMXPixelMapping/Private/DMXPixelMapping.usf", "DMXPixelMappingVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FDMXPixelMappingRendererPS, "/Plugin/DMXPixelMapping/Private/DMXPixelMapping.usf", "DMXPixelMappingPS", SF_Pixel);
