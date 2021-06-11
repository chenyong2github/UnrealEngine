// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/AABB.h"
#include "Chaos/AABBTreeDirtyGridUtils.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Templates/Models.h"
#include "Chaos/BoundingVolume.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(ChaosPhysicsTimers);

struct FAABBTreeCVars
{
	static int32 UpdateDirtyElementPayloadData;
	static FAutoConsoleVariableRef CVarUpdateDirtyElementPayloadData;
};

struct CHAOS_API FAABBTreeDirtyGridCVars
{
	static int32 DirtyElementGridCellSize;
	static FAutoConsoleVariableRef CVarDirtyElementGridCellSize;

	static int32 DirtyElementMaxGridCellQueryCount;
	static FAutoConsoleVariableRef CVarDirtyElementMaxGridCellQueryCount;

	static int32 DirtyElementMaxPhysicalSizeInCells;
	static FAutoConsoleVariableRef CVarDirtyElementMaxPhysicalSizeInCells;

	static int32 DirtyElementMaxCellCapacity;
	static FAutoConsoleVariableRef CVarDirtyElementMaxCellCapacity;
};

namespace Chaos
{

enum class EAABBQueryType
{
	Raycast,
	Sweep,
	Overlap
};

DECLARE_CYCLE_STAT(TEXT("AABBTreeGenerateTree"), STAT_AABBTreeGenerateTree, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeTimeSliceSetup"), STAT_AABBTreeTimeSliceSetup, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeInitialTimeSlice"), STAT_AABBTreeInitialTimeSlice, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeProgressTimeSlice"), STAT_AABBTreeProgressTimeSlice, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("AABBTreeGrowPhase"), STAT_AABBTreeGrowPhase, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("AABBTreeChildrenPhase"), STAT_AABBTreeChildrenPhase, STATGROUP_Chaos);

template <typename TQueryFastData, EAABBQueryType Query>
struct TAABBTreeIntersectionHelper
{
	static bool Intersects(const FVec3& Start, TQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const FAABB3& Bounds, const FAABB3& QueryBounds, const FVec3& QueryHalfExtents, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3])
	{
		check(false);
		return true;
	}
};

template<>
struct TAABBTreeIntersectionHelper<FQueryFastData, EAABBQueryType::Raycast>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const FVec3& Start, FQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const FAABB3& Bounds, const FAABB3& QueryBounds, const FVec3& QueryHalfExtents, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3])
	{
		return Bounds.RaycastFast(Start, Dir, InvDir, bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <>
struct TAABBTreeIntersectionHelper<FQueryFastData, EAABBQueryType::Sweep>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const FVec3& Start, FQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const FAABB3& Bounds, const FAABB3& QueryBounds, const FVec3& QueryHalfExtents, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3])
	{
		FAABB3 SweepBounds(Bounds.Min() - QueryHalfExtents, Bounds.Max() + QueryHalfExtents);
		return SweepBounds.RaycastFast(Start, Dir, InvDir, bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <>
struct TAABBTreeIntersectionHelper<FQueryFastDataVoid, EAABBQueryType::Overlap>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const FVec3& Start, FQueryFastDataVoid& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const FAABB3& Bounds, const FAABB3& QueryBounds, const FVec3& QueryHalfExtents, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3])
	{
		return QueryBounds.Intersects(Bounds);
	}
};

template <typename TPayloadType, bool bComputeBounds>
struct TBoundsWrapperHelper
{

};

template <typename TPayloadType>
struct TBoundsWrapperHelper<TPayloadType, true>
{
	void ComputeBounds(const TArray<TPayloadBoundsElement<TPayloadType, FReal>>& Elems)
	{
		Bounds = FAABB3::EmptyAABB();

		for (const auto& Elem : Elems)
		{
			Bounds.GrowToInclude(Elem.Bounds);
		}
	}

	const FAABB3& GetBounds() const { return Bounds; }

private:
	FAABB3 Bounds;
};

template <typename TPayloadType>
struct TBoundsWrapperHelper<TPayloadType, false>
{
	void ComputeBounds(const TArray<TPayloadBoundsElement<TPayloadType, FReal>>&)
	{
	}

	const FAABB3 GetBounds() const
	{
		return FAABB3::EmptyAABB();
	}
};

template <typename TPayloadType, bool bComputeBounds = true>
struct TAABBTreeLeafArray : public TBoundsWrapperHelper<TPayloadType, bComputeBounds>
{
	TAABBTreeLeafArray() {}
	//todo: avoid copy?
	TAABBTreeLeafArray(const TArray<TPayloadBoundsElement<TPayloadType, FReal>>& InElems)
		: Elems(InElems)
	{
		this->ComputeBounds(Elems);
	}

	void GatherElements(TArray<TPayloadBoundsElement<TPayloadType, FReal>>& OutElements)
	{
		OutElements.Append(Elems);
	}

	SIZE_T GetReserveCount() const
	{
		// Optimize for fewer memory allocations.
		return Elems.Num();
	}

	template <typename TSQVisitor, typename TQueryFastData>
	FORCEINLINE_DEBUGGABLE bool RaycastFast(const FVec3& Start, TQueryFastData& QueryFastData, TSQVisitor& Visitor, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		return RaycastSweepImp</*bSweep=*/false>(Start, QueryFastData, FVec3(), Visitor, Dir, InvDir, bParallel);
	}

	template <typename TSQVisitor, typename TQueryFastData>
	FORCEINLINE_DEBUGGABLE bool SweepFast(const FVec3& Start, TQueryFastData& QueryFastData, const FVec3& QueryHalfExtents, TSQVisitor& Visitor,
			const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		return RaycastSweepImp</*bSweep=*/true>(Start, QueryFastData, QueryHalfExtents, Visitor, Dir, InvDir, bParallel);
	}

	template <typename TSQVisitor>
	bool OverlapFast(const FAABB3& QueryBounds, TSQVisitor& Visitor) const
	{
		for (const auto& Elem : Elems)
		{
			if (PrePreFilterHelper(Elem.Payload, Visitor.GetQueryData()))
			{
				continue;
			}

			if (Elem.Bounds.Intersects(QueryBounds))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, Elem.Bounds);
				if (Visitor.VisitOverlap(VisitData) == false)
				{
					return false;
				}
			}
		}

		return true;
	}

	template <bool bSweep, typename TQueryFastData, typename TSQVisitor>
	FORCEINLINE_DEBUGGABLE bool RaycastSweepImp(const FVec3& Start, TQueryFastData& QueryFastData, const FVec3& QueryHalfExtents, TSQVisitor& Visitor, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		FVec3 TmpPosition;
		FReal TOI;
		for (const auto& Elem : Elems)
		{
			if (PrePreFilterHelper(Elem.Payload, Visitor.GetQueryData()))
			{
				continue;
			}

			const auto& InstanceBounds = Elem.Bounds;
			if (TAABBTreeIntersectionHelper<TQueryFastData, bSweep ? EAABBQueryType::Sweep :
					EAABBQueryType::Raycast>::Intersects(Start, QueryFastData, TOI, TmpPosition, InstanceBounds, FAABB3(), QueryHalfExtents, Dir, InvDir, bParallel))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = (bSweep && Visitor.VisitSweep(VisitData, QueryFastData)) || (!bSweep &&  Visitor.VisitRaycast(VisitData, QueryFastData));
				if (!bContinue)
				{
					return false;
				}
			}
		}

		return true;
	}

	void RemoveElement(TPayloadType Payload)
	{
		for (int32 Idx = 0; Idx < Elems.Num(); ++Idx)
		{
			if (Elems[Idx].Payload == Payload)
			{
				Elems.RemoveAtSwap(Idx);
				break;
			}
		}
	}

	void UpdateElement(const TPayloadType& Payload, const FAABB3& NewBounds, bool bHasBounds)
	{
		if (!bHasBounds)
			return;

		for (int32 Idx = 0; Idx < Elems.Num(); ++Idx)
		{
			if (Elems[Idx].Payload == Payload)
			{
				Elems[Idx].Bounds = NewBounds;
				break;
			}
		}
	}

	const FAABB3& GetBounds() const
	{
		return Bounds;
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Elems;
	}

	TArray<TPayloadBoundsElement<TPayloadType, FReal>> Elems;
	FAABB3 Bounds;
};

