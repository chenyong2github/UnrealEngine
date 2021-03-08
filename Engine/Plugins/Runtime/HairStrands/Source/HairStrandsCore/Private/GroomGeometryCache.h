// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Containers/Array.h"
#include "Engine/EngineTypes.h"
#include "Shader.h"
#include "RenderResource.h"
#include "RenderGraphResources.h"
#include "HairStrandsMeshProjection.h"
#include "GPUSkinCache.h"

FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometry::Section& In);

void BuildCacheGeometry(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	USkeletalMeshComponent* SkeletalMeshComponent, 
	FCachedGeometry& CachedGeometry);

void BuildCacheGeometry(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	class UGeometryCacheComponent* GeometryCacheComponent, 
	FCachedGeometry& CachedGeometry);