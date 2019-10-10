// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "MaterialEditorSettings.generated.h"

UCLASS(config = EditorPerProjectUserSettings)
class MATERIALEDITOR_API UMaterialEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	/**
	Path to user installed Mali shader compiler that can be used by the material editor to compile and extract shader informations for Android platforms.
	Official website address: https://developer.arm.com/products/software-development-tools/graphics-development-tools/mali-offline-compiler/downloads
	*/
	UPROPERTY(config, EditAnywhere, Category = "Offline Shader Compilers", meta = (DisplayName = "Mali Offline Compiler"))
	FFilePath MaliOfflineCompilerPath;

protected:
	// The width (in pixels) of the preview viewport when a material editor is first opened
	UPROPERTY(config, EditAnywhere, meta=(ClampMin=1, ClampMax=4096), Category="User Interface Domain")
	int32 DefaultPreviewWidth = 250;

	// The height (in pixels) of the preview viewport when a material editor is first opened
	UPROPERTY(config, EditAnywhere, meta=(ClampMin=1, ClampMax=4096), Category="User Interface Domain")
	int32 DefaultPreviewHeight = 250;

public:
	FIntPoint GetPreviewViewportStartingSize() const
	{
		return FIntPoint(DefaultPreviewWidth, DefaultPreviewHeight);
	}
};