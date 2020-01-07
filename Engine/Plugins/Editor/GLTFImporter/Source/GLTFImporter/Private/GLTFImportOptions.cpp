// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFImportOptions.h"

#include "CoreTypes.h"

UGLTFImportOptions::UGLTFImportOptions(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bGenerateLightmapUVs(false)
    , ImportScale(100.f)
{
}
