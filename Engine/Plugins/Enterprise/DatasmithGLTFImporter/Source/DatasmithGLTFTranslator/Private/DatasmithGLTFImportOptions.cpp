// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithGLTFImportOptions.h"

#include "CoreTypes.h"

UDatasmithGLTFImportOptions::UDatasmithGLTFImportOptions(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bGenerateLightmapUVs(false)
    , ImportScale(100.f)
{
}