template <typename TPayloadType, bool bComputeBounds>
FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeLeafArray<TPayloadType, bComputeBounds>& LeafArray)
{
	LeafArray.Serialize(Ar);
	return Ar;
}

struct FAABBTreeNode
{
	FAABBTreeNode()
	{
		ChildrenBounds[0] = FAABB3();
		ChildrenBounds[1] = FAABB3();
	}
	FAABB3 ChildrenBounds[2];
	int32 ChildrenNodes[2] = { 0, 0 };
	bool bLeaf = false;

	void Serialize(FChaosArchive& Ar)
	{
		for (auto& Bounds : ChildrenBounds)
		{
			TBox<FReal, 3>::SerializeAsAABB(Ar, Bounds);
		}

		for (auto& Node : ChildrenNodes)
		{
			Ar << Node;
		}

		Ar << bLeaf;
	}
};

FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, FAABBTreeNode& Node)
{
	Node.Serialize(Ar);
	return Ar;
}

struct FAABBTreePayloadInfo
{
	int32 GlobalPayloadIdx;
	int32 DirtyPayloadIdx;
	int32 LeafIdx;
	int32 DirtyGridOverflowIdx;

	FAABBTreePayloadInfo(int32 InGlobalPayloadIdx = INDEX_NONE, int32 InDirtyIdx = INDEX_NONE, int32 InLeafIdx = INDEX_NONE, int32 InDirtyGridOverflowIdx = INDEX_NONE)
		: GlobalPayloadIdx(InGlobalPayloadIdx)
		, DirtyPayloadIdx(InDirtyIdx)
		, LeafIdx(InLeafIdx)
		, DirtyGridOverflowIdx(InDirtyGridOverflowIdx)
	{}

	void Serialize(FArchive& Ar)
	{
		Ar << GlobalPayloadIdx;
		Ar << DirtyPayloadIdx;
		Ar << LeafIdx;
		Ar << DirtyGridOverflowIdx;
	}
};

FORCEINLINE FArchive& operator<<(FArchive& Ar, FAABBTreePayloadInfo& PayloadInfo)
{
	PayloadInfo.Serialize(Ar);
	return Ar;
}

extern CHAOS_API int32 MaxDirtyElements;

struct CIsUpdatableElement
{
	template<typename ElementT>
	auto Requires(ElementT& InElem, const ElementT& InOtherElem) -> decltype(InElem.UpdateFrom(InOtherElem));
};

template<typename T, typename TEnableIf<!TModels<CIsUpdatableElement, T>::Value>::Type* = nullptr>
static void UpdateElementHelper(T& InElem, const T& InFrom)
{

}

template<typename T, typename TEnableIf<TModels<CIsUpdatableElement, T>::Value>::Type* = nullptr>
static void UpdateElementHelper(T& InElem, const T& InFrom)
{
	if(FAABBTreeCVars::UpdateDirtyElementPayloadData != 0)
	{
		InElem.UpdateFrom(InFrom);
	}
}

struct DirtyGridHashEntry
{
	DirtyGridHashEntry()
	{
		Index = 0;
		Count = 0;
	}

	DirtyGridHashEntry(const DirtyGridHashEntry& Other)
	{
		Index = Other.Index;
		Count = Other.Count;
	}

	int32 Index;  // Index into FlattenedCellArrayOfDirtyIndices
	int32 Count;  // Number of valid entries from Index in FlattenedCellArrayOfDirtyIndices
};

template <typename TPayloadType, typename TLeafType, bool bMutable = true>
class TAABBTree final : public ISpatialAcceleration<TPayloadType, FReal, 3>
{
public:
	using PayloadType = TPayloadType;
	static constexpr int D = 3;
	using TType = FReal;
	static constexpr FReal DefaultMaxPayloadBounds = 100000;
	static constexpr int32 DefaultMaxChildrenInLeaf = 12;
	static constexpr int32 DefaultMaxTreeDepth = 16;
	static constexpr int32 DefaultMaxNumToProcess = 0; // 0 special value for processing all without timeslicing
	static constexpr ESpatialAcceleration StaticType = TIsSame<TAABBTreeLeafArray<TPayloadType>, TLeafType>::Value ? ESpatialAcceleration::AABBTree : 
		(TIsSame<TBoundingVolume<TPayloadType>, TLeafType>::Value ? ESpatialAcceleration::AABBTreeBV : ESpatialAcceleration::Unknown);
	TAABBTree()
		: ISpatialAcceleration<TPayloadType, FReal, 3>(StaticType)
		, MaxChildrenInLeaf(DefaultMaxChildrenInLeaf)
		, MaxTreeDepth(DefaultMaxTreeDepth)
		, MaxPayloadBounds(DefaultMaxPayloadBounds)
		, MaxNumToProcess(DefaultMaxNumToProcess)
	{
		GetCVars();
	}

	virtual void Reset() override
	{
		Nodes.Reset();
		Leaves.Reset();
		DirtyElements.Reset();
		CellHashToFlatArray.Reset();
		FlattenedCellArrayOfDirtyIndices.Reset();
		DirtyElementsGridOverflow.Reset();
		GlobalPayloads.Reset();
		PayloadToInfo.Reset();

		NumProcessedThisSlice = 0;
		WorkStack.Reset();
		WorkPoolFreeList.Reset();
		WorkPool.Reset();
	}

	virtual void ProgressAsyncTimeSlicing(bool ForceBuildCompletion) override
	{
		SCOPE_CYCLE_COUNTER(STAT_AABBTreeProgressTimeSlice);
		// force is to stop time slicing and complete the rest of the build now
		if (ForceBuildCompletion)
		{
			MaxNumToProcess = 0;
		}

		// still has work to complete
		if (WorkStack.Num())
		{
			NumProcessedThisSlice = 0;
			SplitNode();
		}
	}

	template <typename TParticles>
	TAABBTree(const TParticles& Particles, int32 InMaxChildrenInLeaf = DefaultMaxChildrenInLeaf, int32 InMaxTreeDepth = DefaultMaxTreeDepth, FReal InMaxPayloadBounds = DefaultMaxPayloadBounds, int32 InMaxNumToProcess = DefaultMaxNumToProcess )
		: ISpatialAcceleration<TPayloadType, FReal, 3>(StaticType)
		, MaxChildrenInLeaf(InMaxChildrenInLeaf)
		, MaxTreeDepth(InMaxTreeDepth)
		, MaxPayloadBounds(InMaxPayloadBounds)
		, MaxNumToProcess(InMaxNumToProcess)

	{
		GenerateTree(Particles);
	}

	template <typename ParticleView>
	void Reinitialize(const ParticleView& Particles)
	{
		GenerateTree(Particles);
	}

	virtual TArray<TPayloadType> FindAllIntersections(const FAABB3& Box) const override { return FindAllIntersectionsImp(Box); }

	bool GetAsBoundsArray(TArray<FAABB3>& AllBounds, int32 NodeIdx, int32 ParentNode, FAABB3& Bounds)
	{
		if (Nodes[NodeIdx].bLeaf)
		{
			AllBounds.Add(Bounds);
			return false;
		}
		else
		{
			GetAsBoundsArray(AllBounds, Nodes[NodeIdx].ChildrenNodes[0], NodeIdx, Nodes[NodeIdx].ChildrenBounds[0]);
			GetAsBoundsArray(AllBounds, Nodes[NodeIdx].ChildrenNodes[1], NodeIdx, Nodes[NodeIdx].ChildrenBounds[0]);
		}
		return true;
	}

	virtual ~TAABBTree() {}

	void CopyFrom(const TAABBTree<TPayloadType, TLeafType, bMutable>& Other)
	{
		(*this) = Other;
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, FReal, 3>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, FReal, 3>>(new TAABBTree<TPayloadType, TLeafType, bMutable>(*this));
	}

