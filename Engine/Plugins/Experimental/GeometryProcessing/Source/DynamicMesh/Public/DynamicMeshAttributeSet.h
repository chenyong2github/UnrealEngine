// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "DynamicMeshOverlay.h"
#include "DynamicMeshTriangleAttribute.h"

/** Standard UV overlay type - 2-element float */
typedef TDynamicMeshVectorOverlay<float, 2, FVector2f> FDynamicMeshUVOverlay;
/** Standard Normal overlay type - 3-element float */
typedef TDynamicMeshVectorOverlay<float, 3, FVector3f> FDynamicMeshNormalOverlay;

/** Standard per-triangle integer material ID */
typedef TDynamicMeshTriangleAttribute<int32, 1> FDynamicMeshMaterialAttribute;

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
		: ParentMesh(Mesh), Normals0(Mesh)
	{
		SetNumUVLayers(1);
		NormalLayers.Add(&Normals0);
	}

	virtual ~FDynamicMeshAttributeSet()
	{
	}

	void Copy(const FDynamicMeshAttributeSet& Copy)
	{
		SetNumUVLayers(Copy.NumUVLayers());
		for (int UVIdx = 0; UVIdx < NumUVLayers(); UVIdx++)
		{
			UVLayers[UVIdx].Copy(Copy.UVLayers[UVIdx]);
		}
		Normals0.Copy(Copy.Normals0);

		if (Copy.MaterialIDAttrib)
		{
			EnableMaterialID();
			MaterialIDAttrib->Copy( *(Copy.MaterialIDAttrib) );
		}

		// parent mesh is *not* copied!
	}

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }


	/** @return number of UV layers */
	virtual int NumUVLayers() const 
	{
		return UVLayers.Num();
	}

	virtual void SetNumUVLayers(int Num)
	{
		if (UVLayers.Num() == Num)
		{
			return;
		}
		if (Num >= UVLayers.Num())
		{
			for (int i = (int)UVLayers.Num(); i < Num; ++i)
			{
				UVLayers.Add(new FDynamicMeshUVOverlay(ParentMesh));
			}
		}
		else
		{
			UVLayers.RemoveAt(Num, UVLayers.Num() - Num);
		}
		check(UVLayers.Num() == Num);
	}

	/** @return number of Normals layers */
	virtual int NumNormalLayers() const
	{
		checkSlow(NormalLayers.Num() == 1);
		return 1;
	}

	/** @return true if the given edge is a seam edge in any overlay */
	virtual bool IsSeamEdge(int EdgeID) const;

	/** @return true if the given vertex is a seam vertex in any overlay */
	virtual bool IsSeamVertex(int VertexID, bool bBoundaryIsSeam = true) const;

	
	//
	// UV Layers 
	//

	/** @return the UV layer at the given Index */
	FDynamicMeshUVOverlay* GetUVLayer(int Index)
	{
		return &UVLayers[Index];
	}

	/** @return the UV layer at the given Index */
	const FDynamicMeshUVOverlay* GetUVLayer(int Index) const
	{
		return &UVLayers[Index];
	}

	/** @return the primary UV layer (layer 0) */
	FDynamicMeshUVOverlay* PrimaryUV() 
	{
		return &UVLayers[0];
	}
	/** @return the primary UV layer (layer 0) */
	const FDynamicMeshUVOverlay* PrimaryUV() const
	{
		return &UVLayers[0];
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



	//
	// Per-Triangle Material ID
	//

	bool HasMaterialID() const
	{
		return !!MaterialIDAttrib;
	}


	void EnableMaterialID();

	FDynamicMeshMaterialAttribute* GetMaterialID()
	{
		return MaterialIDAttrib.Get();
	}

	const FDynamicMeshMaterialAttribute* GetMaterialID() const
	{
		return MaterialIDAttrib.Get();
	}

protected:
	/** Parent mesh of this attribute set */
	FDynamicMesh3* ParentMesh;

	/** Default Normals layer */
	FDynamicMeshNormalOverlay Normals0;

	TIndirectArray<FDynamicMeshUVOverlay> UVLayers;
	TArray<FDynamicMeshNormalOverlay*> NormalLayers;

	TUniquePtr<FDynamicMeshMaterialAttribute> MaterialIDAttrib;
	

protected:
	friend class FDynamicMesh3;

	/**
	 * Initialize the existing attribute layers with the given vertex and triangle sizes
	 */
	void Initialize(int MaxVertexID, int MaxTriangleID)
	{
		for (FDynamicMeshUVOverlay& UVLayer : UVLayers)
		{
			UVLayer.InitializeTriangles(MaxTriangleID);
		}
		Normals0.InitializeTriangles(MaxTriangleID);
	}

	// These functions are called by the FDynamicMesh3 to update the various
	// attributes when the parent mesh topology has been modified.
	virtual void OnNewTriangle(int TriangleID, bool bInserted);
	virtual void OnRemoveTriangle(int TriangleID);
	virtual void OnReverseTriOrientation(int TriangleID);
	virtual void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo & splitInfo);
	virtual void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo & flipInfo);
	virtual void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo & collapseInfo);
	virtual void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo & pokeInfo);
	virtual void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo & mergeInfo);
};

