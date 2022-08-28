// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFConvertBuilder(FilePath, ExportOptions, bSelectedActorsOnly)
{
}

bool FGLTFContainerBuilder::Serialize(FArchive& Archive)
{
	return FGLTFJsonBuilder::Serialize(Archive);
}
