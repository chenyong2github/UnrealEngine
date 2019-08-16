// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessDownsample.h: Post processing down sample implementation.
=============================================================================*/

#pragma once

#include "PostProcess/RenderingCompositionGraph.h"
#include "ScreenPass.h"

enum class EDownsampleFlags : uint8
{
	None = 0,

	// Forces the downsample pass to run on the raster pipeline, regardless of view settings.
	ForceRaster = 0x1
};
ENUM_CLASS_FLAGS(EDownsampleFlags);

enum class EDownsampleQuality : uint8
{
	// Single filtered sample (2x2 tap).
	Low,

	// Four filtered samples (4x4 tap).
	High,

	MAX
};

// Returns the global downsample quality specified by the r.Downsample.Quality CVar.
EDownsampleQuality GetDownsampleQuality();

// The set of inputs needed to add a downsample pass to RDG.
struct FDownsamplePassInputs
{
	FDownsamplePassInputs() = default;

	// Friendly name of the pass. Used for logging and profiling.
	const TCHAR* Name = nullptr;

	// Input RDG texture. Must not be null.
	FRDGTextureRef Texture = nullptr;

	// Input viewport to sample from.
	FIntRect Viewport;

	// The downsample method to use.
	EDownsampleQuality Quality = EDownsampleQuality::Low;

	// Flags to control how the downsample pass is run.
	EDownsampleFlags Flags = EDownsampleFlags::None;

	// The format to use for the output texture (if unknown, the input format is used).
	EPixelFormat FormatOverride = PF_Unknown;
};

struct FDownsamplePassOutputs
{
	FDownsamplePassOutputs() = default;

	// Half-resolution texture.
	FRDGTextureRef Texture = nullptr;

	// Half-resolution viewport.
	FIntRect Viewport;
};

FDownsamplePassOutputs AddDownsamplePass(FRDGBuilder& GraphBuilder, const FScreenPassViewInfo& ScreenPassView, const FDownsamplePassInputs& Inputs);

FRenderingCompositeOutputRef AddDownsamplePass(
	FRenderingCompositionGraph& Graph,
	const TCHAR *Name,
	FRenderingCompositeOutputRef Input,
	uint32 SceneColorDownsampleFactor,
	EDownsampleQuality Quality = EDownsampleQuality::Low,
	EDownsampleFlags Flags = EDownsampleFlags::None,
	EPixelFormat FormatOverride = PF_Unknown);