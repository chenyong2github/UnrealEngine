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

class UGeometryCacheComponent;

FHairStrandsProjectionMeshData::Section ConvertMeshSection(const FCachedGeometry::Section& In, const FTransform& T);

void BuildCacheGeometry(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	const USkeletalMeshComponent* SkeletalMeshComponent, 
	const bool bOutputTriangleData,
	FCachedGeometry& OutCachedGeometry);

void BuildCacheGeometry(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap, 
	const UGeometryCacheComponent* GeometryCacheComponent,
	const bool bOutputTriangleData,
	FCachedGeometry& OutCachedGeometry);