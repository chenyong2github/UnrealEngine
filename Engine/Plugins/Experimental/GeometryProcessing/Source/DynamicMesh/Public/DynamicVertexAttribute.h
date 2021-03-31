// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicAttribute.h"


template<typename AttribValueType, int AttribDimension, typename ParentType>
class TDynamicVertexAttribute;


template<typename AttribValueType, int AttribDimension, typename ParentType>
class TDynamicVertexAttributeChange : public TDynamicAttributeChangeBase<ParentType>
{
private:
	struct FChangeVertexAttribute
	{
		AttribValueType Data[AttribDimension];
		int VertexID;
	};

	TArray<FChangeVertexAttribute> OldVertexAttributes, NewVertexAttributes;

public:
	TDynamicVertexAttributeChange()
	{}

	virtual ~TDynamicVertexAttributeChange()
	{}

	inline virtual void SaveInitialVertex(const TDynamicAttributeBase<ParentType>* Attribute, int VertexID) override;

	inline virtual void StoreAllFinalVertices(const TDynamicAttributeBase<ParentType>* Attribute, const TArray<int>& VertexIDs) override;

	inline virtual bool Apply(TDynamicAttributeBase<ParentType>* Attribute, bool bRevert) const override;
};


/**
 * TDynamicVertexAttribute provides per-vertex storage of an attribute value
 */
template<typename AttribValueType, int AttribDimension, typename ParentType>
class TDynamicVertexAttribute : public TDynamicAttributeBase<ParentType>
{

protected:
	/** The parent object (e.g. mesh, point set) this attribute belongs to */
	ParentType* Parent;

	/** List of per-vertex attribute values */
	TDynamicVector<AttribValueType> AttribValues;

public:
	/** Create an empty overlay */
	TDynamicVertexAttribute() : Parent(nullptr)
	{
	}

	/** Create an attribute for the given parent */
	TDynamicVertexAttribute(ParentType* ParentIn, bool bAutoInit = true) : Parent(ParentIn)
	{
		if (bAutoInit)
		{
			Initialize();
		}
	}

	virtual ~TDynamicVertexAttribute()
	{
	}

	/** @return the parent for this attribute */
	const ParentType* GetParent() const { return Parent; }
	/** @return the parent for this attribute */
	ParentType* GetParent() { return Parent; }
private:
	void Reparent(ParentType* NewParent) override { Parent = NewParent;  }
public:
	virtual TDynamicAttributeBase<ParentType>* MakeNew(ParentType* ParentIn) const override
	{
		TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* Matching = new TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>(ParentIn);
		Matching->Initialize();
		return Matching;
	}
	virtual TDynamicAttributeBase<ParentType>* MakeCopy(ParentType* ParentIn) const override
	{
		TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* ToFill = new TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>(ParentIn);
		ToFill->Copy(*this);
		return ToFill;
	}

	/** Set this overlay to contain the same arrays as the copy overlay */
	void Copy(const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>& Copy)
	{
		TDynamicAttributeBase<ParentType>::CopyParentClassData(Copy);
		AttribValues = Copy.AttribValues;
	}

	virtual TDynamicAttributeBase<ParentType>* MakeCompactCopy(const FCompactMaps& CompactMaps, ParentType* ParentTypeIn) const override
	{
		TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* ToFill = new TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>(ParentTypeIn);
		ToFill->Initialize();
		ToFill->CompactCopy(CompactMaps, *this);
		return ToFill;
	}

	void CompactInPlace(const FCompactMaps& CompactMaps)
	{
		for (int VID = 0; VID < CompactMaps.MapV.Num(); VID++)
		{
			int ToVID = CompactMaps.MapV[VID];
			if (ToVID < 0)
			{
				continue;
			}
			check(ToVID <= VID);
			CopyValue(VID, ToVID);
		}
		AttribValues.Resize(Parent->MaxVertexID() * AttribDimension);
	}

	void CompactCopy(const FCompactMaps& CompactMaps, const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>& ToCopy)
	{
		TDynamicAttributeBase<ParentType>::CopyParentClassData(ToCopy);
		check(CompactMaps.MapV.Num() >= 0 && static_cast<size_t>(CompactMaps.MapV.Num()) <= AttribValues.Num() / AttribDimension);

		AttribValueType Data[AttribDimension];
		for (int VID = 0; VID < CompactMaps.MapV.Num(); VID++)
		{
			int ToVID = CompactMaps.MapV[VID];
			if (ToVID == -1)
			{
				continue;
			}
			ToCopy.GetValue(VID, Data);
			SetValue(ToVID, Data);
		}
	}

	/** Initialize the attribute values to the given max triangle ID */
	void Initialize(AttribValueType InitialValue = (AttribValueType)0)
	{
		check(Parent != nullptr);
		AttribValues.Resize(0);
		AttribValues.Resize( Parent->MaxVertexID() * AttribDimension, InitialValue );
	}

