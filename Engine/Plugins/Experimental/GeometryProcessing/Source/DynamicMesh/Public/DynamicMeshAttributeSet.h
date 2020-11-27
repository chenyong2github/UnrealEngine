// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "DynamicMeshOverlay.h"
#include "DynamicMeshTriangleAttribute.h"
#include "DynamicAttribute.h"
#include "GeometryTypes.h"
#include "InfoTypes.h"

/** Standard UV overlay type - 2-element float */
typedef TDynamicMeshVectorOverlay<float, 2, FVector2f> FDynamicMeshUVOverlay;
/** Standard Normal overlay type - 3-element float */
typedef TDynamicMeshVectorOverlay<float, 3, FVector3f> FDynamicMeshNormalOverlay;

/** Standard per-triangle integer material ID */
typedef TDynamicMeshScalarTriangleAttribute<int32> FDynamicMeshMaterialAttribute;

/** Per-triangle integer polygroup ID */
typedef TDynamicMeshScalarTriangleAttribute<int32> FDynamicMeshPolygroupAttribute;

/**
 * FDynamicMeshAttributeSet manages a set of extended attributes for a FDynamicMesh3.
 * This includes UV and Normal overlays, etc.
 * 
 * Currently the default is to always have one UV layer and one Normal layer
 * 
 * @todo current internal structure is a work-in-progress
 */
class DYNAMICMESH_API FDynamicMeshAttributeSet : public FDynamicMeshAttributeSetBase
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

	void Copy(const FDynamicMeshAttributeSet& Copy);

	/** returns true if the attached overlays/attributes are compact */
	bool IsCompact();

	/**
	 * Performs a CompactCopy of the attached overlays/attributes.
	 * Called by the parent mesh CompactCopy function.
	 *
	 * @param CompactMaps Maps indicating how vertices and triangles were changes in the parent
	 * @param Copy The attribute set to be copied
	 */
	void CompactCopy(const FCompactMaps& CompactMaps, const FDynamicMeshAttributeSet& Copy);

	/**
	 * Compacts the attribute set in place
	 * Called by the parent mesh CompactInPlace function
	 *
	 * @param CompactMaps Maps of how the vertices and triangles were compacted in the parent
	 */
	void CompactInPlace(const FCompactMaps& CompactMaps);


	/**
	 * Enable the matching attributes and overlay layers as the reference Copy set, but do not copy any data across
	 */
	void EnableMatchingAttributes(const FDynamicMeshAttributeSet& ToMatch);

	/** @return the parent mesh for this overlay */
	const FDynamicMesh3* GetParentMesh() const { return ParentMesh; }
	/** @return the parent mesh for this overlay */
	FDynamicMesh3* GetParentMesh() { return ParentMesh; }

private:
	/** @set the parent mesh for this overlay.  Only safe for use during FDynamicMesh move */
	void Reparent(FDynamicMesh3* NewParent);