	virtual void Raycast(const FVec3& Start, const FVec3& Dir, const FReal Length, ISpatialVisitor<TPayloadType, FReal>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, FReal> ProxyVisitor(Visitor);
		Raycast(Start, Dir, Length, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Raycast(const FVec3& Start, const FVec3& Dir, const FReal Length, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Raycast>(Start, QueryFastData, FVec3(), FAABB3(), Visitor, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel);
	}

	template <typename SQVisitor>
	bool RaycastFast(const FVec3& Start, FQueryFastData& CurData, SQVisitor& Visitor, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		return QueryImp<EAABBQueryType::Raycast>(Start, CurData, FVec3(), FAABB3(), Visitor, Dir, InvDir, bParallel);
	}

	void Sweep(const FVec3& Start, const FVec3& Dir, const FReal Length, const FVec3 QueryHalfExtents, ISpatialVisitor<TPayloadType, FReal>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, FReal> ProxyVisitor(Visitor);
		Sweep(Start, Dir, Length, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Sweep(const FVec3& Start, const FVec3& Dir, const FReal Length, const FVec3 QueryHalfExtents, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Sweep>(Start, QueryFastData, QueryHalfExtents, FAABB3(), Visitor, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel);
	}

	template <typename SQVisitor>
	bool SweepFast(const FVec3& Start, FQueryFastData& CurData, const FVec3 QueryHalfExtents, SQVisitor& Visitor, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		return QueryImp<EAABBQueryType::Sweep>(Start,CurData, QueryHalfExtents, FAABB3(), Visitor, Dir, InvDir, bParallel);
	}

	void Overlap(const FAABB3& QueryBounds, ISpatialVisitor<TPayloadType, FReal>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, FReal> ProxyVisitor(Visitor);
		Overlap(QueryBounds, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Overlap(const FAABB3& QueryBounds, SQVisitor& Visitor) const
	{
		OverlapFast(QueryBounds, Visitor);
	}

	template <typename SQVisitor>
	bool OverlapFast(const FAABB3& QueryBounds, SQVisitor& Visitor) const
	{
		//dummy variables to reuse templated path
		FQueryFastDataVoid VoidData;
		return QueryImp<EAABBQueryType::Overlap>(FVec3(), VoidData, FVec3(), QueryBounds, Visitor, VoidData.Dir, VoidData.InvDir, VoidData.bParallel);
	}

	// This is to make sure important parameters are not changed during inopportune times
	void GetCVars()
	{
		DirtyElementGridCellSize = (FReal) FAABBTreeDirtyGridCVars::DirtyElementGridCellSize;
		if (DirtyElementGridCellSize > SMALL_NUMBER)
		{
			DirtyElementGridCellSizeInv = 1.0f / DirtyElementGridCellSize;
		}
		else
		{
			DirtyElementGridCellSizeInv = 1.0f;
		}

		DirtyElementMaxGridCellQueryCount = FAABBTreeDirtyGridCVars::DirtyElementMaxGridCellQueryCount;
		DirtyElementMaxPhysicalSizeInCells = FAABBTreeDirtyGridCVars::DirtyElementMaxPhysicalSizeInCells;
		DirtyElementMaxCellCapacity = FAABBTreeDirtyGridCVars::DirtyElementMaxCellCapacity;
	}

	FORCEINLINE_DEBUGGABLE bool DirtyElementGridEnabled() const
	{
		return DirtyElementGridCellSize > 0.0f &&
			DirtyElementMaxGridCellQueryCount > 0 &&
			DirtyElementMaxPhysicalSizeInCells > 0 &&
			DirtyElementMaxCellCapacity > 0;
	}

	FORCEINLINE_DEBUGGABLE bool EnoughSpaceInGridCell(int32 Hash)
	{
		DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(Hash);
		if (HashEntry)
		{
			if (HashEntry->Count >= DirtyElementMaxCellCapacity) // Checking if we are at capacity
			{
				return false;
			}
		}

		return true;
	}

	// Returns true if there was enough space in the cell to add the new dirty element index and if the element is not already in the cell 
	//(The second condition should never be true for the current implementation)
	FORCEINLINE_DEBUGGABLE bool AddNewDirtyParticleIndexToGridCell(int32 Hash, int32 NewDirtyIndex)
	{
		DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(Hash);
		if (HashEntry)
		{
			if (HashEntry->Count < DirtyElementMaxCellCapacity && ensure(InsertValueIntoSortedSubArray(FlattenedCellArrayOfDirtyIndices, NewDirtyIndex, HashEntry->Index, HashEntry->Count)))
			{
				++(HashEntry->Count);
				return true;
			}
		}
		else
		{
			DirtyGridHashEntry& NewHashEntry = CellHashToFlatArray.Add(Hash);
			NewHashEntry.Index = FlattenedCellArrayOfDirtyIndices.Num(); // End of flat array
			NewHashEntry.Count = 1;
			FlattenedCellArrayOfDirtyIndices.AddUninitialized(DirtyElementMaxCellCapacity);
			FlattenedCellArrayOfDirtyIndices[NewHashEntry.Index] = NewDirtyIndex;
			return true;
		}
		return false;
	}

	// Returns true if the dirty particle was in the grid and successfully deleted
	FORCEINLINE_DEBUGGABLE bool DeleteDirtyParticleIndexFromGridCell(int32 Hash, int32 DirtyIndex)
	{
		DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(Hash);
		if (HashEntry && HashEntry->Count >= 1)
		{
			if (DeleteValueFromSortedSubArray(FlattenedCellArrayOfDirtyIndices, DirtyIndex, HashEntry->Index, HashEntry->Count))
			{
				--(HashEntry->Count);
				// Not deleting cell when it gets empty, it may get reused or will be deleted when the AABBTree is rebuilt
				return true;
			}
		}
		return false;
	}

	FORCEINLINE_DEBUGGABLE void DeleteDirtyParticleEverywhere(int32 DeleteDirtyParticleIdx, int32 DeleteDirtyGridOverflowIdx)
	{
		if (DeleteDirtyGridOverflowIdx == INDEX_NONE)
		{
			// Remove this element from the Grid
			DoForOverlappedCells(DirtyElements[DeleteDirtyParticleIdx].Bounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
				ensure(DeleteDirtyParticleIndexFromGridCell(Hash, DeleteDirtyParticleIdx));
				});
		}
		else
		{
			// remove element from the grid overflow
			ensure(DirtyElementsGridOverflow[DeleteDirtyGridOverflowIdx] == DeleteDirtyParticleIdx);

			if (DeleteDirtyGridOverflowIdx + 1 < DirtyElementsGridOverflow.Num())
			{
				auto LastOverflowPayload = DirtyElements[DirtyElementsGridOverflow.Last()].Payload;
				PayloadToInfo.FindChecked(LastOverflowPayload).DirtyGridOverflowIdx = DeleteDirtyGridOverflowIdx;
			}
			DirtyElementsGridOverflow.RemoveAtSwap(DeleteDirtyGridOverflowIdx);
		}

		if (DeleteDirtyParticleIdx + 1 < DirtyElements.Num())
		{
			// Now rename the last element in DirtyElements in both the grid and the overflow
			// So that it is correct after swapping Dirty elements in next step
			int32 LastDirtyElementIndex = DirtyElements.Num() - 1;
			auto LastDirtyPayload = DirtyElements[LastDirtyElementIndex].Payload;
			int32 LastDirtyGridOverflowIdx = PayloadToInfo.FindChecked(LastDirtyPayload).DirtyGridOverflowIdx;
			if (LastDirtyGridOverflowIdx == INDEX_NONE)
			{
				// Rename this element in the Grid
				DoForOverlappedCells(DirtyElements.Last().Bounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
					ensure(DeleteDirtyParticleIndexFromGridCell(Hash, LastDirtyElementIndex));
					ensure(AddNewDirtyParticleIndexToGridCell(Hash, DeleteDirtyParticleIdx));
					});
			}
			else
			{
				// Rename element in overflow instead
				DirtyElementsGridOverflow[LastDirtyGridOverflowIdx] = DeleteDirtyParticleIdx;
			}

			// Copy the Payload to the new index
			
			PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = DeleteDirtyParticleIdx;
		}
		DirtyElements.RemoveAtSwap(DeleteDirtyParticleIdx);
	}

	FORCEINLINE_DEBUGGABLE int32 AddDirtyElementToGrid(const TAABB<FReal, 3>& NewBounds, int32 NewDirtyElement)
	{
		bool bAddToGrid = !TooManyOverlapQueryCells(NewBounds, DirtyElementGridCellSizeInv, DirtyElementMaxPhysicalSizeInCells);
		if (bAddToGrid)
		{
			DoForOverlappedCells(NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
				if (!EnoughSpaceInGridCell(Hash))
				{
					bAddToGrid = false;
				}
				});
		}

		if (bAddToGrid)
		{
			DoForOverlappedCells(NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
				ensure(AddNewDirtyParticleIndexToGridCell(Hash, NewDirtyElement));
				});
		}
		else
		{
			int32 NewOverflowIndex = DirtyElementsGridOverflow.Add(NewDirtyElement);
			return NewOverflowIndex;
		}

		return INDEX_NONE;
	}

	FORCEINLINE_DEBUGGABLE int32 UpdateDirtyElementInGrid(const TAABB<FReal, 3>& NewBounds, int32 DirtyElementIndex, int32 DirtyGridOverflowIdx)
	{
		if (DirtyGridOverflowIdx == INDEX_NONE)
		{
			const TAABB<FReal, 3>& OldBounds = DirtyElements[DirtyElementIndex].Bounds;

			// Delete element in cells that are no longer overlapping
			DoForOverlappedCellsExclude(OldBounds, NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) -> bool {
				ensure(DeleteDirtyParticleIndexFromGridCell(Hash, DirtyElementIndex));
				return true;
				});

			// Add to new overlapped cells
			if (!DoForOverlappedCellsExclude(NewBounds, OldBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) -> bool {
					return AddNewDirtyParticleIndexToGridCell(Hash, DirtyElementIndex);
				}))
			{
				// Was not able to add it to the grid , so delete element from grid
				DoForOverlappedCells(NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
						DeleteDirtyParticleIndexFromGridCell(Hash, DirtyElementIndex);
					});
				// Add to overflow
				int32 NewOverflowIndex = DirtyElementsGridOverflow.Add(DirtyElementIndex);
				return NewOverflowIndex;
			}
		}
		return DirtyGridOverflowIdx;
	}

	virtual void RemoveElement(const TPayloadType& Payload)
	{
		if (ensure(bMutable))
		{
			if (FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload))
			{
				if (PayloadInfo->GlobalPayloadIdx != INDEX_NONE)
				{
					ensure(PayloadInfo->DirtyPayloadIdx == INDEX_NONE);
					ensure(PayloadInfo->DirtyGridOverflowIdx == INDEX_NONE);
					ensure(PayloadInfo->LeafIdx == INDEX_NONE);
					if (PayloadInfo->GlobalPayloadIdx + 1 < GlobalPayloads.Num())
					{
						auto LastGlobalPayload = GlobalPayloads.Last().Payload;
						PayloadToInfo.FindChecked(LastGlobalPayload).GlobalPayloadIdx = PayloadInfo->GlobalPayloadIdx;
					}
					GlobalPayloads.RemoveAtSwap(PayloadInfo->GlobalPayloadIdx);
				}
				else if (PayloadInfo->DirtyPayloadIdx != INDEX_NONE)
				{
					if (DirtyElementGridEnabled())
					{
						DeleteDirtyParticleEverywhere(PayloadInfo->DirtyPayloadIdx, PayloadInfo->DirtyGridOverflowIdx);
					}
					else
					{
						if (PayloadInfo->DirtyPayloadIdx + 1 < DirtyElements.Num())
						{
							auto LastDirtyPayload = DirtyElements.Last().Payload;
							PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = PayloadInfo->DirtyPayloadIdx;
						}
						DirtyElements.RemoveAtSwap(PayloadInfo->DirtyPayloadIdx);
					}
				}
				else if (ensure(PayloadInfo->LeafIdx != INDEX_NONE))
				{
					Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
				}

				PayloadToInfo.Remove(Payload);
			}
		}
	}

	virtual void UpdateElement(const TPayloadType& Payload, const FAABB3& NewBounds, bool bHasBounds) override
	{
		//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBTreeUpdateElement)
		if (ensure(bMutable))
		{
			FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload);
			if (PayloadInfo)
			{
				if (PayloadInfo->LeafIdx != INDEX_NONE)
				{
					//If we are still within the same leaf bounds, do nothing
					if (bHasBounds)
					{
						const FAABB3& LeafBounds = Leaves[PayloadInfo->LeafIdx].GetBounds();
						if (LeafBounds.Contains(NewBounds.Min()) && LeafBounds.Contains(NewBounds.Max()))
						{
							// We still need to update the constituent bounds
							Leaves[PayloadInfo->LeafIdx].UpdateElement(Payload, NewBounds, bHasBounds);
							return;
						}
					}

					Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
					PayloadInfo->LeafIdx = INDEX_NONE;
				}
			}
			else
			{
				PayloadInfo = &PayloadToInfo.Add(Payload);
			}

			bool bTooBig = false;
			if (bHasBounds)
			{
				if (NewBounds.Extents().Max() > MaxPayloadBounds)
				{
					bTooBig = true;
					bHasBounds = false;
				}
			}

			if (bHasBounds)
			{
				if (PayloadInfo->DirtyPayloadIdx == INDEX_NONE)
				{
					PayloadInfo->DirtyPayloadIdx = DirtyElements.Add(FElement{ Payload, NewBounds });
					if (DirtyElementGridEnabled())
					{
						//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBAddElement)
						PayloadInfo->DirtyGridOverflowIdx = AddDirtyElementToGrid(NewBounds, PayloadInfo->DirtyPayloadIdx);
					}
				}
				else
				{
					const int32 DirtyElementIndex = PayloadInfo->DirtyPayloadIdx;
					if (DirtyElementGridEnabled())
					{
						//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBUpElement)
						PayloadInfo->DirtyGridOverflowIdx = UpdateDirtyElementInGrid(NewBounds, DirtyElementIndex, PayloadInfo->DirtyGridOverflowIdx);
					}
					DirtyElements[DirtyElementIndex].Bounds = NewBounds;
					UpdateElementHelper(DirtyElements[DirtyElementIndex].Payload, Payload);
				}

				// Handle something that previously did not have bounds that may be in global elements.
				if (PayloadInfo->GlobalPayloadIdx != INDEX_NONE)
				{
					if (PayloadInfo->GlobalPayloadIdx + 1 < GlobalPayloads.Num())
					{
						auto LastGlobalPayload = GlobalPayloads.Last().Payload;
						PayloadToInfo.FindChecked(LastGlobalPayload).GlobalPayloadIdx = PayloadInfo->GlobalPayloadIdx;
					}
					GlobalPayloads.RemoveAtSwap(PayloadInfo->GlobalPayloadIdx);

					PayloadInfo->GlobalPayloadIdx = INDEX_NONE;
				}
			}
			else
			{
				FAABB3 GlobalBounds = bTooBig ? NewBounds : FAABB3(FVec3(TNumericLimits<FReal>::Lowest()), FVec3(TNumericLimits<FReal>::Max()));
				if (PayloadInfo->GlobalPayloadIdx == INDEX_NONE)
				{
					PayloadInfo->GlobalPayloadIdx = GlobalPayloads.Add(FElement{ Payload, GlobalBounds });
				}
				else
				{
					GlobalPayloads[PayloadInfo->GlobalPayloadIdx].Bounds = GlobalBounds;
					UpdateElementHelper(GlobalPayloads[PayloadInfo->GlobalPayloadIdx].Payload, Payload);
				}

				// Handle something that previously had bounds that may be in dirty elements.
				if (PayloadInfo->DirtyPayloadIdx != INDEX_NONE)
				{
					if (DirtyElementGridEnabled())
					{
						DeleteDirtyParticleEverywhere(PayloadInfo->DirtyPayloadIdx, PayloadInfo->DirtyGridOverflowIdx);
					}
					else
					{
						if (PayloadInfo->DirtyPayloadIdx + 1 < DirtyElements.Num())
						{
							auto LastDirtyPayload = DirtyElements.Last().Payload;
							PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = PayloadInfo->DirtyPayloadIdx;
						}
						DirtyElements.RemoveAtSwap(PayloadInfo->DirtyPayloadIdx);
					}

					PayloadInfo->DirtyPayloadIdx = INDEX_NONE;
					PayloadInfo->DirtyGridOverflowIdx = INDEX_NONE;
				}
			}
		}

		if(DirtyElements.Num() > MaxDirtyElements)
		{
			UE_LOG(LogChaos, Verbose, TEXT("Bounding volume exceeded maximum dirty elements (%d dirty of max %d) and is forcing a tree rebuild."), DirtyElements.Num(), MaxDirtyElements);
			ReoptimizeTree();
		}
	}

	int32 NumDirtyElements() const
	{
		return DirtyElements.Num();
	}

	const TArray<TPayloadBoundsElement<TPayloadType, FReal>>& GlobalObjects() const
	{
		return GlobalPayloads;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedAABBTreeFullBounds)
		{
			// Serialize out unused aabb for earlier versions
			FAABB3 Dummy(FVec3((FReal)0), FVec3((FReal)0));
			TBox<FReal, 3>::SerializeAsAABB(Ar, Dummy);
		}
		Ar << Nodes;
		Ar << Leaves;
		Ar << DirtyElements;
		Ar << GlobalPayloads;

		bool bSerializePayloadToInfo = !bMutable;
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::ImmutableAABBTree)
		{
			Ar << bSerializePayloadToInfo;
		}
		else
		{
			bSerializePayloadToInfo = true;
		}

		if (bSerializePayloadToInfo)
		{
			Ar << PayloadToInfo;

			if (!bMutable)	//if immutable empty this even if we had to serialize it in for backwards compat
			{
				PayloadToInfo.Empty();
			}
		}

		Ar << MaxChildrenInLeaf;
		Ar << MaxTreeDepth;
		Ar << MaxPayloadBounds;

		if (Ar.IsLoading())
		{
			// Disable the Grid until it is rebuilt
			DirtyElementGridCellSize = 0.0f;
			DirtyElementGridCellSizeInv = 1.0f;
		}
	}

