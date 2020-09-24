// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GroupTopology.h"
#include "ModelingOperators.h"
#include "Operations/GroupEdgeInserter.h"

class FProgressCancel;

class MODELINGOPERATORS_API FGroupEdgeInsertionOp : public FDynamicMeshOperator
{
public:
	virtual ~FGroupEdgeInsertionOp() {}

	// Inputs:
	TSharedPtr<const FDynamicMesh3> OriginalMesh;
	TSharedPtr<const FGroupTopology> OriginalTopology;
	FGroupEdgeInserter::EInsertionMode Mode;
	double VertexTolerance = KINDA_SMALL_NUMBER * 10;

	FGroupEdgeInserter::FGroupEdgeSplitPoint StartPoint;
	FGroupEdgeInserter::FGroupEdgeSplitPoint EndPoint;

	int32 CommonGroupID = FDynamicMesh3::InvalidID;
	int32 CommonBoundaryIndex = 0;

	// This overrides the behavior to revert to the original mesh when CalculateResult is called.
	bool bShowingBaseMesh = false;

	// Outputs:

	// Edge ID's in the ResultMesh that together make up the new group edge.
	TSet<int32> Eids; 

	TSharedPtr<FGroupTopology> ResultTopology;
	bool bSucceeded = false;

	void SetTransform(const FTransform& Transform);

	/** 
	 * Converts GroupEdgeEids into pairs of endpoints, since ResultMesh is inaccessible without
	 * extracting it. Can be used to render the added loops.
	 * Clears EndPointPairsOut before use.
	 */
	void GetEdgeLocations(TArray<TPair<FVector3d, FVector3d>>& EndPointPairsOut) const;

	// FDynamicMeshOperator implementation
	virtual void CalculateResult(FProgressCancel* Progress) override;
};