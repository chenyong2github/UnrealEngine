// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SourceMeshDataForDerivedDataTask.h
=============================================================================*/

#pragma once

#include "Components.h"
#include "StaticMeshResources.h"

/** Source mesh data. Valid only in specific cases, when StaticMesh doesn't contain original data anymore (e.g. it's replaced by the Nanite coarse mesh representation) */
class FSourceMeshDataForDerivedDataTask
{
public:
	TArray<FStaticMeshBuildVertex> Vertices;
	TArray<uint32> Indices;
	FStaticMeshSectionArray Sections;

	bool IsValid() const
	{ 
		return Sections.Num() > 0;
	}
};