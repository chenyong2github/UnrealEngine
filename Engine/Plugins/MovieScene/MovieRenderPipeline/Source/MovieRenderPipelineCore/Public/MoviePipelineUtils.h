// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"

// Forward Declare
class UClass;
class UMoviePipelineAntiAliasingSetting;

namespace UE
{
	namespace MovieRenderPipeline
	{
		MOVIERENDERPIPELINECORE_API TArray<UClass*> FindMoviePipelineSettingClasses();
		MOVIERENDERPIPELINECORE_API EAntiAliasingMethod GetEffectiveAntiAliasingMethod(const UMoviePipelineAntiAliasingSetting* InSetting);
	}
}