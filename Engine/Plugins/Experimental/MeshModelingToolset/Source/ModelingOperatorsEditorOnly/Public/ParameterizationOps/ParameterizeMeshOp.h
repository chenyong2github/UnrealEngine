// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"


struct FMeshDescription;


class MODELINGOPERATORSEDITORONLY_API FParameterizeMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FParameterizeMeshOp() {}

	//
	// Inputs
	// 

	// source mesh
	TSharedPtr<FMeshDescription> InputMesh;
		
	// UV generation parameters
	float Stretch;
	int32 NumCharts;

	// Atlas Packing parameters
	int32 Height = 512;
	int32 Width = 512;
	float Gutter = 2.5;

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
		FLinearMesh(const FDynamicMesh3& Mesh);

		// Stripped down mesh
		TArray<int32>   IndexBuffer;
		TArray<FVector> VertexBuffer;

		// Map from offset in the VertexBuffer to the VertexID in the FDynamicMesh
		TArray<int32> VertToID;

		// Adjacency[tri], Adjacency[tri+1], Adjacency[tri+2] 
		// are the three tris adjacent to tri.
		TArray<int32>   AdjacencyBuffer;
		
	};

	bool ComputeUVs(FDynamicMesh3& InOutMesh, TFunction<bool(float)>& Interrupter);
};
