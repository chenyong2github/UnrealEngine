// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "DynamicMeshAttributeSet.h"


enum class EParamOpUnwrapType
{
	MinStretch = 0,
	ExpMap = 1,
	ConformalFreeBoundary = 2
};



enum class EParamOpIslandMode
{
	Auto = 0,
	PolyGroups = 1,
	UVIslands = 2
};


class MODELINGOPERATORSEDITORONLY_API FParameterizeMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FParameterizeMeshOp() {}

	//
	// Inputs
	// 

	// source mesh
	TSharedPtr<FDynamicMesh3> InputMesh;
		
	// UV generation parameters
	float Stretch;
	int32 NumCharts;

	// area scaling
	bool bNormalizeAreas = true;
	float AreaScaling = 1.0;

	// UV layer
	int32 UVLayer = 0;

	// Atlas Packing parameters
	int32 Height = 512;
	int32 Width = 512;
	float Gutter = 2.5;

	EParamOpUnwrapType UnwrapType = EParamOpUnwrapType::ExpMap;
	EParamOpIslandMode IslandMode = EParamOpIslandMode::PolyGroups;

	// set ability on protected transform.
	void SetTransform(FTransform3d& XForm)
	{
		ResultTransform = XForm;
	}

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:

	// dense index/vertex buffer based representation of the data needed for parameterization
	struct FLinearMesh
	{
		FLinearMesh(const FDynamicMesh3& Mesh, const bool bRespectPolygroups);

		// Stripped down mesh
		TArray<int32>   IndexBuffer;
		TArray<FVector> VertexBuffer;

		// Map from offset in the VertexBuffer to the VertexID in the FDynamicMesh
		TArray<int32> VertToID;

		// Adjacency[tri], Adjacency[tri+1], Adjacency[tri+2] 
		// are the three tris adjacent to tri.
		TArray<int32>   AdjacencyBuffer;
		
	};

	bool ComputeUVs(FDynamicMesh3& InOutMesh,  TFunction<bool(float)>& Interrupter, const bool bUsePolygroups = false, float GlobalScale = 1.0f);
	bool ComputeUVs_ExpMap(FDynamicMesh3& InOutMesh, TFunction<bool(float)>& Interrupter, float GlobalScale = 1.0f);
	bool ComputeUVs_ConformalFreeBoundary(FDynamicMesh3& InOutMesh, TFunction<bool(float)>& Interrupter, float GlobalScale = 1.0f);

	void NormalizeUVAreas(const FDynamicMesh3& Mesh, FDynamicMeshUVOverlay* Overlay, float GlobalScale = 1.0f);
};
