// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScreenPass.h"
#include "PostProcess/RenderingCompositionGraph.h"

BEGIN_SHADER_PARAMETER_STRUCT(FColorRemapParameters, )
	SHADER_PARAMETER(FVector, MappingPolynomial)
END_SHADER_PARAMETER_STRUCT()

FColorRemapParameters GetColorRemapParameters();

BEGIN_SHADER_PARAMETER_STRUCT(FFilmTonemapParameters, )
	SHADER_PARAMETER(float, FilmSlope)
	SHADER_PARAMETER(float, FilmToe)
	SHADER_PARAMETER(float, FilmShoulder)
	SHADER_PARAMETER(float, FilmBlackClip)
	SHADER_PARAMETER(float, FilmWhiteClip)
END_SHADER_PARAMETER_STRUCT()

FFilmTonemapParameters GetFilmTonemapParameters(const FPostProcessSettings& Settings);

bool PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(EShaderPlatform Platform);

FRDGTextureRef AddCombineLUTPass(FRDGBuilder& GraphBuilder, const FScreenPassViewInfo& ScreenPassView);

FRenderingCompositeOutputRef AddCombineLUTPass(FRenderingCompositionGraph& Graph);