private:

	using FElement = TPayloadBoundsElement<TPayloadType, FReal>;
	using FNode = FAABBTreeNode;

	void ReoptimizeTree()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TAABBTree::ReoptimizeTree);
		TArray<FElement> AllElements;

		int32 ReserveCount = DirtyElements.Num() + GlobalPayloads.Num();
		for (const auto& Leaf : Leaves)
		{
			ReserveCount += static_cast<int32>(Leaf.GetReserveCount());
		}

		AllElements.Reserve(ReserveCount);

		AllElements.Append(DirtyElements);
		AllElements.Append(GlobalPayloads);

		for (auto& Leaf : Leaves)
		{
			Leaf.GatherElements(AllElements);
		}

		TAABBTree<TPayloadType,TLeafType,bMutable> NewTree(AllElements);
		*this = NewTree;
	}

	// Returns true if the query should continue
	// Execute a function for all cells found in a query as well as the overflow 
	template <typename FunctionType>
	bool DoForHitGridCellsAndOverflow(TArray<DirtyGridHashEntry>& HashEntryForOverlappedCells, FunctionType Function) const
	{

		// Now merge and iterate the lists of elements found in the overlapping cells
		bool DoneWithGridElements = false;
		bool DoneWithNonGridElements = false;
		int NonGridElementIter = 0;
		while (!DoneWithGridElements || !DoneWithNonGridElements)
		{
			// Get the next dirty element index

			int32 SmallestDirtyParticleIndex = INT_MAX; // Best dirty particle index to find

			if (!DoneWithGridElements)
			{
				// Find the next smallest index 
				// This will start slowing down if we are overlapping a lot of cells
				DoneWithGridElements = true;
				for (const DirtyGridHashEntry& HashEntry : HashEntryForOverlappedCells)
				{
					int32 Count = HashEntry.Count;
					if (Count > 0)
					{
						int32 DirtyParticleIndex = FlattenedCellArrayOfDirtyIndices[HashEntry.Index];
						if (DirtyParticleIndex < SmallestDirtyParticleIndex)
						{
							SmallestDirtyParticleIndex = DirtyParticleIndex;
							DoneWithGridElements = false;
						}
					}
				}
			}

			// Now skip all elements with the same best index
			if (!DoneWithGridElements)
			{
				for (DirtyGridHashEntry& HashEntry : HashEntryForOverlappedCells)
				{
					int32 Count = HashEntry.Count;
					if (Count > 0)
					{
						int32 DirtyParticleIndex = FlattenedCellArrayOfDirtyIndices[HashEntry.Index];
						if (DirtyParticleIndex == SmallestDirtyParticleIndex)
						{
							++HashEntry.Index; // Increment Index
							--HashEntry.Count; // Decrement count
						}
					}
				}
			}

			DoneWithNonGridElements = NonGridElementIter >= DirtyElementsGridOverflow.Num();
			if (DoneWithGridElements && !DoneWithNonGridElements)
			{
				SmallestDirtyParticleIndex = DirtyElementsGridOverflow[NonGridElementIter];
				++NonGridElementIter;
			}

			// Elements that are in the overflow should not also be in the grid
			ensure(DoneWithGridElements || PayloadToInfo.Find(DirtyElements[SmallestDirtyParticleIndex].Payload)->DirtyGridOverflowIdx == INDEX_NONE);

			if ((!DoneWithGridElements || !DoneWithNonGridElements))
			{
				const int32 Index = SmallestDirtyParticleIndex;
				const auto& Elem = DirtyElements[Index];

				if (!Function(Elem))
				{
					return false;
				}
			}
		}
		return true;
	}
	

	template <EAABBQueryType Query, typename TQueryFastData, typename SQVisitor>
	bool QueryImp(const FVec3& RESTRICT Start, TQueryFastData& CurData, const FVec3 QueryHalfExtents, const FAABB3& QueryBounds, SQVisitor& Visitor, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3]) const
	{
		//QUICK_SCOPE_CYCLE_COUNTER(AABBTreeQueryImp);
		FVec3 TmpPosition;
		FReal TOI = 0;
		const void* QueryData = Visitor.GetQueryData();
		{
			//QUICK_SCOPE_CYCLE_COUNTER(QueryGlobal);

			for(const auto& Elem : GlobalPayloads)
			{
				if (PrePreFilterHelper(Elem.Payload, QueryData))
				{
					continue;
				}

				const auto& InstanceBounds = Elem.Bounds;
				if(TAABBTreeIntersectionHelper<TQueryFastData,Query>::Intersects(Start, CurData, TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents, Dir, InvDir, bParallel))
				{
					TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload,true);
					bool bContinue;
					if(Query == EAABBQueryType::Overlap)
					{
						bContinue = Visitor.VisitOverlap(VisitData);
					} else
					{
						bContinue = Query == EAABBQueryType::Sweep ? Visitor.VisitSweep(VisitData,CurData) : Visitor.VisitRaycast(VisitData,CurData);
					}

					if(!bContinue)
					{
						return false;
					}
				}
			}
		}

		if (bMutable)
		{	// Returns true if we should continue
			auto IntersectAndVisit = [&](const FElement& Elem) -> bool
			{
				const auto& InstanceBounds = Elem.Bounds;
				if (PrePreFilterHelper(Elem.Payload, QueryData))
				{
					return true;
				}

				if (TAABBTreeIntersectionHelper<TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents, Dir, InvDir, bParallel))
				{
					TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
					bool bContinue;
					if (Query == EAABBQueryType::Overlap)
					{
						bContinue = Visitor.VisitOverlap(VisitData);
					}
					else
					{
						bContinue = Query == EAABBQueryType::Sweep ? Visitor.VisitSweep(VisitData, CurData) : Visitor.VisitRaycast(VisitData, CurData);
					}

					if (!bContinue)
					{
						return false;
					}
				}
				return true;
			};

			//QUICK_SCOPE_CYCLE_COUNTER(QueryDirty);
			bool bUseGrid = false;

			if (DirtyElementGridEnabled())
			{
				if (Query == EAABBQueryType::Overlap)
				{
					bUseGrid = !TooManyOverlapQueryCells(QueryBounds, DirtyElementGridCellSizeInv, DirtyElementMaxGridCellQueryCount);
				}
				else if (Query == EAABBQueryType::Raycast)
				{
					bUseGrid = !TooManyRaycastQueryCells(Start, CurData.Dir, CurData.CurrentLength, DirtyElementGridCellSizeInv, DirtyElementMaxGridCellQueryCount);
				}
				else if (Query == EAABBQueryType::Sweep)
				{
					bUseGrid = !TooManySweepQueryCells(QueryHalfExtents, Start, CurData.Dir, CurData.CurrentLength, DirtyElementGridCellSizeInv, DirtyElementMaxGridCellQueryCount);
				}
			}
			
			if (bUseGrid)
			{
				TArray<DirtyGridHashEntry> HashEntryForOverlappedCells;
				auto AddHashEntry = [&](int32 QueryCellHash)
				{
					const DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(QueryCellHash);
					if (HashEntry)
					{
						HashEntryForOverlappedCells.Add(*HashEntry);
					}
				};

				if (Query == EAABBQueryType::Overlap)
				{
					DoForOverlappedCells(QueryBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, AddHashEntry);
				}
				else if (Query == EAABBQueryType::Raycast)
				{
					DoForRaycastIntersectCells(Start, CurData.Dir, CurData.CurrentLength, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, AddHashEntry);
				}
				else if (Query == EAABBQueryType::Sweep)
				{
					DoForSweepIntersectCells(QueryHalfExtents, Start, CurData.Dir, CurData.CurrentLength, DirtyElementGridCellSize , DirtyElementGridCellSizeInv ,
					[&](FReal X, FReal Y)
					{
						int32 QueryCellHash = HashCoordinates(X, Y, DirtyElementGridCellSizeInv);
						const DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(QueryCellHash);
						if (HashEntry)
						{
							HashEntryForOverlappedCells.Add(*HashEntry);
						}
					});
				}

				if (!DoForHitGridCellsAndOverflow(HashEntryForOverlappedCells, IntersectAndVisit))
				{
					return false;
				}
			}  // end overlap

			else for (const auto& Elem : DirtyElements)
			{
				if (!IntersectAndVisit(Elem))
				{
					return false;
				}
			}
		}

		struct FNodeQueueEntry
		{
			int32 NodeIdx;
			FReal TOI;
		};

		TArray<FNodeQueueEntry> NodeStack;
		if (Nodes.Num())
		{
			NodeStack.Add(FNodeQueueEntry{ 0, 0 });
		}

		while (NodeStack.Num())
		{
			const FNodeQueueEntry NodeEntry = NodeStack.Pop(false);
			if (Query != EAABBQueryType::Overlap)
			{
				if (NodeEntry.TOI > CurData.CurrentLength)
				{
					continue;
				}
			}

			const FNode& Node = Nodes[NodeEntry.NodeIdx];
			if (Node.bLeaf)
			{
				const auto& Leaf = Leaves[Node.ChildrenNodes[0]];
				if (Query == EAABBQueryType::Overlap)
				{
					if (Leaf.OverlapFast(QueryBounds, Visitor) == false)
					{
						return false;
					}
				}
				else if (Query == EAABBQueryType::Sweep)
				{
					if (Leaf.SweepFast(Start, CurData, QueryHalfExtents, Visitor, Dir, InvDir, bParallel) == false)
					{
						return false;
					}
				}
				else if (Leaf.RaycastFast(Start, CurData, Visitor, Dir, InvDir, bParallel) == false)
				{
					return false;
				}
			}
			else
			{
				int32 Idx = 0;
				for (const FAABB3& AABB : Node.ChildrenBounds)
				{
					if(TAABBTreeIntersectionHelper<TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, AABB, QueryBounds, QueryHalfExtents, Dir, InvDir, bParallel))
					{
						NodeStack.Add(FNodeQueueEntry{ Node.ChildrenNodes[Idx], TOI });
					}
					++Idx;
				}
			}
		}

		return true;
	}

	int32 GetNewWorkSnapshot()
	{
		if(WorkPoolFreeList.Num())
		{
			return WorkPoolFreeList.Pop();
		}
		else
		{
			return WorkPool.AddDefaulted(1);
		}
	}

	void FreeWorkSnapshot(int32 WorkSnapshotIdx)
	{
		//Reset for next time they want to use it
		WorkPool[WorkSnapshotIdx] = FWorkSnapshot();
		WorkPoolFreeList.Add(WorkSnapshotIdx);
		
	}

	template <typename TParticles>
	void GenerateTree(const TParticles& Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_AABBTreeGenerateTree);
		this->SetAsyncTimeSlicingComplete(false);

		ensure(WorkStack.Num() == 0);

		const int32 ExpectedNumLeaves = Particles.Num() / MaxChildrenInLeaf;
		const int32 ExpectedNumNodes = ExpectedNumLeaves;

		WorkStack.Reserve(ExpectedNumNodes);

		const int32 CurIdx = GetNewWorkSnapshot();
		FWorkSnapshot& WorkSnapshot = WorkPool[CurIdx];
		WorkSnapshot.Elems.Reserve(Particles.Num());
		
		
		GlobalPayloads.Reset();
		Leaves.Reset();
		Nodes.Reset();
		DirtyElements.Reset();
		CellHashToFlatArray.Reset(); 
		FlattenedCellArrayOfDirtyIndices.Reset();
		DirtyElementsGridOverflow.Reset();
		PayloadToInfo.Reset();
		NumProcessedThisSlice = 0;
		GetCVars();  // Safe to copy CVARS here

		WorkSnapshot.Bounds = FAABB3::EmptyAABB();

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeTimeSliceSetup);

			int32 Idx = 0;

			//TODO: we need a better way to time-slice this case since there can be a huge number of Particles. Can't do it right now without making full copy
			FVec3 CenterSum(0);

			for (auto& Particle : Particles)
			{
				bool bHasBoundingBox = HasBoundingBox(Particle);
				auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
				FAABB3 ElemBounds = ComputeWorldSpaceBoundingBox(Particle, false, (FReal)0);
				if (bHasBoundingBox)
				{
					if (ElemBounds.Extents().Max() > MaxPayloadBounds)
					{
						bHasBoundingBox = false;
					}
					else
					{
						WorkSnapshot.Elems.Add(FElement{ Payload, ElemBounds });
						WorkSnapshot.Bounds.GrowToInclude(ElemBounds);
						CenterSum += ElemBounds.Center();
					}
				}
				else
				{
					ElemBounds = FAABB3(FVec3(TNumericLimits<FReal>::Lowest()), FVec3(TNumericLimits<FReal>::Max()));
				}

				if (!bHasBoundingBox)
				{
					if (bMutable)
					{
						PayloadToInfo.Add(Payload, FAABBTreePayloadInfo{ GlobalPayloads.Num(), INDEX_NONE, INDEX_NONE, INDEX_NONE });
					}
					GlobalPayloads.Add(FElement{ Payload, ElemBounds });
				}

				++Idx;
				//todo: payload info
			}

			if(WorkSnapshot.Elems.Num())
			{
				WorkSnapshot.AverageCenter = CenterSum * ((FReal)1 / (FReal)WorkSnapshot.Elems.Num());
			}
			else
			{
				WorkSnapshot.AverageCenter = FVec3(0);
			}
		}

		NumProcessedThisSlice = Particles.Num();	//todo: give chance to time slice out of next phase

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeInitialTimeSlice);
			WorkSnapshot.NewNodeIdx = 0;
			WorkSnapshot.NodeLevel = 0;

			//push root onto stack
			WorkStack.Add(CurIdx);

			SplitNode();
		}

		/*  Helper validation code 
		int32 Count = 0;
		TSet<int32> Seen;
		if(WorkStack.Num() == 0)
		{
			int32 LeafIdx = 0;
			for (const auto& Leaf : Leaves)
			{
				Validate(Seen, Count, Leaf);
				bool bHasParent = false;
				for (const auto& Node : Nodes)
				{
					if (Node.bLeaf && Node.ChildrenNodes[0] == LeafIdx)
					{
						bHasParent = true;

						break;
					}
				}
				ensure(bHasParent);
				++LeafIdx;
			}
			ensure(Count == 0 || Seen.Num() == Count);
			ensure(Count == 0 || Count == Particles.Num());
		}
		*/

	}

	enum eTimeSlicePhase
	{
		PreFindBestBounds,
		DuringFindBestBounds,
		ProcessingChildren
	};

	struct FSplitInfo
	{
		FAABB3 SplitBounds;	//Even split of parent bounds
		FAABB3 RealBounds;	//Actual bounds as children are added
		int32 WorkSnapshotIdx;	//Idx into work snapshot pool
		FReal SplitBoundsSize2;
	};

	struct FWorkSnapshot
	{
		FWorkSnapshot()
			: TimeslicePhase(eTimeSlicePhase::PreFindBestBounds)
		{

		}

		eTimeSlicePhase TimeslicePhase;

		FAABB3 Bounds;
		FVec3 AverageCenter;
		TArray<FElement> Elems;

		int32 NodeLevel;
		int32 NewNodeIdx;

		int32 BestBoundsCurIdx;

		FSplitInfo SplitInfos[2];
	};

	void FindBestBounds(const int32 StartElemIdx, const int32 LastElem, FWorkSnapshot& CurrentSnapshot)
	{
		// add all elements to one of the two split infos at this level - root level [ not taking into account the max number allowed or anything
		for(int32 ElemIdx = StartElemIdx; ElemIdx < LastElem; ++ElemIdx)
		{
			const FElement& Elem = CurrentSnapshot.Elems[ElemIdx];
			int32 MinBoxIdx = INDEX_NONE;
			FReal MinDelta2 = TNumericLimits<FReal>::Max();
			int32 BoxIdx = 0;
			for (const FSplitInfo& SplitInfo : CurrentSnapshot.SplitInfos)
			{
				FAABB3 NewBox = SplitInfo.SplitBounds;
				NewBox.GrowToInclude(Elem.Bounds);
				const FReal Delta2 = NewBox.Extents().SizeSquared() - SplitInfo.SplitBoundsSize2;
				if (Delta2 < MinDelta2)
				{
					MinDelta2 = Delta2;
					MinBoxIdx = BoxIdx;
				}
				++BoxIdx;
			}

			if (CHAOS_ENSURE(MinBoxIdx != INDEX_NONE))
			{
				FSplitInfo& SplitInfo = CurrentSnapshot.SplitInfos[MinBoxIdx];
				WorkPool[SplitInfo.WorkSnapshotIdx].Elems.Add(Elem);
				WorkPool[SplitInfo.WorkSnapshotIdx].AverageCenter += Elem.Bounds.Center();
				SplitInfo.RealBounds.GrowToInclude(Elem.Bounds);
			}
		}

		NumProcessedThisSlice += LastElem - StartElemIdx;
	}

	void SplitNode()
	{
		const bool WeAreTimeslicing = (MaxNumToProcess > 0);

		while (WorkStack.Num())
		{
			//NOTE: remember to be careful with this since it's a pointer on a tarray
			const int32 CurIdx = WorkStack.Last();

			if (WorkPool[CurIdx].TimeslicePhase == eTimeSlicePhase::ProcessingChildren)
			{
				//If we got to this it must be that my children are done, so I'm done as well
				WorkStack.Pop(/*bResize=*/false);
				FreeWorkSnapshot(CurIdx);
				continue;
			}

			const int32 NewNodeIdx = WorkPool[CurIdx].NewNodeIdx;

			// create the actual node space but might no be filled in (YET) due to time slicing exit
			if (NewNodeIdx >= Nodes.Num())
			{
				Nodes.AddDefaulted((1 + NewNodeIdx) - Nodes.Num());
			}

			if (WeAreTimeslicing && (NumProcessedThisSlice >= MaxNumToProcess))
			{
				return; // done enough
			}

			auto& PayloadToInfoRef = PayloadToInfo;
			auto& LeavesRef = Leaves;
			auto& NodesRef = Nodes;
			auto& WorkPoolRef = WorkPool;
			auto MakeLeaf = [NewNodeIdx, &PayloadToInfoRef, &WorkPoolRef, CurIdx, &LeavesRef, &NodesRef]()
			{
				if (bMutable)
				{
					//todo: does this need time slicing in the case when we have a ton of elements that can't be split?
					//hopefully not a real issue
					for (const FElement& Elem : WorkPoolRef[CurIdx].Elems)
					{
						PayloadToInfoRef.Add(Elem.Payload, FAABBTreePayloadInfo{ INDEX_NONE, INDEX_NONE, LeavesRef.Num() });
					}
				}

				NodesRef[NewNodeIdx].bLeaf = true;
				NodesRef[NewNodeIdx].ChildrenNodes[0] = LeavesRef.Add(TLeafType{ WorkPoolRef[CurIdx].Elems }); //todo: avoid copy?

			};

			if (WorkPool[CurIdx].Elems.Num() <= MaxChildrenInLeaf || WorkPool[CurIdx].NodeLevel >= MaxTreeDepth)
			{

				MakeLeaf();
				WorkStack.Pop(/*bResize=*/false);	//finished with this node
				FreeWorkSnapshot(CurIdx);
				continue;
			}

			if (WorkPool[CurIdx].TimeslicePhase == eTimeSlicePhase::PreFindBestBounds)
			{
				const FVec3 Extents = WorkPool[CurIdx].Bounds.Extents();
				const int32 MaxAxis = WorkPool[CurIdx].Bounds.LargestAxis();	//todo: use variance instead

				//Add two children, remember this invalidates any pointers to current snapshot
				const int32 FirstChildIdx = GetNewWorkSnapshot();
				const int32 SecondChildIdx = GetNewWorkSnapshot();

				//mark idx of children into the work pool
				WorkPool[CurIdx].SplitInfos[0].WorkSnapshotIdx = FirstChildIdx;
				WorkPool[CurIdx].SplitInfos[1].WorkSnapshotIdx = SecondChildIdx;

				//these are the hypothetical bounds for the perfect 50/50 split
				WorkPool[CurIdx].SplitInfos[0].SplitBounds = FAABB3(WorkPool[CurIdx].Bounds.Min(), WorkPool[CurIdx].Bounds.Min());
				WorkPool[CurIdx].SplitInfos[1].SplitBounds = FAABB3(WorkPool[CurIdx].Bounds.Max(), WorkPool[CurIdx].Bounds.Max());

				const FVec3 Center = WorkPool[CurIdx].AverageCenter;
				for (FSplitInfo& SplitInfo : WorkPool[CurIdx].SplitInfos)
				{
					SplitInfo.RealBounds = FAABB3::EmptyAABB();

					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						FVec3 NewPt0 = Center;
						FVec3 NewPt1 = Center;
						if (Axis != MaxAxis)
						{
							NewPt0[Axis] = WorkPool[CurIdx].Bounds.Min()[Axis];
							NewPt1[Axis] = WorkPool[CurIdx].Bounds.Max()[Axis];
							SplitInfo.SplitBounds.GrowToInclude(NewPt0);
							SplitInfo.SplitBounds.GrowToInclude(NewPt1);
						}
					}

					SplitInfo.SplitBoundsSize2 = SplitInfo.SplitBounds.Extents().SizeSquared();
				}

				WorkPool[CurIdx].BestBoundsCurIdx = 0;
				WorkPool[CurIdx].TimeslicePhase = eTimeSlicePhase::DuringFindBestBounds;
				const int32 ExpectedNumPerChild = WorkPool[CurIdx].Elems.Num() / 2;
				{
					WorkPool[FirstChildIdx].Elems.Reserve(ExpectedNumPerChild);
					WorkPool[SecondChildIdx].Elems.Reserve(ExpectedNumPerChild);

					WorkPool[FirstChildIdx].AverageCenter = FVec3(0);
					WorkPool[SecondChildIdx].AverageCenter = FVec3(0);
				}
			}

			if (WorkPool[CurIdx].TimeslicePhase == eTimeSlicePhase::DuringFindBestBounds)
			{
				const int32 NumWeCanProcess = MaxNumToProcess - NumProcessedThisSlice;
				const int32 LastIdxToProcess = WeAreTimeslicing ? FMath::Min(WorkPool[CurIdx].BestBoundsCurIdx + NumWeCanProcess, WorkPool[CurIdx].Elems.Num()) : WorkPool[CurIdx].Elems.Num();
				FindBestBounds(WorkPool[CurIdx].BestBoundsCurIdx, LastIdxToProcess, WorkPool[CurIdx]);
				WorkPool[CurIdx].BestBoundsCurIdx = LastIdxToProcess;

				if (WeAreTimeslicing && (NumProcessedThisSlice >= MaxNumToProcess))
				{
					return; // done enough
				}
			}


			const int32 FirstChildIdx = WorkPool[CurIdx].SplitInfos[0].WorkSnapshotIdx;
			const int32 SecondChildIdx = WorkPool[CurIdx].SplitInfos[1].WorkSnapshotIdx;

			const bool bChildrenInBothHalves = WorkPool[FirstChildIdx].Elems.Num() && WorkPool[SecondChildIdx].Elems.Num();

			// if children in both halves, push them on the stack to continue the split
			if (bChildrenInBothHalves)
			{
				Nodes[NewNodeIdx].bLeaf = false;

				Nodes[NewNodeIdx].ChildrenBounds[0] = WorkPool[CurIdx].SplitInfos[0].RealBounds;
				WorkPool[FirstChildIdx].Bounds = Nodes[NewNodeIdx].ChildrenBounds[0];
				Nodes[NewNodeIdx].ChildrenNodes[0] = Nodes.Num();

				Nodes[NewNodeIdx].ChildrenBounds[1] = WorkPool[CurIdx].SplitInfos[1].RealBounds;
				WorkPool[SecondChildIdx].Bounds = Nodes[NewNodeIdx].ChildrenBounds[1];
				Nodes[NewNodeIdx].ChildrenNodes[1] = Nodes.Num() + 1;

				WorkPool[FirstChildIdx].NodeLevel = WorkPool[CurIdx].NodeLevel + 1;
				WorkPool[SecondChildIdx].NodeLevel = WorkPool[CurIdx].NodeLevel + 1;

				WorkPool[FirstChildIdx].NewNodeIdx = Nodes[NewNodeIdx].ChildrenNodes[0];
				WorkPool[SecondChildIdx].NewNodeIdx = Nodes[NewNodeIdx].ChildrenNodes[1];

				WorkPool[FirstChildIdx].AverageCenter *= ((FReal)1 / (FReal)WorkPool[FirstChildIdx].Elems.Num());
				WorkPool[SecondChildIdx].AverageCenter *= ((FReal)1 / (FReal)WorkPool[SecondChildIdx].Elems.Num());

				//push these two new nodes onto the stack
				WorkStack.Add(SecondChildIdx);
				WorkStack.Add(FirstChildIdx);

				// create the actual node so that no one else can use our children node indices
				const int32 HighestNodeIdx = Nodes[NewNodeIdx].ChildrenNodes[1];
				Nodes.AddDefaulted((1 + HighestNodeIdx) - Nodes.Num());
				
				WorkPool[CurIdx].TimeslicePhase = eTimeSlicePhase::ProcessingChildren;
			}
			else
			{
				//couldn't split so just make a leaf - THIS COULD CONTAIN MORE THAN MaxChildrenInLeaf!!!
				MakeLeaf();
				WorkStack.Pop(/*bResize=*/false);	//we are done with this node
				FreeWorkSnapshot(CurIdx);
			}
		}

		check(WorkStack.Num() == 0);
		//Stack is empty, clean up pool and mark task as complete
		
		this->SetAsyncTimeSlicingComplete(true);
	}

	TArray<TPayloadType> FindAllIntersectionsImp(const FAABB3& Intersection) const
	{
		struct FSimpleVisitor
		{
			FSimpleVisitor(TArray<TPayloadType>& InResults) : CollectedResults(InResults) {}
			bool VisitOverlap(const TSpatialVisitorData<TPayloadType>& Instance)
			{
				CollectedResults.Add(Instance.Payload);
				return true;
			}
			bool VisitSweep(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData)
			{
				check(false);
				return true;
			}
			bool VisitRaycast(const TSpatialVisitorData<TPayloadType>& Instance, FQueryFastData& CurData)
			{
				check(false);
				return true;
			}

			const void* GetQueryData() const { return nullptr; }

			TArray<TPayloadType>& CollectedResults;
		};

		TArray<TPayloadType> Results;
		FSimpleVisitor Collector(Results);
		Overlap(Intersection, Collector);

		return Results;
	}


	TAABBTree(const TAABBTree<TPayloadType, TLeafType, bMutable>& Other)
		: ISpatialAcceleration<TPayloadType, FReal, 3>(StaticType)
		, Nodes(Other.Nodes)
		, Leaves(Other.Leaves)
		, DirtyElements(Other.DirtyElements)
		, CellHashToFlatArray(Other.CellHashToFlatArray)
		, FlattenedCellArrayOfDirtyIndices(Other.FlattenedCellArrayOfDirtyIndices)
		, DirtyElementsGridOverflow(Other.DirtyElementsGridOverflow)
		, DirtyElementGridCellSize(Other.DirtyElementGridCellSize)
		, DirtyElementGridCellSizeInv(Other.DirtyElementGridCellSizeInv)
		, DirtyElementMaxGridCellQueryCount(Other.DirtyElementMaxGridCellQueryCount)
		, DirtyElementMaxPhysicalSizeInCells(Other.DirtyElementMaxPhysicalSizeInCells)
		, DirtyElementMaxCellCapacity(Other.DirtyElementMaxCellCapacity)
		, GlobalPayloads(Other.GlobalPayloads)
		, PayloadToInfo(Other.PayloadToInfo)
		, MaxChildrenInLeaf(Other.MaxChildrenInLeaf)
		, MaxTreeDepth(Other.MaxTreeDepth)
		, MaxPayloadBounds(Other.MaxPayloadBounds)
		, MaxNumToProcess(Other.MaxNumToProcess)
		, NumProcessedThisSlice(Other.NumProcessedThisSlice)
	{

	}

	TAABBTree<TPayloadType,TLeafType, bMutable>& operator=(const TAABBTree<TPayloadType,TLeafType,bMutable>& Rhs)
	{
		ensure(Rhs.WorkStack.Num() == 0);
		//ensure(Rhs.WorkPool.Num() == 0);
		//ensure(Rhs.WorkPoolFreeList.Num() == 0);
		WorkStack.Empty();
		WorkPool.Empty();
		WorkPoolFreeList.Empty();
		if(this != &Rhs)
		{
			Nodes = Rhs.Nodes;
			Leaves = Rhs.Leaves;
			DirtyElements = Rhs.DirtyElements;
			
			CellHashToFlatArray = Rhs.CellHashToFlatArray;
			FlattenedCellArrayOfDirtyIndices = Rhs.FlattenedCellArrayOfDirtyIndices;
			DirtyElementsGridOverflow = Rhs.DirtyElementsGridOverflow;
			
			DirtyElementGridCellSize = Rhs.DirtyElementGridCellSize;
			DirtyElementGridCellSizeInv = Rhs.DirtyElementGridCellSizeInv;
			DirtyElementMaxGridCellQueryCount = Rhs.DirtyElementMaxGridCellQueryCount;
			DirtyElementMaxPhysicalSizeInCells = Rhs.DirtyElementMaxPhysicalSizeInCells;
			DirtyElementMaxCellCapacity = Rhs.DirtyElementMaxCellCapacity;
			
			GlobalPayloads = Rhs.GlobalPayloads;
			PayloadToInfo = Rhs.PayloadToInfo;
			MaxChildrenInLeaf = Rhs.MaxChildrenInLeaf;
			MaxTreeDepth = Rhs.MaxTreeDepth;
			MaxPayloadBounds = Rhs.MaxPayloadBounds;
			MaxNumToProcess = Rhs.MaxNumToProcess;
			NumProcessedThisSlice = Rhs.NumProcessedThisSlice;
		}

		return *this;
	}

	TArray<FNode> Nodes;
	TArray<TLeafType> Leaves;
	TArray<FElement> DirtyElements;

	// Data needed for DirtyElement2DAccelerationGrid
	TMap<int32, DirtyGridHashEntry> CellHashToFlatArray; // Index, size into flat grid structure (FlattenedCellArrayOfDirtyIndices)
	TArray<int32> FlattenedCellArrayOfDirtyIndices; // Array of indices into dirty Elements array, indices are always sorted within a 2D cell
	TArray<int32> DirtyElementsGridOverflow; // Array of indices of DirtyElements that is not in the grid for some reason
	// Copy of CVARS
	FReal DirtyElementGridCellSize;
	FReal DirtyElementGridCellSizeInv;
	int32 DirtyElementMaxGridCellQueryCount;
	int32 DirtyElementMaxPhysicalSizeInCells;
	int32 DirtyElementMaxCellCapacity;

	TArray<FElement> GlobalPayloads;
	TArrayAsMap<TPayloadType, FAABBTreePayloadInfo> PayloadToInfo;

	int32 MaxChildrenInLeaf;
	int32 MaxTreeDepth;
	FReal MaxPayloadBounds;
	int32 MaxNumToProcess;

	int32 NumProcessedThisSlice;
	TArray<int32> WorkStack;
	TArray<int32> WorkPoolFreeList;
	TArray<FWorkSnapshot> WorkPool;
};

template<typename TPayloadType, typename TLeafType, bool bMutable>
FArchive& operator<<(FChaosArchive& Ar, TAABBTree<TPayloadType, TLeafType, bMutable>& AABBTree)
{
	AABBTree.Serialize(Ar);
	return Ar;
}


}
