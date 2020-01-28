// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputBase.h"
#include "Engine/RendererSettings.h"

void UMoviePipelineOutputBase::ValidateStateImpl()
{
	Super::ValidateStateImpl();

	if (IsAlphaSupported())
	{
		const URendererSettings* RenderSettings = GetDefault<URendererSettings>();
		if (RenderSettings->bEnableAlphaChannelInPostProcessing == EAlphaChannelMode::Type::Disabled)
		{
			ValidationState = EMoviePipelineValidationState::Warnings;
			ValidationResults.Add(NSLOCTEXT("MovieRenderPipeline", "Outputs_AlphaWithoutProjectSetting",
				"This option does not work without enabling the Alpha Support in Tonemapper setting via Project Settings > Rendering > Post Processing > Enable Alpha Channel Support."));
		}

	}
}