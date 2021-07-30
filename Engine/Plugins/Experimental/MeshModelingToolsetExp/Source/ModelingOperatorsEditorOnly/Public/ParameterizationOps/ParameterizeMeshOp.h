// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"


namespace UE
{
namespace Geometry
{

enum class EParamOpBackend
{
	UVAtlas = 0,
	XAtlas = 1
};

class MODELINGOPERATORSEDITORONLY_API FParameterizeMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FParameterizeMeshOp() {}

	//
	// Inputs
	// 

	// source mesh
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;
		
	// UVAtlas generation parameters
	float Stretch = 0.11f;
	int32 NumCharts = 0;

	// XAtlas generation parameters
	int32 XAtlasMaxIterations = 1;

	// UV layer
	int32 UVLayer = 0;

	// Atlas Packing parameters
	int32 Height = 512;
	int32 Width = 512;
	float Gutter = 2.5;

	EParamOpBackend Method = EParamOpBackend::UVAtlas;

	// set ability on protected transform.
	void SetTransform(const FTransform3d& XForm)
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
		TArray<FVector3f> VertexBuffer;

		// Map from offset in the VertexBuffer to the VertexID in the FDynamicMesh
		TArray<int32> VertToID;

		// Adjacency[tri], Adjacency[tri+1], Adjacency[tri+2] 
		// are the three tris adjacent to tri.
		TArray<int32>   AdjacencyBuffer;
		
	};

	FGeometryResult NewResultInfo;

	bool ComputeUVs_UVAtlas(FDynamicMesh3& InOutMesh, TFunction<bool(float)>& Interrupter);
	bool ComputeUVs_XAtlas(FDynamicMesh3& InOutMesh, TFunction<bool(float)>& Interrupter);

	void CopyNewUVsToMesh(
		FDynamicMesh3& Mesh,
		const FLinearMesh& LinearMesh,
		const FDynamicMesh3& FlippedMesh,
		const TArray<FVector2D>& UVVertexBuffer,
		const TArray<int32>& UVIndexBuffer,
		const TArray<int32>& VertexRemapArray,
		bool bReverseOrientation);
};

} // end namespace UE::Geometry
} // end namespace UE