	void SetNewValue(int NewVertexID, const AttribValueType* Data)
	{
		int k = NewVertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(Data[i], k + i);
		}
	}



	//
	// Accessors/Queries
	//

	virtual bool CopyThroughMapping(const TDynamicAttributeBase<ParentType>* Source, const FMeshIndexMappings& Mapping) override
	{
		AttribValueType BufferData[AttribDimension];
		int BufferSize = sizeof(BufferData);
		for (const TPair<int32, int32>& MapVID : Mapping.GetVertexMap().GetForwardMap())
		{
			if (!ensure(Source->CopyOut(MapVID.Key, BufferData, BufferSize)))
			{
				return false;
			}
			SetValue(MapVID.Value, BufferData);
		}
		return true;
	}
	virtual bool CopyOut(int RawID, void* Buffer, int BufferSize) const override
	{
		if (sizeof(AttribValueType)*AttribDimension != BufferSize)
		{
			return false;
		}
		AttribValueType* BufferData = static_cast<AttribValueType*>(Buffer);
		int k = RawID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			BufferData[i] = AttribValues[k + i];
		}
		return true;
	}
	virtual bool CopyIn(int RawID, void* Buffer, int BufferSize) override
	{
		if (sizeof(AttribValueType) * AttribDimension != BufferSize)
		{
			return false;
		}
		AttribValueType* BufferData = static_cast<AttribValueType*>(Buffer);
		int k = RawID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = BufferData[i];
		}
		return true;
	}

	/** Get the element at a given index */
	inline void GetValue(int VertexID, AttribValueType* Data) const
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}

	/** Get the element at a given index */
	template<typename AsType>
	void GetValue(int VertexID, AsType& Data) const
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			Data[i] = AttribValues[k + i];
		}
	}


	/** Set the element at a given index */
	inline void SetValue(int VertexID, const AttribValueType* Data)
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/** Set the element at a given index */
	template<typename AsType>
	void SetValue(int VertexID, const AsType& Data)
	{
		int k = VertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[k + i] = Data[i];
		}
	}

	/**
	 * Copy the attribute value at FromVertexID to ToVertexID
	 */
	inline void CopyValue(int FromVertexID, int ToVertexID)
	{
		int kA = FromVertexID * AttribDimension;
		int kB = ToVertexID * AttribDimension;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues.InsertAt(AttribValues[kA+i], kB+i);
		}
	}


public:

	/** Update the overlay to reflect an edge split in the parent */
	void OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& SplitInfo) override
	{
		ResizeAttribStoreIfNeeded(SplitInfo.NewVertex);
		SetAttributeFromLerp(SplitInfo.NewVertex, SplitInfo.OriginalVertices.A, SplitInfo.OriginalVertices.B, SplitInfo.SplitT);
	}

	/** Update the overlay to reflect an edge flip in the parent */
	void OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo) override
	{
		// vertices unchanged
	}

	/** Update the overlay to reflect an edge collapse in the parent */
	void OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& CollapseInfo) override
	{
		SetAttributeFromLerp(CollapseInfo.KeptVertex, CollapseInfo.KeptVertex, CollapseInfo.RemovedVertex, CollapseInfo.CollapseT);
	}

	virtual AttribValueType GetDefaultAttributeValue()
	{
		return (AttribValueType)0;
	}

	inline void ResizeAttribStoreIfNeeded(int VertexID)
	{
		if (!ensure(VertexID >= 0))
		{
			return;
		}
		size_t NeededSize = (size_t(VertexID)+1) * AttribDimension;
		if (NeededSize > AttribValues.Num())
		{
			AttribValues.Resize(NeededSize, GetDefaultAttributeValue());
		}
	}

	virtual void OnNewVertex(int VertexID, bool bInserted) override
	{
		ResizeAttribStoreIfNeeded(VertexID);
	}

	/** Update the overlay to reflect a face poke in the parent */
	void OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo) override
	{
		FIndex3i Tri = PokeInfo.TriVertices;
		ResizeAttribStoreIfNeeded(PokeInfo.NewVertex);
		SetAttributeFromBary(PokeInfo.NewVertex, Tri.A, Tri.B, Tri.C, PokeInfo.BaryCoords);
	}

	/** Update the overlay to reflect an edge merge in the parent */
	void OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo) override
	{
		// just blend the attributes?
		if (MergeInfo.RemovedVerts.A != FDynamicMesh3::InvalidID)
		{
			SetAttributeFromLerp(MergeInfo.KeptVerts.A, MergeInfo.KeptVerts.A, MergeInfo.RemovedVerts.A, .5);
		}
		if (MergeInfo.RemovedVerts.B != FDynamicMesh3::InvalidID)
		{
			SetAttributeFromLerp(MergeInfo.KeptVerts.B, MergeInfo.KeptVerts.B, MergeInfo.RemovedVerts.B, .5);
		}
	}

	/** Update the overlay to reflect a vertex split in the parent */
	void OnSplitVertex(const FDynamicMesh3::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate) override
	{
		CopyValue(SplitInfo.OriginalVertex, SplitInfo.NewVertex);
	}

	virtual TUniquePtr<TDynamicAttributeChangeBase<ParentType>> NewBlankChange() override
	{
		return MakeUnique<TDynamicVertexAttributeChange<AttribValueType, AttribDimension, ParentType>>();
	}

	/**
	* Check validity of attribute
	* 
	* @param bAllowNonmanifold Accept non-manifold topology as valid. Note that this should almost always be true for attributes; non-manifold overlays are generally valid.
	* @param FailMode Desired behavior if mesh is found invalid
	*/
	virtual bool CheckValidity(bool bAllowNonmanifold, EValidityCheckFailMode FailMode) const override
	{
		// just check that the values buffer is big enough
		if (Parent->MaxVertexID() < 0 || static_cast<size_t>(Parent->MaxVertexID()*AttribDimension) > AttribValues.Num())
		{
			switch (FailMode)
			{
			case EValidityCheckFailMode::Check:
				check(false);
			case EValidityCheckFailMode::Ensure:
				ensure(false);
			default:
				return false;
			}
		}

		return true;
	}


