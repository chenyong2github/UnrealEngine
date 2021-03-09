// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"

using UE::Geometry::FDynamicMesh3;

class FFindPolygonsAlgorithm
{
	using FVector3d = UE::Geometry::FVector3d;
public:

	FFindPolygonsAlgorithm() {}
	FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn);

	FDynamicMesh3* Mesh = nullptr;
	TArray<TArray<int>> FoundPolygons;
	TArray<int> PolygonTags;
	TArray<FVector3d> PolygonNormals;

	TArray<int> PolygonEdges;

	bool FindPolygonsFromFaceNormals(double DotTolerance = 0.0001);
	bool FindPolygonsFromUVIslands();
	bool FindPolygonsFromConnectedTris();

	bool FindPolygonEdges();


protected:

	void SetGroupsFromPolygons();
};