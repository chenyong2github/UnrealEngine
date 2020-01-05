// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveToolChange.h"
#include "VectorTypes.h"
#include "MeshVertexChange.generated.h"

class FDynamicMesh3;

/**
 * FMeshVertexChange represents an reversible change to a set of vertex positions.
 * Currently only a USimpleDynamicMeshComponent target is supported.
 * 
 * @todo support optionally storing old/new normals and tangents
 * @todo support applying to a StaticMeshComponent/MeshDescription ?
 */
class MODELINGCOMPONENTS_API FMeshVertexChange : public FToolCommandChange
{
public:
	TArray<int> Vertices;
	TArray<FVector3d> OldPositions;
	TArray<FVector3d> NewPositions;


	/** Makes the change to the object */
	virtual void Apply(UObject* Object) override;

	/** Reverts change to the object */
	virtual void Revert(UObject* Object) override;

	/** Describes this change (for debugging) */
	virtual FString ToString() const override;
};



/**
 * FMeshVertexChangeBuilder can be used to construct a FMeshVertexChange.
 * Usage is to call UpdateVertex() each time a vertex moves, with the old and new positions.
 */
class MODELINGCOMPONENTS_API FMeshVertexChangeBuilder
{
public:
	TUniquePtr<FMeshVertexChange> Change;
	TMap<int, int> SavedVertices;

	FMeshVertexChangeBuilder();

	void UpdateVertex(int VertexID, const FVector3d& OldPosition, const FVector3d& NewPosition);
	void UpdateVertexFinal(int VertexID, const FVector3d& NewPosition);

	void SavePosition(const FDynamicMesh3* Mesh, int VertexID, bool bInitial);
	void SavePositions(const FDynamicMesh3* Mesh, const TArray<int>& VertexIDs, bool bInitial);
	void SavePositions(const FDynamicMesh3* Mesh, const TSet<int>& VertexIDs, bool bInitial);
};



UINTERFACE()
class MODELINGCOMPONENTS_API UMeshVertexCommandChangeTarget : public UInterface
{
	GENERATED_BODY()
};
/**
 * IMeshVertexCommandChangeTarget is an interface which is used to apply a FMeshVertexChange
 */
class MODELINGCOMPONENTS_API IMeshVertexCommandChangeTarget
{
	GENERATED_BODY()
public:
	virtual void ApplyChange(const FMeshVertexChange* Change, bool bRevert) = 0;
};

