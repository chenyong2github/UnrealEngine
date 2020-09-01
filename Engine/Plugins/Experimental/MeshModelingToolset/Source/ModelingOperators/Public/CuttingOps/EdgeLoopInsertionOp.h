// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelingOperators.h"
#include "Operations/GroupEdgeInserter.h"

class FProgressCancel;

class MODELINGOPERATORS_API FEdgeLoopInsertionOp : public FDynamicMeshOperator
{
public:
	virtual ~FEdgeLoopInsertionOp() {}

	// Inputs:
	TSharedPtr<const FDynamicMesh3> OriginalMesh;
	TSharedPtr<const FGroupTopology> OriginalTopology;
	FGroupEdgeInserter::EInsertionMode Mode;
	double VertexTolerance; // TODO: Add some defaults
	int32 GroupEdgeID = FDynamicMesh3::InvalidID;
	TArray<double> InputLengths;
	bool bInputsAreProportions = false;
	int32 StartCornerID = FDynamicMesh3::InvalidID;

	// Outputs:
	TSet<int32> LoopEids; // These are edge ID's in the ResultMesh.
	TSharedPtr<FGroupTopology> ResultTopology;
	bool bSucceeded = false;

	void SetTransform(const FTransform& Transform);

	/** 
	 * Converts LoopEids into pairs of endpoints, since ResultMesh is inaccessible without
	 * extracting it. Can be used to render the added loops.
	 * Clears EndPointPairsOut before use.
	 */
	void GetLoopEdgeLocations(TArray<TPair<FVector3d, FVector3d>>& EndPointPairsOut) const;

	// FDynamicMeshOperator implementation
	virtual void CalculateResult(FProgressCancel* Progress) override;
};