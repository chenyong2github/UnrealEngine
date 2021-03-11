// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScenePrivate.h"
#include "RenderGraph.h"
#include "SceneTextureParameters.h"

void BuildHZB(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBufferTexture,
	const FIntRect ViewRect,
	ERHIFeatureLevel::Type FeatureLevel,
	EShaderPlatform ShaderPlatform,
	FRDGTextureRef* OutClosestHZBTexture,
	FRDGTextureRef* OutFurthestHZBTexture,
	EPixelFormat Format = PF_R16F);

void BuildHZB(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBufferTexture,
	const FViewInfo& View,
	FRDGTextureRef* OutClosestHZBTexture,
	FRDGTextureRef* OutFurthestHZBTexture);
