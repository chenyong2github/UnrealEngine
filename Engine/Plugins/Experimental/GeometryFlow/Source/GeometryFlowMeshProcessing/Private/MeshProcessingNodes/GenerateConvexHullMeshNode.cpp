// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshProcessingNodes/GenerateConvexHullMeshNode.h"
#include "Operations/MeshConvexHull.h"
#include "Util/ProgressCancel.h"

using namespace UE::Geometry;
using namespace UE::GeometryFlow;

EGeometryFlowResult FGenerateConvexHullMeshNode::MakeConvexHullMesh(const FDynamicMesh3& MeshIn,
	const FGenerateConvexHullMeshSettings& Settings, 
	FDynamicMesh3& MeshOut)
{
	FMeshConvexHull Hull(&MeshIn);

	if (Settings.bPrefilterVertices)
	{
		FMeshConvexHull::GridSample(MeshIn, Settings.PrefilterGridResolution, Hull.VertexSet);
	}

	Hull.bPostSimplify = false;		// Mesh can be simplified later

	FProgressCancel* Progress = nullptr;	// TODO: Add ProgressCancel
	if (Hull.Compute(Progress))
	{
		MeshOut = MoveTemp(Hull.ConvexHull);
	}

	return EGeometryFlowResult::Ok;
}

