// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowGraph.h"
#include "DataTypes/NormalMapData.h"
#include "DataTypes/TextureImageData.h"
#include "DataTypes/CollisionGeometryData.h"
#include "DynamicMesh3.h"
#include "MeshTangents.h"


class FGenerateMeshLODGraph
{
public:
	void BuildGraph();

	int32 AppendTextureBakeNode(const TImageBuilder<FVector4f>& SourceImage, const FString& Identifier);

	void SetSourceMesh(const FDynamicMesh3& SourceMesh);


	void EvaluateResult(
		FDynamicMesh3& ResultMesh,
		FMeshTangentsd& ResultTangents,
		FSimpleShapeSet3d& ResultCollision,
		UE::GeometryFlow::FNormalMapImage& NormalMap,
		TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages);

	void EvaluateResultParallel(FDynamicMesh3& ResultMesh,
						   FMeshTangentsd& ResultTangents,
						   FSimpleShapeSet3d& ResultCollision,
						   UE::GeometryFlow::FNormalMapImage& NormalMap,
						   TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages);

protected:
	TUniquePtr<UE::GeometryFlow::FGraph> Graph;


	UE::GeometryFlow::FGraph::FHandle MeshSourceNode;

	UE::GeometryFlow::FGraph::FHandle SolidifyNode;
	UE::GeometryFlow::FGraph::FHandle MorphologyNode;

	UE::GeometryFlow::FGraph::FHandle SimplifyNode;

	UE::GeometryFlow::FGraph::FHandle NormalsNode;

	UE::GeometryFlow::FGraph::FHandle AutoUVNode;
	UE::GeometryFlow::FGraph::FHandle RecomputeUVNode;
	UE::GeometryFlow::FGraph::FHandle RepackUVNode;

	UE::GeometryFlow::FGraph::FHandle TangentsNode;

	UE::GeometryFlow::FGraph::FHandle BakeCacheNode;
	UE::GeometryFlow::FGraph::FHandle BakeNormalMapNode;

	struct FBakeTextureGraphInfo
	{
		int32 Index;
		FString Identifier;
		UE::GeometryFlow::FGraph::FHandle TexSourceNode;
		UE::GeometryFlow::FGraph::FHandle BakeNode;
	};
	TArray<FBakeTextureGraphInfo> BakeTextureNodes;

	UE::GeometryFlow::FGraph::FHandle DecomposeMeshForCollisionNode;
	UE::GeometryFlow::FGraph::FHandle GenerateConvexesNode;

	UE::GeometryFlow::FGraph::FHandle CollisionOutputNode;
	UE::GeometryFlow::FGraph::FHandle MeshOutputNode;
	UE::GeometryFlow::FGraph::FHandle TangentsOutputNode;

};