public:
	/** @return number of UV layers */
	virtual int NumUVLayers() const
	{
		return UVLayers.Num();
	}

	virtual void SetNumUVLayers(int Num);

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

	/** @return true if the given edge is a material ID boundary */
	virtual bool IsMaterialBoundaryEdge(int EdgeID) const;

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
	// Polygroup layers
	//

	/** @return number of Polygroup layers */
	virtual int32 NumPolygroupLayers() const;

	/** Set the number of Polygroup layers */
	virtual void SetNumPolygroupLayers(int32 Num);

	/** @return the Polygroup layer at the given Index */
	FDynamicMeshPolygroupAttribute* GetPolygroupLayer(int Index);

	/** @return the Polygroup layer at the given Index */
	const FDynamicMeshPolygroupAttribute* GetPolygroupLayer(int Index) const;




	//
	// Per-Triangle Material ID
	//

	bool HasMaterialID() const
	{
		return !!MaterialIDAttrib;
	}


	void EnableMaterialID();

	void DisableMaterialID();

	FDynamicMeshMaterialAttribute* GetMaterialID()
	{
		return MaterialIDAttrib.Get();
	}

	const FDynamicMeshMaterialAttribute* GetMaterialID() const
	{
		return MaterialIDAttrib.Get();
	}

	// Attach a new attribute (and transfer ownership of it to the attribute set)
	void AttachAttribute(FName AttribName, FDynamicMeshAttributeBase* Attribute)
	{
		if (GenericAttributes.Contains(AttribName))
		{
			UnregisterExternalAttribute(GenericAttributes[AttribName].Get());
		}
		GenericAttributes.Add(AttribName, TUniquePtr<FDynamicMeshAttributeBase>(Attribute));
		RegisterExternalAttribute(Attribute);
	}

	void RemoveAttribute(FName AttribName)
	{
		if (GenericAttributes.Contains(AttribName))
		{
			UnregisterExternalAttribute(GenericAttributes[AttribName].Get());
			GenericAttributes.Remove(AttribName);
		}
	}

	FDynamicMeshAttributeBase* GetAttachedAttribute(FName AttribName) const
	{
		return GenericAttributes[AttribName].Get();
	}

	int NumAttachedAttributes() const
	{
		return GenericAttributes.Num();
	}

	bool HasAttachedAttribute(FName AttribName) const
	{
		return GenericAttributes.Contains(AttribName);
	}

	const TMap<FName, TUniquePtr<FDynamicMeshAttributeBase>>& GetAttachedAttributes() const
	{
		return GenericAttributes;
	}

protected:
	/** Parent mesh of this attribute set */
	FDynamicMesh3* ParentMesh;

	/** Default Normals layer */
	FDynamicMeshNormalOverlay Normals0;

	TIndirectArray<FDynamicMeshUVOverlay> UVLayers;
	TArray<FDynamicMeshNormalOverlay*> NormalLayers;

	TUniquePtr<FDynamicMeshMaterialAttribute> MaterialIDAttrib;

	TIndirectArray<FDynamicMeshPolygroupAttribute> PolygroupLayers;

	TMap<FName, TUniquePtr<FDynamicMeshAttributeBase>> GenericAttributes;

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
	// TODO: would it be better to register all the overlays and attributes with the base set and not overload these?  maybe!
	virtual void OnNewTriangle(int TriangleID, bool bInserted);
	virtual void OnNewVertex(int VertexID, bool bInserted);
	virtual void OnRemoveTriangle(int TriangleID);
	virtual void OnRemoveVertex(int VertexID);
	virtual void OnReverseTriOrientation(int TriangleID);
	virtual void OnSplitEdge(const DynamicMeshInfo::FEdgeSplitInfo & splitInfo);
	virtual void OnFlipEdge(const DynamicMeshInfo::FEdgeFlipInfo & flipInfo);
	virtual void OnCollapseEdge(const DynamicMeshInfo::FEdgeCollapseInfo & collapseInfo);
	virtual void OnPokeTriangle(const DynamicMeshInfo::FPokeTriangleInfo & pokeInfo);
	virtual void OnMergeEdges(const DynamicMeshInfo::FMergeEdgesInfo & mergeInfo);
	virtual void OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate);

	/**
	 * Check validity of attributes
	 * 
	 * @param bAllowNonmanifold Accept non-manifold topology as valid. Note that this should almost always be true for attributes; non-manifold overlays are generally valid.
	 * @param FailMode Desired behavior if mesh is found invalid
	 */
	virtual bool CheckValidity(bool bAllowNonmanifold, EValidityCheckFailMode FailMode) const
	{
		bool bValid = FDynamicMeshAttributeSetBase::CheckValidity(bAllowNonmanifold, FailMode);
		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers(); UVLayerIndex++)
		{
			bValid = GetUVLayer(UVLayerIndex)->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		}
		bValid = PrimaryNormals()->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		if (MaterialIDAttrib)
		{
			bValid = MaterialIDAttrib->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		}
		for (int PolygroupLayerIndex = 0; PolygroupLayerIndex < NumPolygroupLayers(); PolygroupLayerIndex++)
		{
			bValid = GetPolygroupLayer(PolygroupLayerIndex)->CheckValidity(bAllowNonmanifold, FailMode) && bValid;
		}
		return bValid;
	}
};

