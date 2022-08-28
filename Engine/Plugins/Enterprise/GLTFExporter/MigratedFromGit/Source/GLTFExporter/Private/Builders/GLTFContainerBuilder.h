// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFConvertBuilder.h"

class FGLTFContainerBuilder : public FGLTFConvertBuilder
{
public:

	FGLTFContainerBuilder(const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly);

	bool Serialize(FArchive& Archive, const FString& FilePath);
};
