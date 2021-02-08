// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowGraph.h"
#include "DataTypes/NormalMapData.h"
#include "DataTypes/TextureImageData.h"
#include "DataTypes/CollisionGeometryData.h"
#include "DynamicMesh3.h"
#include "MeshTangents.h"

#include "MeshProcessingNodes/MeshSolidifyNode.h"
#include "MeshProcessingNodes/MeshVoxMorphologyNode.h"
#include "MeshProcessingNodes/MeshSimplifyNode.h"
#include "MeshProcessingNodes/MeshDeleteTrianglesNode.h"
#include "MeshProcessingNodes/MeshAutoGenerateUVsNode.h"
#include "DataTypes/MeshImageBakingData.h"
#include "PhysicsNodes/GenerateSimpleCollisionNode.h"



struct FMeshLODGraphPreFilterSettings
{
	FName FilterGroupLayerName = FName( "PreFilterGroups" );
};


class FGenerateMeshLODGraph
{
public:
	void BuildGraph();

	int32 AppendTextureBakeNode(const TImageBuilder<FVector4f>& SourceImage, const FString& Identifier);

	void SetSourceMesh(const FDynamicMesh3& SourceMesh);

	void UpdatePreFilterSettings(const FMeshLODGraphPreFilterSettings& PreFilterSettings);
	const FMeshLODGraphPreFilterSettings& GetCurrentPreFilterSettings() const { return CurrentPreFilterSettings; }

	void UpdateSolidifySettings(const UE::GeometryFlow::FMeshSolidifySettings& SolidifySettings);
	const UE::GeometryFlow::FMeshSolidifySettings& GetCurrentSolidifySettings() const { return CurrentSolidifySettings; }

	void UpdateMorphologySettings(const UE::GeometryFlow::FVoxClosureSettings& MorphologySettings);
	const UE::GeometryFlow::FVoxClosureSettings& GetCurrentMorphologySettings() const { return CurrentMorphologySettings; }

	void UpdateSimplifySettings(const UE::GeometryFlow::FMeshSimplifySettings& SimplifySettings);
	const UE::GeometryFlow::FMeshSimplifySettings& GetCurrentSimplifySettings() const { return CurrentSimplifySettings; }

	void UpdateAutoUVSettings(const UE::GeometryFlow::FMeshAutoGenerateUVsSettings& AutoUVSettings);
	const UE::GeometryFlow::FMeshAutoGenerateUVsSettings& GetCurrentAutoUVSettings() const { return CurrentAutoUVSettings; }

	void UpdateBakeCacheSettings(const UE::GeometryFlow::FMeshMakeBakingCacheSettings& BakeCacheSettings);
	const UE::GeometryFlow::FMeshMakeBakingCacheSettings& GetCurrentBakeCacheSettings() const { return CurrentBakeCacheSettings; }

	void UpdateGenerateSimpleCollisionSettings(const UE::GeometryFlow::FGenerateSimpleCollisionSettings& SimpleCollisionSettings);
	const UE::GeometryFlow::FGenerateSimpleCollisionSettings& GetCurrentGenerateSimpleCollisionSettings() const { return CurrentGenerateSimpleCollisionSettings; }

	void UpdateCollisionGroupLayerName(const FName& CollisionGroupLayerName);



	void EvaluateResult(
		FDynamicMesh3& ResultMesh,
		FMeshTangentsd& ResultTangents,
		FSimpleShapeSet3d& ResultCollision,
		UE::GeometryFlow::FNormalMapImage& NormalMap,
		TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages,
		FProgressCancel* Progress);

	void EvaluateResultParallel(FDynamicMesh3& ResultMesh,
								FMeshTangentsd& ResultTangents,
								FSimpleShapeSet3d& ResultCollision,
								UE::GeometryFlow::FNormalMapImage& NormalMap,
								TArray<TUniquePtr<UE::GeometryFlow::FTextureImage>>& TextureImages,
								FProgressCancel* Progress);

protected:
	TUniquePtr<UE::GeometryFlow::FGraph> Graph;


	UE::GeometryFlow::FGraph::FHandle MeshSourceNode;

	UE::GeometryFlow::FGraph::FHandle FilterGroupsLayerNameNode;	// FNameSourceNode that defines name of polygroups layer
	UE::GeometryFlow::FGraph::FHandle FilterTrianglesNode;
	FMeshLODGraphPreFilterSettings CurrentPreFilterSettings;

	UE::GeometryFlow::FGraph::FHandle SolidifyNode;
	UE::GeometryFlow::FGraph::FHandle SolidifySettingsNode;
	UE::GeometryFlow::FMeshSolidifySettings CurrentSolidifySettings;

	UE::GeometryFlow::FGraph::FHandle MorphologyNode;
	UE::GeometryFlow::FGraph::FHandle MorphologySettingsNode;
	UE::GeometryFlow::FVoxClosureSettings CurrentMorphologySettings;

	UE::GeometryFlow::FGraph::FHandle SimplifyNode;
	UE::GeometryFlow::FGraph::FHandle SimplifySettingsNode;
	UE::GeometryFlow::FMeshSimplifySettings CurrentSimplifySettings;

	UE::GeometryFlow::FGraph::FHandle NormalsNode;
	UE::GeometryFlow::FGraph::FHandle NormalsSettingsNode;

	UE::GeometryFlow::FGraph::FHandle AutoUVNode;
	UE::GeometryFlow::FGraph::FHandle AutoUVSettingsNode;
	UE::GeometryFlow::FMeshAutoGenerateUVsSettings CurrentAutoUVSettings;

	UE::GeometryFlow::FGraph::FHandle RecomputeUVNode;
	UE::GeometryFlow::FGraph::FHandle RecomputeUVSettingsNode;

	UE::GeometryFlow::FGraph::FHandle RepackUVNode;
	UE::GeometryFlow::FGraph::FHandle RepackUVSettingsNode;

	UE::GeometryFlow::FGraph::FHandle TangentsNode;
	UE::GeometryFlow::FGraph::FHandle TangentsSettingsNode;

	UE::GeometryFlow::FGraph::FHandle BakeCacheNode;
	UE::GeometryFlow::FGraph::FHandle BakeCacheSettingsNode;
	UE::GeometryFlow::FMeshMakeBakingCacheSettings CurrentBakeCacheSettings;

	UE::GeometryFlow::FGraph::FHandle BakeNormalMapNode;
	UE::GeometryFlow::FGraph::FHandle BakeNormalMapSettingsNode;

	UE::GeometryFlow::FGraph::FHandle GroupLayerNameNode;

	struct FBakeTextureGraphInfo
	{
		int32 Index;
		FString Identifier;
		UE::GeometryFlow::FGraph::FHandle TexSourceNode;
		UE::GeometryFlow::FGraph::FHandle BakeNode;
	};
	TArray<FBakeTextureGraphInfo> BakeTextureNodes;

	UE::GeometryFlow::FGraph::FHandle DecomposeMeshForCollisionNode;
	
	UE::GeometryFlow::FGraph::FHandle GenerateSimpleCollisionNode;
	UE::GeometryFlow::FGraph::FHandle GenerateSimpleCollisionSettingsNode;
	UE::GeometryFlow::FGenerateSimpleCollisionSettings CurrentGenerateSimpleCollisionSettings;

	UE::GeometryFlow::FGraph::FHandle CollisionOutputNode;
	UE::GeometryFlow::FGraph::FHandle MeshOutputNode;
	UE::GeometryFlow::FGraph::FHandle TangentsOutputNode;

	FName CollisionGroupLayerName = TEXT("Default");
	
};
