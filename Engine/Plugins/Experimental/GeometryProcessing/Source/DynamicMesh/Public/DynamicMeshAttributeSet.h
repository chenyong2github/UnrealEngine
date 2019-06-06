// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "DynamicMeshOverlay.h"

/** Standard UV overlay type - 2-element float */
typedef TDynamicMeshVectorOverlay<float, 2, FVector2f> FDynamicMeshUVOverlay;
/** Standard Normal overlay type - 3-element float */
typedef TDynamicMeshVectorOverlay<float, 3, FVector3f> FDynamicMeshNormalOverlay;


/**
 * FDynamicMeshAttributeSet manages a set of extended attributes for a FDynamicMesh3.
 * This includes UV and Normal overlays, etc.
 * 
 * Currently the default is to always have one UV layer and one Normal layer
 * 
 * @todo current internal structure is a work-in-progress
 */
class DYNAMICMESH_API FDynamicMeshAttributeSet
{
public:

	FDynamicMeshAttributeSet(FDynamicMesh3* Mesh) 
		: ParentMesh(Mesh), UV0(Mesh), Normals0(Mesh)
	{
		UVLayers.Add(&UV0);
		NormalLayers.Add(&Normals0);
	}

	virtual ~FDynamicMeshAttributeSet()
	{
	}

	void Copy(const FDynamicMeshAttributeSet& Copy)
	{
		UV0.Copy(Copy.UV0);
		Normals0.Copy(Copy.Normals0);
		// parent mesh is *not* copied!
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }


	/** @return number of UV layers */
	virtual int NumUVLayers() const 
	{
		return 1;
	}

	/** @return number of Normals layers */
	virtual int NumNormalLayers() const
	{
		return 1;
	}

	/** @return true if the given edge is a seam edge in any overlay */
	virtual bool IsSeamEdge(int EdgeID) const;

	
	//
	// UV Layers 
	//


	/** @return the UV layer at the given Index */
	FDynamicMeshUVOverlay* GetUVLayer(int Index)
	{
		return (Index == 0) ? &UV0 : nullptr;
	}

	/** @return the UV layer at the given Index */
	const FDynamicMeshUVOverlay* GetUVLayer(int Index) const
	{
		return (Index == 0) ? &UV0 : nullptr;
	}

	/** @return list of all UV layers */
	const TArray<FDynamicMeshUVOverlay*>& GetAllUVLayers() const
	{
		return UVLayers;
	}

	/** @return the primary UV layer (layer 0) */
	FDynamicMeshUVOverlay* PrimaryUV() 
	{
		return &UV0;
	}
	/** @return the primary UV layer (layer 0) */
	const FDynamicMeshUVOverlay* PrimaryUV() const
	{
		return &UV0;
	}


	//
	// Normal Layers 
	//

	/** @return the Normal layer at the given Index */
	FDynamicMeshNormalOverlay* GetNormalLayer(int Index)
	{
		return (Index == 0) ? &Normals0 : nullptr;
	}

	/** @return the Normal layer at the given Index */
	const FDynamicMeshNormalOverlay* GetNormalLayer(int Index) const
	{
		return (Index == 0) ? &Normals0 : nullptr;
	}

	/** @return list of all Normal layers */
	const TArray<FDynamicMeshNormalOverlay*>& GetAllNormalLayers() const
	{
		return NormalLayers;
	}

	/** @return the primary Normal layer (layer 0) */
	FDynamicMeshNormalOverlay* PrimaryNormals()
	{
		return &Normals0;
	}
	/** @return the primary Normal layer (layer 0) */
	const FDynamicMeshNormalOverlay* PrimaryNormals() const
	{
		return &Normals0;
	}


protected:
	/** Parent mesh of this attribute set */
	FDynamicMesh3* ParentMesh;

	/** Default UV layer */
	FDynamicMeshUVOverlay UV0;
	/** Default Normals layer */
	FDynamicMeshNormalOverlay Normals0;

	TArray<FDynamicMeshUVOverlay*> UVLayers;
	TArray<FDynamicMeshNormalOverlay*> NormalLayers;


protected:
	friend class FDynamicMesh3;

	/**
	 * Initialize the existing attribute layers with the given vertex and triangle sizes
	 */
	void Initialize(int MaxVertexID, int MaxTriangleID)
	{
		UV0.InitializeTriangles(MaxTriangleID);
		Normals0.InitializeTriangles(MaxTriangleID);
	}

	// These functions are called by the FDynamicMesh3 to update the various
	// attributes when the parent mesh topology has been modified.
	virtual void OnNewTriangle(int TriangleID, bool bInserted);
	virtual void OnRemoveTriangle(int TriangleID, bool bRemoveIsolatedVertices);
	virtual void OnReverseTriOrientation(int TriangleID);
	virtual void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo & splitInfo);
	virtual void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo & flipInfo);
	virtual void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo & collapseInfo);
	virtual void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo & pokeInfo);
	virtual void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo & mergeInfo);
};

