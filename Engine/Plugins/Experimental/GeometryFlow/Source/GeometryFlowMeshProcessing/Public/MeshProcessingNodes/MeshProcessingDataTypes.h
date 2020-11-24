// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryFlowTypes.h"

namespace UE
{
namespace GeometryFlow
{

enum class EMeshProcessingDataTypes
{
	DynamicMesh = (int)EDataTypes::BaseMeshProcessingTypes + 1,
	MeshNormalSet = (int)EDataTypes::BaseMeshProcessingTypes + 2,
	MeshTangentSet = (int)EDataTypes::BaseMeshProcessingTypes + 3,

	BakingCache = (int)EDataTypes::BaseMeshProcessingTypes + 10,
	TextureImage = (int)EDataTypes::BaseMeshProcessingTypes + 11,
	NormalMapImage = (int)EDataTypes::BaseMeshProcessingTypes + 12,
	CollisionGeometry = (int)EDataTypes::BaseMeshProcessingTypes + 13,
	IndexSets = (int)EDataTypes::BaseMeshProcessingTypes + 14,


	SolidifySettings = (int)EDataTypes::BaseMeshProcessingTypes + 100,
	VoxMorphologyOpSettings = (int)EDataTypes::BaseMeshProcessingTypes + 101,

	SimplifySettings = (int)EDataTypes::BaseMeshProcessingTypes + 110,



	NormalsSettings = (int)EDataTypes::BaseMeshProcessingTypes + 120,
	TangentsSettings = (int)EDataTypes::BaseMeshProcessingTypes + 121,


	RecalculateUVsSettings = (int)EDataTypes::BaseMeshProcessingTypes + 150,
	RepackUVsSettings = (int)EDataTypes::BaseMeshProcessingTypes + 151,


	MakeBakingCacheSettings = (int)EDataTypes::BaseMeshProcessingTypes + 160,
	BakeNormalMapSettings = (int)EDataTypes::BaseMeshProcessingTypes + 161,
	BakeTextureImageSettings = (int)EDataTypes::BaseMeshProcessingTypes + 162,


	GenerateCollisionConvexHullsSettings = (int)EDataTypes::BaseMeshProcessingTypes + 200,
};



}	// end namespace GeometryFlow
}	// en