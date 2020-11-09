// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineDebugSettings.h"

#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "Rendering/IRenderCaptureProvider.h"
#endif

UMoviePipelineDebugSettings::UMoviePipelineDebugSettings()
	: bIsRenderDebugCaptureAvailable(false)
{
#if WITH_EDITOR && !UE_BUILD_SHIPPING
	bIsRenderDebugCaptureAvailable = IRenderCaptureProvider::IsAvailable();
#endif
}

#if WITH_EDITOR

FText UMoviePipelineDebugSettings::GetFooterText(UMoviePipelineExecutorJob* InJob) const
{
	return FText();
}


bool UMoviePipelineDebugSettings::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMoviePipelineDebugSettings, bCaptureFramesWithRenderDoc))
	{
		return bIsRenderDebugCaptureAvailable;
	}

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMoviePipelineDebugSettings, CaptureStartFrame))
	{
		return bIsRenderDebugCaptureAvailable && bCaptureFramesWithRenderDoc;
	}
	
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMoviePipelineDebugSettings, CaptureEndFrame))
	{
		return bIsRenderDebugCaptureAvailable && bCaptureFramesWithRenderDoc;
	}

	return Super::CanEditChange(InProperty);
}

#endif // WITH_EDITOR