// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"

class FFindPolygonsAlgorithm
{
public:

	FFindPolygonsAlgorithm() {}
	FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn);

	FDynamicMesh3* Mesh = nullptr;
	TArray<int> PolygonGroupIDs;
	TArray<TArray<int>> FoundPolygons;
	TArray<int> PolygonTags;
	TArray<FVector> PolygonNormals;

	TArray<int> PolygonEdges;

	bool FindPolygons(double DotTolerance = 0.0001);

	bool FindPolygonEdges();
};