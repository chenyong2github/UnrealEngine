// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "MeshPassProcessor.h"
#include "UnifiedBuffer.h"
#include "Rendering/NaniteResources.h"

class FNaniteMaterialCommands
{
public:
	FNaniteMaterialCommands();
	~FNaniteMaterialCommands();

private:
	FRWLock ReadWriteLock;
	FStateBucketMap StateBuckets;
};
