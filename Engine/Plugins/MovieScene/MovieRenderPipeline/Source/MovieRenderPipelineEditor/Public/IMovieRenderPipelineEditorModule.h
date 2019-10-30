// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Public module interface for the Movie Render Pipeline module
 */
class IMovieRenderPipelineEditorModule : public IModuleInterface
{
public:
	/** The tab name for the movie render pipeline tab */
	static FName MovieRenderPipelineTabName;

	/** The default label for the movie render pipeline tab. */
	static FText MovieRenderPipelineTabLabel;
};
