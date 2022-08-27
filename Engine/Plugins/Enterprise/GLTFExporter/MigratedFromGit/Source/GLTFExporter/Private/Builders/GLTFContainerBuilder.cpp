// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFConvertBuilder(ExportOptions, bSelectedActorsOnly)
{
}

bool FGLTFContainerBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	return FGLTFImageBuilder::Serialize(FilePath)
		&& FGLTFBufferBuilder::Serialize(Archive, FilePath)
		&& FGLTFJsonBuilder::Serialize(Archive);
}
