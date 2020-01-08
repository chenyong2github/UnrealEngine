// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "RenderingCompositionGraph.h"

struct FGaussianBlurInputs
{
	// Friendly names of the blur passes along the X and Y axis. Used for logging and profiling.
	const TCHAR* NameX = nullptr;
	const TCHAR* NameY = nullptr;

	// The input texture to be filtered.
	FScreenPassTexture Filter;

	// The input texture to be added after filtering.
	FScreenPassTexture Additive;

	// The color to tint when filtering.
	FLinearColor TintColor;

	// Controls the cross shape of the blur, in both X / Y directions. See r.Bloom.Cross.
	FVector2D CrossCenterWeight = FVector2D::ZeroVector;

	// The filter kernel size in percentage of the screen.
	float KernelSizePercent = 0.0f;
};

using FGaussianBlurOutputs = FScreenPassTexture;

FGaussianBlurOutputs AddGaussianBlurPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FGaussianBlurInputs& Inputs);

FRenderingCompositeOutputRef AddGaussianBlurPass(
	FRenderingCompositionGraph& Graph,
	const TCHAR *InNameX,
	const TCHAR* InNameY,
	FRenderingCompositeOutputRef FilterInput,
	float FilterPercent,
	FLinearColor Tint = FLinearColor::White,
	FRenderingCompositeOutputRef Additive = FRenderingCompositeOutputRef(),
	FVector2D CrossCenterWeight = FVector2D::ZeroVector);
