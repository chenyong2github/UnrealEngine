// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExportOptions.h"
#include "Json/GLTFJsonEnums.h"

class FGLTFBuilder
{
protected:

	FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	const bool bIsGlbFile;
	const FString FilePath;
	const FString DirPath;

	const UGLTFExportOptions* const ExportOptions;

	FIntPoint GetDefaultMaterialBakeSize() const;

	EGLTFJsonHDREncoding GetTextureHDREncoding() const;

	bool ShouldExportLight(EComponentMobility::Type LightMobility) const;
};
