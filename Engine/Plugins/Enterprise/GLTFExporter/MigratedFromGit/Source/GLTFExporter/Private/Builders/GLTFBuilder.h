// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExportOptions.h"

class FGLTFBuilder
{
protected:

	FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	const FString FilePath;

	const UGLTFExportOptions* const ExportOptions;
};
