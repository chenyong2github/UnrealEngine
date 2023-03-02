// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMovieRenderPipelineEditorModule.h"
#include "IMovieRendererInterface.h"
#include "Delegates/IDelegateInstance.h"

class FMovieRenderPipelineEditorModule : public IMovieRenderPipelineEditorModule
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
private:
	void RegisterSettings();
	void UnregisterSettings();

	void RegisterMovieRenderer();
	void UnregisterMovieRenderer();

	void RegisterTypeCustomizations();
	void UnregisterTypeCustomizations();

	FDelegateHandle MovieRendererDelegate;
};
