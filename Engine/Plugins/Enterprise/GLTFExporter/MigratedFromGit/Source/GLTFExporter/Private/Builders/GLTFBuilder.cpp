// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"

FGLTFBuilder::FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FilePath(FilePath)
	, ExportOptions(ExportOptions)
{
}
