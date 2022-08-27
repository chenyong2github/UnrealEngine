// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"
#include "Builders/GLTFBuilderUtility.h"

FGLTFBuilder::FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: bIsGlbFile(FGLTFBuilderUtility::IsGlbFile(FilePath))
	, FilePath(FilePath)
	, ExportOptions(ExportOptions)
{
}