protected:

	// interpolation functions; default implementation assumes your attributes can be interpolated as reals

	/** Set the value at an Attribute to be a linear interpolation of two other Attributes */
	virtual void SetAttributeFromLerp(int SetAttribute, int AttributeA, int AttributeB, double Alpha)
	{
		int IndexSet = AttribDimension * SetAttribute;
		int IndexA = AttribDimension * AttributeA;
		int IndexB = AttribDimension * AttributeB;
		AttribValueType Beta = ((AttribValueType)1 - Alpha);
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[IndexSet+i] = Beta*AttribValues[IndexA+i] + Alpha*AttribValues[IndexB+i];
		}
	}

	/** Set the value at an Attribute to be a barycentric interpolation of three other Attributes */
	virtual void SetAttributeFromBary(int SetAttribute, int AttributeA, int AttributeB, int AttributeC, const FVector3d& BaryCoords)
	{
		int IndexSet = AttribDimension * SetAttribute;
		int IndexA = AttribDimension * AttributeA;
		int IndexB = AttribDimension * AttributeB;
		int IndexC = AttribDimension * AttributeC;
		for (int i = 0; i < AttribDimension; ++i)
		{
			AttribValues[IndexSet + i] = 
				BaryCoords.X*AttribValues[IndexA+i] + BaryCoords.Y*AttribValues[IndexB+i] + BaryCoords.Z*AttribValues[IndexC+i];
		}
	}

};


template<typename AttribValueType, int AttribDimension>
using TDynamicMeshVertexAttribute = TDynamicVertexAttribute<AttribValueType, AttribDimension, FDynamicMesh3>;


template<typename AttribValueType, int AttribDimension, typename ParentType>
void TDynamicVertexAttributeChange<AttribValueType, AttribDimension, ParentType>::SaveInitialVertex(const TDynamicAttributeBase<ParentType>* Attribute, int VertexID)
{
	FChangeVertexAttribute& Change = OldVertexAttributes.Emplace_GetRef();
	Change.VertexID = VertexID;
	const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* AttribCast = static_cast<const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>*>(Attribute);
	AttribCast->GetValue(VertexID, Change.Data);
}

template<typename AttribValueType, int AttribDimension, typename ParentType>
void TDynamicVertexAttributeChange<AttribValueType, AttribDimension, ParentType>::StoreAllFinalVertices(const TDynamicAttributeBase<ParentType>* Attribute, const TArray<int>& VertexIDs)
{
	NewVertexAttributes.Reserve(NewVertexAttributes.Num() + VertexIDs.Num());
	const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* AttribCast = static_cast<const TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>*>(Attribute);
	for (int VertexID : VertexIDs)
	{
		FChangeVertexAttribute& Change = NewVertexAttributes.Emplace_GetRef();
		Change.VertexID = VertexID;
		AttribCast->GetValue(VertexID, Change.Data);
	}
}

template<typename AttribValueType, int AttribDimension, typename ParentType>
bool TDynamicVertexAttributeChange<AttribValueType, AttribDimension, ParentType>::Apply(TDynamicAttributeBase<ParentType>* Attribute, bool bRevert) const
{
	const TArray<FChangeVertexAttribute> *Changes = bRevert ? &OldVertexAttributes : &NewVertexAttributes;
	TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>* AttribCast = static_cast<TDynamicVertexAttribute<AttribValueType, AttribDimension, ParentType>*>(Attribute);
	for (const FChangeVertexAttribute& Change : *Changes)
	{
		check(AttribCast->GetParent()->IsVertex(Change.VertexID));
		AttribCast->SetValue(Change.VertexID, Change.Data);
	}
	return true;
}



