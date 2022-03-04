// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/AABB.h"
#include "Chaos/AABBVectorized.h"
#include "Chaos/AABBTreeDirtyGridUtils.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Templates/Models.h"
#include "Chaos/BoundingVolume.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ChaosStats.h"
#include "Math/VectorRegister.h"

CSV_DECLARE_CATEGORY_EXTERN(ChaosPhysicsTimers);

struct CHAOS_API FAABBTreeCVars
{
	static int32 UpdateDirtyElementPayloadData;
	static FAutoConsoleVariableRef CVarUpdateDirtyElementPayloadData;

	static int32 SplitAtAverageCenter;
	static FAutoConsoleVariableRef CVarSplitAtAverageCenter;

	static int32 SplitOnVarianceAxis;
	static FAutoConsoleVariableRef CVarSplitOnVarianceAxis;

	static float MaxNonGlobalElementBoundsExtrema; 
	static FAutoConsoleVariableRef CVarMaxNonGlobalElementBoundsExtrema;

	static float DynamicTreeBoundingBoxPadding;
	static FAutoConsoleVariableRef CVarDynamicTreeBoundingBoxPadding;

	static int32 DynamicTreeLeafCapacity;
	static FAutoConsoleVariableRef CVarDynamicTreeLeafCapacity;
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

struct AABBTreeStatistics
{
	void Reset()
	{
		StatNumNonEmptyCellsInGrid = 0;
		StatNumElementsTooLargeForGrid = 0;
		StatNumDirtyElements = 0;
		StatNumGridOverflowElements = 0;
	}

	AABBTreeStatistics& MergeStatistics(const AABBTreeStatistics& Rhs)
	{
		StatNumNonEmptyCellsInGrid += Rhs.StatNumNonEmptyCellsInGrid;
		StatNumElementsTooLargeForGrid += Rhs.StatNumElementsTooLargeForGrid;
		StatNumDirtyElements += Rhs.StatNumDirtyElements;
		StatNumGridOverflowElements += Rhs.StatNumGridOverflowElements;
		return *this;
	}

	int32 StatNumNonEmptyCellsInGrid = 0;
	int32 StatNumElementsTooLargeForGrid = 0;
	int32 StatNumDirtyElements = 0;
	int32 StatNumGridOverflowElements = 0;
};

struct AABBTreeExpensiveStatistics
{
	void Reset()
	{
		StatMaxNumLeaves = 0;
		StatMaxDirtyElements = 0;
		StatMaxLeafSize = 0;
		StatMaxTreeDepth = 0;
		StatGlobalPayloadsSize = 0;
	}

	AABBTreeExpensiveStatistics& MergeStatistics(const AABBTreeExpensiveStatistics& Rhs)
	{
		StatMaxNumLeaves = FMath::Max(StatMaxNumLeaves, Rhs.StatMaxNumLeaves);
		StatMaxDirtyElements = FMath::Max(StatMaxDirtyElements, Rhs.StatMaxDirtyElements);
		StatMaxLeafSize = FMath::Max(StatMaxLeafSize, Rhs.StatMaxLeafSize);
		StatMaxTreeDepth = FMath::Max(StatMaxTreeDepth, Rhs.StatMaxTreeDepth);
		StatGlobalPayloadsSize += Rhs.StatGlobalPayloadsSize;
		return *this;
	}
	int32 StatMaxNumLeaves = 0;
	int32 StatMaxDirtyElements = 0;
	int32 StatMaxLeafSize = 0;
	int32 StatMaxTreeDepth = 0;
	int32 StatGlobalPayloadsSize = 0;
};

DECLARE_CYCLE_STAT(TEXT("AABBTreeGenerateTree"), STAT_AABBTreeGenerateTree, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeTimeSliceSetup"), STAT_AABBTreeTimeSliceSetup, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeInitialTimeSlice"), STAT_AABBTreeInitialTimeSlice, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("AABBTreeProgressTimeSlice"), STAT_AABBTreeProgressTimeSlice, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("AABBTreeGrowPhase"), STAT_AABBTreeGrowPhase, STATGROUP_Chaos);
//DECLARE_CYCLE_STAT(TEXT("AABBTreeChildrenPhase"), STAT_AABBTreeChildrenPhase, STATGROUP_Chaos);

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
	if (FAABBTreeCVars::UpdateDirtyElementPayloadData != 0)
	{
		InElem.UpdateFrom(InFrom);
	}
}

template <typename TQueryFastData, EAABBQueryType Query>
struct TAABBTreeIntersectionHelper
{
	static bool Intersects(const FVec3& Start, TQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const FAABB3& Bounds, const FAABB3& QueryBounds, const FVec3& QueryHalfExtents, const FVec3& Dir, const FVec3 InvDir, const bool bParallel[3])
	{
		check(false);
		return true;
	}
	static bool IntersectsVectorized(const VectorRegister4Float& Start, FQueryFastData& QueryFastData, VectorRegister4Float& TOI, VectorRegister4Float& OutPosition,
		const FAABBVectorized& Bounds, const FAABBVectorized& QueryBounds, const VectorRegister4Float& QueryHalfExtents, const VectorRegister4Float& Dir, const VectorRegister4Float InvDir, const bool bParallel[3])
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

	FORCEINLINE_DEBUGGABLE static bool IntersectsVectorized(const VectorRegister4Float& Start, FQueryFastData& QueryFastData, VectorRegister4Float& TOI, VectorRegister4Float& OutPosition,
		const FAABBVectorized& Bounds, const FAABBVectorized& QueryBounds, const VectorRegister4Float& QueryHalfExtents, const VectorRegister4Float& Dir, const VectorRegister4Float InvDir, const bool bParallel[3])
	{
		check(false);
		return true;
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

	FORCEINLINE_DEBUGGABLE static bool IntersectsVectorized(const VectorRegister4Float& Start, FQueryFastData& QueryFastData, VectorRegister4Float& TOI, VectorRegister4Float& OutPosition,
		const FAABBVectorized& Bounds, const FAABBVectorized& QueryBounds, const VectorRegister4Float& QueryHalfExtents, const VectorRegister4Float& Dir, const VectorRegister4Float InvDir, const bool bParallel[3])
	{
		VectorRegister4Float CurrentLength = MakeVectorRegisterFloatFromDouble(VectorLoadDouble1(&QueryFastData.CurrentLength));
		VectorRegister4Float InvCurrentLength = MakeVectorRegisterFloatFromDouble(VectorLoadDouble1(&QueryFastData.InvCurrentLength));
		FAABBVectorized SweepBounds(VectorSubtract(Bounds.GetMin(), QueryHalfExtents), VectorAdd(Bounds.GetMax(), QueryHalfExtents));
		return SweepBounds.RaycastFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, OutPosition);
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

template <typename TPayloadType, typename T, bool bComputeBounds>
struct TBoundsWrapperHelper
{
};

template <typename TPayloadType, typename T>
struct TBoundsWrapperHelper<TPayloadType, T, true>
{
	void ComputeBounds(const TArray<TPayloadBoundsElement<TPayloadType, T>>& Elems)
	{
		Bounds = TAABB<T, 3>::EmptyAABB();

		for (const auto& Elem : Elems)
		{
			Bounds.GrowToInclude(Elem.Bounds);
		}
	}

	const TAABB<T, 3>& GetBounds() const { return Bounds; }

private:
	TAABB<T, 3> Bounds;
};

template <typename TPayloadType, typename T>
struct TBoundsWrapperHelper<TPayloadType, T, false>
{
	void ComputeBounds(const TArray<TPayloadBoundsElement<TPayloadType, T>>&)
	{
	}

	const TAABB<T, 3> GetBounds() const
	{
		return TAABB<T, 3>::EmptyAABB();
	}
};

template <typename TPayloadType, bool bComputeBounds = true, typename T = FReal>
struct TAABBTreeLeafArray : public TBoundsWrapperHelper<TPayloadType, T, bComputeBounds>
{
	TAABBTreeLeafArray() {}
	//todo: avoid copy?
	TAABBTreeLeafArray(const TArray<TPayloadBoundsElement<TPayloadType, T>>& InElems)
		: Elems(InElems)
	{
		this->ComputeBounds(Elems);
	}

	void GatherElements(TArray<TPayloadBoundsElement<TPayloadType, T>>& OutElements)
	{
		OutElements.Append(Elems);
	}

	SIZE_T GetReserveCount() const
	{
		// Optimize for fewer memory allocations.
		return Elems.Num();
	}

	SIZE_T GetElementCount() const
	{
		return Elems.Num();
	}

	void RecomputeBounds()
	{
		this->ComputeBounds(Elems);
	}

	/** Check if the leaf is dirty (if one of the payload have been updated)
	 * @return Dirty boolean that indicates if the leaf is dirty or not
 	 */
	bool IsLeafDirty() const
	{
		return bDirtyLeaf;
	}

	/** Set thye dirty flag onto the leaf 
	 * @param  bDirtyState Disrty flag to set 
	 */
	void SetDirtyState(const bool bDirtyState)
	{
		bDirtyLeaf = bDirtyState;
	}

	template <typename TSQVisitor, typename TQueryFastData>
	FORCEINLINE_DEBUGGABLE bool RaycastFast(const TVec3<T>& Start, TQueryFastData& QueryFastData, TSQVisitor& Visitor, const TVec3<T>& Dir, const TVec3<T> InvDir, const bool bParallel[3]) const
	{
		return RaycastSweepImp</*bSweep=*/false>(Start, QueryFastData, TVec3<T>((T)0), Visitor, Dir, InvDir, bParallel);
	}

	template <typename TSQVisitor, typename TQueryFastData>
	FORCEINLINE_DEBUGGABLE bool SweepFast(const TVec3<T>& Start, TQueryFastData& QueryFastData, const TVec3<T>& QueryHalfExtents, TSQVisitor& Visitor,
			const TVec3<T>& Dir, const TVec3<T> InvDir, const bool bParallel[3]) const
	{
		return RaycastSweepImp</*bSweep=*/true>(Start, QueryFastData, QueryHalfExtents, Visitor, Dir, InvDir, bParallel);
	}

	template <typename TSQVisitor>
	bool OverlapFast(const FAABB3& QueryBounds, TSQVisitor& Visitor) const
	{
		PHYSICS_CSV_CUSTOM_VERY_EXPENSIVE(PhysicsCounters, MaxLeafSize, Elems.Num(), ECsvCustomStatOp::Max);

		for (const auto& Elem : Elems)
		{
			if (PrePreFilterHelper(Elem.Payload, Visitor))
			{
				continue;
			}

			if (Elem.Bounds.Intersects(QueryBounds))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, FAABB3(Elem.Bounds.Min(), Elem.Bounds.Max()));
				if (Visitor.VisitOverlap(VisitData) == false)
				{
					return false;
				}
			}
		}

		return true;
	}

	template <bool bSweep, typename TQueryFastData, typename TSQVisitor>
	FORCEINLINE_DEBUGGABLE bool RaycastSweepImp(const TVec3<T>& Start, TQueryFastData& QueryFastData, const TVec3<T>& QueryHalfExtents, TSQVisitor& Visitor, const TVec3<T>& Dir, const TVec3<T> InvDir, const bool bParallel[3]) const
	{
		PHYSICS_CSV_CUSTOM_VERY_EXPENSIVE(PhysicsCounters, MaxLeafSize, Elems.Num(), ECsvCustomStatOp::Max);

		const VectorRegister4Float DirSimd = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(Dir.X, Dir.Y, Dir.Z, 0.0));
		const VectorRegister4Float InvDirSimd = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(InvDir.X, InvDir.Y, InvDir.Z, 0.0));
		const VectorRegister4Float QueryHalfExtentsSimd = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(QueryHalfExtents.X, QueryHalfExtents.Y, QueryHalfExtents.Z, 0.0));
		
		for (const auto& Elem : Elems)
		{
			if (PrePreFilterHelper(Elem.Payload, Visitor))
			{
				continue;
			}

			const FAABB3 InstanceBounds(Elem.Bounds.Min(), Elem.Bounds.Max());
			if (bSweep == true)
			{
				const VectorRegister4Float Min = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(InstanceBounds.Min().X, InstanceBounds.Min().Y, InstanceBounds.Min().Z, 0.0));
				const VectorRegister4Float Max = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(InstanceBounds.Max().X, InstanceBounds.Max().Y, InstanceBounds.Max().Z, 0.0));
				const FAABBVectorized InstanceBoundsSimd(Min, Max);
				VectorRegister4Float TmpPosition;
				VectorRegister4Float TOI;
				VectorRegister4Float StartSimd = MakeVectorRegisterFloatFromDouble(MakeVectorRegisterDouble(Start.X, Start.Y, Start.Z, 0.0));

				if (TAABBTreeIntersectionHelper<TQueryFastData, EAABBQueryType::Sweep>::IntersectsVectorized(
					StartSimd, QueryFastData, TOI, TmpPosition, InstanceBoundsSimd, FAABBVectorized(), QueryHalfExtentsSimd, DirSimd, InvDirSimd, bParallel))
				{
					TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
					const bool bContinue = (bSweep && Visitor.VisitSweep(VisitData, QueryFastData)) || (!bSweep && Visitor.VisitRaycast(VisitData, QueryFastData));
					if (!bContinue)
					{
						return false;
					}
				}
			}
			else 
			{
				FVec3 TmpPosition;
				FReal TOI;
				if (TAABBTreeIntersectionHelper<TQueryFastData,
					EAABBQueryType::Raycast>::Intersects(Start, QueryFastData, TOI, TmpPosition, InstanceBounds, FAABB3(), QueryHalfExtents, Dir, InvDir, bParallel))
				{
					TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
					const bool bContinue = (bSweep && Visitor.VisitSweep(VisitData, QueryFastData)) || (!bSweep && Visitor.VisitRaycast(VisitData, QueryFastData));
					if (!bContinue)
					{
						return false;
					}
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
		bDirtyLeaf = true;
	}

	void UpdateElement(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds, bool bHasBounds)
	{
		if (!bHasBounds)
			return;

		for (int32 Idx = 0; Idx < Elems.Num(); ++Idx)
		{
			if (Elems[Idx].Payload == Payload)
			{
				Elems[Idx].Bounds = NewBounds;
				UpdateElementHelper(Elems[Idx].Payload, Payload);
				break;
			}
		}
		bDirtyLeaf = true;
	}

	void AddElement(const TPayloadBoundsElement<TPayloadType, T>& Element)
	{
		Elems.Add(Element);
		this->ComputeBounds(Elems);
		bDirtyLeaf = true;
	}

	void Reset()
	{
		Elems.Reset();
		bDirtyLeaf = false;
	}

#if !UE_BUILD_SHIPPING
	void DebugDrawLeaf(ISpacialDebugDrawInterface<T>& InInterface, const FLinearColor& InLinearColor, float InThickness) const
	{
		const TAABB<T, 3> LeafBounds = TBoundsWrapperHelper<TPayloadType, T, bComputeBounds>::GetBounds();

		const float Alpha = (float)Elems.Num() / 10.f;
		const FLinearColor ColorByCount = FLinearColor::Green * (1.f - Alpha) + FLinearColor::Red * Alpha;
		const FVec3 ColorAsVec = { ColorByCount.R, ColorByCount.G, ColorByCount.B };
		
		InInterface.Box(LeafBounds, ColorAsVec, InThickness);
		for (const auto& Elem : Elems)
		{
			InInterface.Line(LeafBounds.Center(), Elem.Bounds.Center(), ColorAsVec, InThickness);
			InInterface.Box(Elem.Bounds, { (T)1.0, (T)0.2, (T)0.2 }, 1.0f);
		}
	}
#endif

	/** Print leaf information (bounds) for debugging purpose*/
	void PrintLeaf() const
	{
		int32 ElemIndex = 0;
		for (const auto& Elem : Elems)
		{
			UE_LOG(LogChaos, Log, TEXT("Elem[%d] with bounds = %f %f %f | %f %f %f"), ElemIndex, 
					Elem.Bounds.Min()[0], Elem.Bounds.Min()[1], Elem.Bounds.Min()[2], 
					Elem.Bounds.Max()[0], Elem.Bounds.Max()[1], Elem.Bounds.Max()[2]);
			++ElemIndex;
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Elems;
	}

	TArray<TPayloadBoundsElement<TPayloadType, T>> Elems;

	/** Flag on the leaf to know if it has been updated */
	bool bDirtyLeaf = false;
};

template <typename TPayloadType, bool bComputeBounds, typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeLeafArray<TPayloadType, bComputeBounds, T>& LeafArray)
{
	LeafArray.Serialize(Ar);
	return Ar;
}

template <typename T>
struct TAABBTreeNode
{
	TAABBTreeNode()
	{
		ChildrenBounds[0] = TAABB<T, 3>();
		ChildrenBounds[1] = TAABB<T, 3>();
	}
	TAABB<T, 3> ChildrenBounds[2];
	int32 ChildrenNodes[2] = { INDEX_NONE, INDEX_NONE };
	int32 ParentNode = INDEX_NONE;
	bool bLeaf = false;
	bool bDirtyNode = false;

#if !UE_BUILD_SHIPPING
	void DebugDraw(ISpacialDebugDrawInterface<T>& InInterface, const TArray<TAABBTreeNode<T>>& Nodes, const FVec3& InLinearColor, float InThickness) const
	{
		constexpr float ColorRatio = 0.75f;
		constexpr float LineThicknessRatio = 0.75f;
		if (!bLeaf)
		{
			FLinearColor ChildColor = FLinearColor::MakeRandomColor();
			for (int ChildIndex = 0; ChildIndex < 2; ++ChildIndex)
			{
				int32 NodeIndex = ChildrenNodes[ChildIndex];
				if (NodeIndex > 0 && NodeIndex < Nodes.Num())
				{
					Nodes[NodeIndex].DebugDraw(InInterface, Nodes, { ChildColor.R, ChildColor.G, ChildColor.B }, InThickness * LineThicknessRatio);
				}
			}
			for (int ChildIndex = 0; ChildIndex < 2; ++ChildIndex)
			{
				InInterface.Box(ChildrenBounds[ChildIndex], InLinearColor, InThickness);
			}
		}
	}
#endif

	void Serialize(FChaosArchive& Ar)
	{
		for (auto& Bounds : ChildrenBounds)
		{
			TBox<T, 3>::SerializeAsAABB(Ar, Bounds);
		}

		for (auto& Node : ChildrenNodes)
		{
			Ar << Node;
		}

		Ar << bLeaf;

		// Dynamic trees are not serialized
		if (Ar.IsLoading())
		{
			ParentNode = INDEX_NONE;
		}
	}
};

template <typename T>
FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeNode<T>& Node)
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
	int32 NodeIdx;

	FAABBTreePayloadInfo(int32 InGlobalPayloadIdx = INDEX_NONE, int32 InDirtyIdx = INDEX_NONE, int32 InLeafIdx = INDEX_NONE, int32 InDirtyGridOverflowIdx = INDEX_NONE, int32 InNodeIdx = INDEX_NONE)
		: GlobalPayloadIdx(InGlobalPayloadIdx)
		, DirtyPayloadIdx(InDirtyIdx)
		, LeafIdx(InLeafIdx)
		, DirtyGridOverflowIdx(InDirtyGridOverflowIdx)
		, NodeIdx(InNodeIdx)
	{}

	void Serialize(FArchive& Ar)
	{
		Ar << GlobalPayloadIdx;
		Ar << DirtyPayloadIdx;
		Ar << LeafIdx;
		Ar << DirtyGridOverflowIdx;
		// Dynamic trees are not serialized
		if (Ar.IsLoading())
		{
			NodeIdx = INDEX_NONE;
		}
	}
};

FORCEINLINE FArchive& operator<<(FArchive& Ar, FAABBTreePayloadInfo& PayloadInfo)
{
	PayloadInfo.Serialize(Ar);
	return Ar;
}

extern CHAOS_API int32 MaxDirtyElements;

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

template <typename TPayloadType, typename TLeafType, bool bMutable = true, typename T = FReal>
class TAABBTree final : public ISpatialAcceleration<TPayloadType, T, 3> 
{
public:
	using PayloadType = TPayloadType;
	static constexpr int D = 3;
	using TType = T;
	static constexpr T DefaultMaxPayloadBounds = 100000;
	static constexpr int32 DefaultMaxChildrenInLeaf = 12;
	static constexpr int32 DefaultMaxTreeDepth = 16;
	static constexpr int32 DefaultMaxNumToProcess = 0; // 0 special value for processing all without timeslicing
	static constexpr ESpatialAcceleration StaticType = TIsSame<TAABBTreeLeafArray<TPayloadType>, TLeafType>::Value ? ESpatialAcceleration::AABBTree : 
		(TIsSame<TBoundingVolume<TPayloadType>, TLeafType>::Value ? ESpatialAcceleration::AABBTreeBV : ESpatialAcceleration::Unknown);
	TAABBTree()
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, bDynamicTree(false)
		, RootNode(INDEX_NONE)
		, FirstFreeInternalNode(INDEX_NONE)
		, FirstFreeLeafNode(INDEX_NONE)
		, MaxChildrenInLeaf(DefaultMaxChildrenInLeaf)
		, MaxTreeDepth(DefaultMaxTreeDepth)
		, MaxPayloadBounds(DefaultMaxPayloadBounds)
		, MaxNumToProcess(DefaultMaxNumToProcess)
		, bShouldRebuild(true)
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
		TreeStats.Reset();
		TreeExpensiveStats.Reset();
		GlobalPayloads.Reset();
		PayloadToInfo.Reset();
		
		OverlappingLeaves.Reset();
		OverlappingOffsets.Reset();
		OverlappingPairs.Reset();
		OverlappingCounts.Reset();
		
		NumProcessedThisSlice = 0;
		WorkStack.Reset();
		WorkPoolFreeList.Reset();
		WorkPool.Reset();

		bShouldRebuild = true;

		RootNode = INDEX_NONE;
		FirstFreeInternalNode = INDEX_NONE;
		FirstFreeLeafNode = INDEX_NONE;

		this->SetAsyncTimeSlicingComplete(true);
	}

	virtual void ProgressAsyncTimeSlicing(bool ForceBuildCompletion) override
	{
		SCOPE_CYCLE_COUNTER(STAT_AABBTreeProgressTimeSlice);

		if (bDynamicTree)
		{
			// Nothing to do
			this->SetAsyncTimeSlicingComplete(true);
			return;
		}

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
	TAABBTree(const TParticles& Particles, int32 InMaxChildrenInLeaf = DefaultMaxChildrenInLeaf, int32 InMaxTreeDepth = DefaultMaxTreeDepth, T InMaxPayloadBounds = DefaultMaxPayloadBounds, int32 InMaxNumToProcess = DefaultMaxNumToProcess, bool bInDynamicTree = false)
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, bDynamicTree(bInDynamicTree)
		, MaxChildrenInLeaf(InMaxChildrenInLeaf)
		, MaxTreeDepth(InMaxTreeDepth)
		, MaxPayloadBounds(InMaxPayloadBounds)
		, MaxNumToProcess(InMaxNumToProcess)
		, bShouldRebuild(true)

	{
		GenerateTree(Particles);
	}

	template <typename ParticleView>
	void Reinitialize(const ParticleView& Particles)
	{
		bShouldRebuild = true;
		GenerateTree(Particles);
	}

	virtual TArray<TPayloadType> FindAllIntersections(const FAABB3& Box) const override { return FindAllIntersectionsImp(Box); }

	bool GetAsBoundsArray(TArray<TAABB<T, 3>>& AllBounds, int32 NodeIdx, int32 ParentNode, TAABB<T, 3>& Bounds)
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

	void CopyFrom(const TAABBTree<TPayloadType, TLeafType, bMutable, T>& Other)
	{
		(*this) = Other;
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>>(new TAABBTree<TPayloadType, TLeafType, bMutable, T>(*this));
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
		return QueryImp<EAABBQueryType::Raycast>(Start, CurData, TVec3<T>(), TAABB<T, 3>(), Visitor, Dir, InvDir, bParallel);
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
		DirtyElementGridCellSize = (T) FAABBTreeDirtyGridCVars::DirtyElementGridCellSize;
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

	// Returns true if there was enough space in the cell to add the new dirty element index or if the element was already added (This second condition should not happen)
	//(The second condition should never be true for the current implementation)
	FORCEINLINE_DEBUGGABLE bool AddNewDirtyParticleIndexToGridCell(int32 Hash, int32 NewDirtyIndex)
	{
		DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(Hash);
		if (HashEntry)
		{
			if (HashEntry->Count < DirtyElementMaxCellCapacity)
			{
				if (ensure(InsertValueIntoSortedSubArray(FlattenedCellArrayOfDirtyIndices, NewDirtyIndex, HashEntry->Index, HashEntry->Count)))
				{
					++(HashEntry->Count);
				}
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
			TreeStats.StatNumNonEmptyCellsInGrid++;
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
				return true;
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
					return true;
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

	FORCEINLINE_DEBUGGABLE int32 AddDirtyElementToGrid(const TAABB<T, 3>& NewBounds, int32 NewDirtyElement)
	{
		bool bAddToGrid = !TooManyOverlapQueryCells(NewBounds, DirtyElementGridCellSizeInv, DirtyElementMaxPhysicalSizeInCells);
		if (!bAddToGrid)
		{
			TreeStats.StatNumElementsTooLargeForGrid++;
		}

		if (bAddToGrid)
		{
			DoForOverlappedCells(NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
				if (!EnoughSpaceInGridCell(Hash))
				{
					bAddToGrid = false;
					return false; // early exit to avoid going through all the cells
				}
				return true;
				});
		}

		if (bAddToGrid)
		{
			DoForOverlappedCells(NewBounds, DirtyElementGridCellSize, DirtyElementGridCellSizeInv, [&](int32 Hash) {
				ensure(AddNewDirtyParticleIndexToGridCell(Hash, NewDirtyElement));
				return true;
				});
		}
		else
		{
			int32 NewOverflowIndex = DirtyElementsGridOverflow.Add(NewDirtyElement);
			return NewOverflowIndex;
		}

		return INDEX_NONE;
	}

	FORCEINLINE_DEBUGGABLE int32 UpdateDirtyElementInGrid(const TAABB<T, 3>& NewBounds, int32 DirtyElementIndex, int32 DirtyGridOverflowIdx)
	{
		if (DirtyGridOverflowIdx == INDEX_NONE)
		{
			const TAABB<T, 3>& OldBounds = DirtyElements[DirtyElementIndex].Bounds;

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
						return true;
					});
				// Add to overflow
				int32 NewOverflowIndex = DirtyElementsGridOverflow.Add(DirtyElementIndex);
				return NewOverflowIndex;
			}
		}
		return DirtyGridOverflowIdx;
	}

	// Expensive function: Don't call unless debugging
	void DynamicTreeDebugStats()
	{
		TArray<FElement> AllElements;
		for (auto& Leaf : Leaves)
		{
			Leaf.GatherElements(AllElements);
		}

		int32 MaxDepth = 0;
		int32 DepthTotal = 0;
		for (const FElement& Element : AllElements)
		{
			FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Element.Payload);
			int32 Depth = 0;
			int32 Node = PayloadInfo->NodeIdx;
			check(Node != INDEX_NONE);
			while (Node != INDEX_NONE)
			{
				Node = Nodes[Node].ParentNode;
				if (Node != INDEX_NONE)
				{
					Depth++;
				}
			}
			if (Depth > MaxDepth)
			{
				MaxDepth = Depth;
			}
			DepthTotal += Depth;
		}
#if !WITH_EDITOR
		CSV_CUSTOM_STAT(ChaosPhysicsTimers, MaximumTreeDepth, MaxDepth, ECsvCustomStatOp::Max);
		CSV_CUSTOM_STAT(ChaosPhysicsTimers, AvgTreeDepth, DepthTotal / AllElements.Num(), ECsvCustomStatOp::Max);
		CSV_CUSTOM_STAT(ChaosPhysicsTimers, Dirty,DirtyElements.Num(), ECsvCustomStatOp::Max);
#endif

	}

	int32 AllocateInternalNode()
	{
		int32 AllocatedNodeIdx = FirstFreeInternalNode;
		if (FirstFreeInternalNode != INDEX_NONE)
		{
			// Unlink from free list
			FirstFreeInternalNode = Nodes[FirstFreeInternalNode].ChildrenNodes[1];
		}
		else
		{
			// create the actual node space
			AllocatedNodeIdx = Nodes.AddUninitialized(1);;
			Nodes[AllocatedNodeIdx].bLeaf = false;
		}

		return AllocatedNodeIdx;
	}

	int32 AllocateLeafNodeAndLeaf(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds)
	{
		int32 AllocatedNodeIdx = FirstFreeLeafNode;
		int32 LeafIndex;
		if (FirstFreeLeafNode != INDEX_NONE)
		{
			FirstFreeLeafNode = Nodes[FirstFreeLeafNode].ChildrenNodes[1];
			LeafIndex = Nodes[AllocatedNodeIdx].ChildrenNodes[0]; // This is already set when it was allocated for the first time
			FElement NewElement{ Payload, NewBounds };
			Leaves[LeafIndex].AddElement(NewElement);
		}
		else
		{
			LeafIndex = Leaves.Num();

			// create the actual node space
			AllocatedNodeIdx = Nodes.AddUninitialized(1);
			Nodes[AllocatedNodeIdx].ChildrenNodes[0] = LeafIndex;
			Nodes[AllocatedNodeIdx].bLeaf = true;

			FElement NewElement{ Payload, NewBounds };
			TArray<FElement> SingleElementArray;
			SingleElementArray.Add(NewElement);
			Leaves.Add(TLeafType{ SingleElementArray }); // Extra copy
		}

		// Expand the leaf node bounding box to reduce the number of updates
		TAABB<T, 3> ExpandedBounds = NewBounds;
		ExpandedBounds.Thicken(FAABBTreeCVars::DynamicTreeBoundingBoxPadding);
		Nodes[AllocatedNodeIdx].ChildrenBounds[0] = ExpandedBounds;

		Nodes[AllocatedNodeIdx].ParentNode = INDEX_NONE;
		FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload);
		check(PayloadInfo);
		PayloadInfo->LeafIdx = LeafIndex;
		PayloadInfo->NodeIdx = AllocatedNodeIdx;

		return AllocatedNodeIdx;
	}

	void DeAllocateInternalNode(int32 NodeIdx)
	{
		Nodes[NodeIdx].ChildrenNodes[1] = FirstFreeInternalNode;
		FirstFreeInternalNode = NodeIdx;
	}

	void  DeAllocateLeafNode(int32 NodeIdx)
	{
		
		Leaves[Nodes[NodeIdx].ChildrenNodes[0]].Reset();

		Nodes[NodeIdx].ChildrenNodes[1] = FirstFreeLeafNode;
		FirstFreeLeafNode = NodeIdx;
	}

	// Is the input node Child 0 or Child 1?
	int32 WhichChildAmI(int32 NodeIdx)
	{
		check(NodeIdx != INDEX_NONE);
		int32 ParentIdx = Nodes[NodeIdx].ParentNode;
		check(ParentIdx != INDEX_NONE);
		if (Nodes[ParentIdx].ChildrenNodes[0] == NodeIdx)
		{
			return  0;
		}
		else
		{
			return 1;
		}
	}

	// Is the input node Child 0 or Child 1?
	int32 GetSiblingIndex(int32 NodeIdx)
	{
		return(WhichChildAmI(NodeIdx) ^ 1);
	}

	int32 FindBestSibling(const TAABB<T, 3>& InNewBounds, bool& bOutAddToLeaf)
	{
		bOutAddToLeaf = false;
		if (RootNode == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		
		TAABB<T, 3> NewBounds = InNewBounds;
		NewBounds.Thicken(FAABBTreeCVars::DynamicTreeBoundingBoxPadding);

		//Priority Q of indices to explore
		TArray<int32> PriorityQ;
		PriorityQ.Reserve(10);

		TArray<FReal> SumDeltaCostQ;
		SumDeltaCostQ.Reserve(10);

		int32 QIndex = 0;
		
		// Initializing
		
		TAABB<T, 3> WorkingAABB{ NewBounds };
		WorkingAABB.GrowToInclude(Nodes[RootNode].ChildrenBounds[0]);
		if (!Nodes[RootNode].bLeaf)
		{
			WorkingAABB.GrowToInclude(Nodes[RootNode].ChildrenBounds[1]);
		}

		int32 BestSiblingIdx = RootNode;
		FReal BestCost = WorkingAABB.GetArea();
		PriorityQ.Add(RootNode);
		SumDeltaCostQ.Add(0.0f);

		while (PriorityQ.Num() - QIndex)
		{
			// Pop from queue
			uint32 TestSibling = PriorityQ[QIndex];
			FReal SumDeltaCost = SumDeltaCostQ[QIndex];
			QIndex++;

			// Alternative is a stack (this is not very optimal so don't use it)
			//uint32 TestSibling = PriorityQ[PriorityQ.Num() - 1];
			//FReal SumDeltaCost = SumDeltaCostQ[SumDeltaCostQ.Num() - 1];
			//PriorityQ.SetNumUninitialized(PriorityQ.Num() - 1);
			//SumDeltaCostQ.SetNumUninitialized(SumDeltaCostQ.Num() - 1);

			// TestSibling bounds union with new bounds
			bool bAddToLeaf = false;
			WorkingAABB = Nodes[TestSibling].ChildrenBounds[0];
			if (!Nodes[TestSibling].bLeaf)
			{
				WorkingAABB.GrowToInclude(Nodes[TestSibling].ChildrenBounds[1]);
			}
			else
			{
				int32 LeafIdx = Nodes[TestSibling].ChildrenNodes[0];
				bAddToLeaf = Leaves[LeafIdx].GetElementCount() < FAABBTreeCVars::DynamicTreeLeafCapacity;
			}
			FReal TestSiblingArea = WorkingAABB.GetArea();
			WorkingAABB.GrowToInclude(NewBounds);

			FReal NewPotentialNodeArea = WorkingAABB.GetArea();
			FReal CostForChoosingNode = NewPotentialNodeArea + SumDeltaCost;
			if (bAddToLeaf)
			{
				// No new node is added (we can experiment with this cost function
				// It is faster overall if we don't subtract here
				//CostForChoosingNode -= TestSiblingArea;
			}
			FReal NewDeltaCost = NewPotentialNodeArea - TestSiblingArea;
			// Did we get a better cost?
			if (CostForChoosingNode < BestCost)
			{
				BestCost = CostForChoosingNode;
				BestSiblingIdx = TestSibling;
				bOutAddToLeaf = bAddToLeaf;
			}

			// Lower bound of Children costs
			FReal ChildCostLowerBound = NewBounds.GetArea() + NewDeltaCost + SumDeltaCost;

			if (!Nodes[TestSibling].bLeaf && ChildCostLowerBound < BestCost)
			{
				// Now we will push the children
				PriorityQ.Add(Nodes[TestSibling].ChildrenNodes[0]);
				PriorityQ.Add(Nodes[TestSibling].ChildrenNodes[1]);
				SumDeltaCostQ.Add(NewDeltaCost + SumDeltaCost);
				SumDeltaCostQ.Add(NewDeltaCost + SumDeltaCost);
			}

		}

		return BestSiblingIdx;
	}

	// Rotate nodes to decrease tree cost
	// Grandchildren can swap with their aunts
	void RotateNode(uint32 NodeIdx, bool debugAssert = false)
	{
		int32 BestGrandChildToSwap = INDEX_NONE; // GrandChild of NodeIdx
		int32 BestAuntToSwap = INDEX_NONE; // Aunt of BestGrandChildToSwap
		FReal BestDeltaCost = 0.0f; // Negative values are cost reductions, doing nothing changes the cost with 0

		check(!Nodes[NodeIdx].bLeaf);
		// Check both children of NodeIdx
		for (uint32 AuntLocalIdx = 0; AuntLocalIdx < 2; AuntLocalIdx++)
		{
			int32 Aunt = Nodes[NodeIdx].ChildrenNodes[AuntLocalIdx];
			int32 Mother = Nodes[NodeIdx].ChildrenNodes[AuntLocalIdx ^ 1];
			if (Nodes[Mother].bLeaf)
			{
				continue;
			}
			for (int32 GrandChild : Nodes[Mother].ChildrenNodes)
			{
				// Only the Mother's cost will change
				TAABB<T, 3> NewMotherAABB{ Nodes[NodeIdx].ChildrenBounds[AuntLocalIdx]}; // Aunt will be under mother now
				NewMotherAABB.GrowToInclude(Nodes[Mother].ChildrenBounds[GetSiblingIndex(GrandChild)]); // Add the Grandchild's sibling cost
				FReal MotherCostWithoutRotation = Nodes[NodeIdx].ChildrenBounds[AuntLocalIdx ^ 1].GetArea();
				FReal MotherCostWithRotation = NewMotherAABB.GetArea();
				FReal DeltaCost = MotherCostWithRotation - MotherCostWithoutRotation;

				if (DeltaCost < BestDeltaCost)
				{
					BestDeltaCost = DeltaCost;
					BestAuntToSwap = Aunt;
					BestGrandChildToSwap = GrandChild;
				}

			}
		}

		// Now do the rotation if required
		if (BestGrandChildToSwap != INDEX_NONE)
		{
			if (debugAssert)
			{
				check(false);
			}

			int32 AuntLocalChildIdx = WhichChildAmI(BestAuntToSwap);
			int32 GrandChildLocalChildIdx = WhichChildAmI(BestGrandChildToSwap);

			int32 MotherOfBestGrandChild = Nodes[BestGrandChildToSwap].ParentNode;

			// Modify NodeIdx 
			Nodes[NodeIdx].ChildrenNodes[AuntLocalChildIdx] = BestGrandChildToSwap;
			// Modify BestGrandChildToSwap
			Nodes[BestGrandChildToSwap].ParentNode = NodeIdx;
			// Modify BestAuntToSwap
			Nodes[BestAuntToSwap].ParentNode = MotherOfBestGrandChild;
			// Modify MotherOfBestGrandChild
			Nodes[MotherOfBestGrandChild].ChildrenNodes[GrandChildLocalChildIdx] = BestAuntToSwap;
			// Swap the bounds
			TAABB<T, 3> AuntAABB = Nodes[NodeIdx].ChildrenBounds[AuntLocalChildIdx];
			Nodes[NodeIdx].ChildrenBounds[AuntLocalChildIdx] = Nodes[MotherOfBestGrandChild].ChildrenBounds[GrandChildLocalChildIdx];
			Nodes[MotherOfBestGrandChild].ChildrenBounds[GrandChildLocalChildIdx] = AuntAABB;
			// Update the other child bound of NodeIdx
			Nodes[NodeIdx].ChildrenBounds[AuntLocalChildIdx ^ 1] = Nodes[MotherOfBestGrandChild].ChildrenBounds[0];
			Nodes[NodeIdx].ChildrenBounds[AuntLocalChildIdx ^ 1].GrowToInclude(Nodes[MotherOfBestGrandChild].ChildrenBounds[1]);
		}
	}

	void InsertLeaf(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds)
	{

		// Slow Debug Code
		//if (GetUniqueIdx(Payload).Idx == 5)
		//{
		//	DynamicTreeDebugStats();
		//}

		// Find the best sibling
		bool bAddToLeaf;
		int32 BestSibling = FindBestSibling(NewBounds, bAddToLeaf);

		if (bAddToLeaf)
		{
			int32 LeafIdx = Nodes[BestSibling].ChildrenNodes[0];
			Leaves[LeafIdx].AddElement(TPayloadBoundsElement<TPayloadType, T>{Payload, NewBounds});
			Leaves[LeafIdx].RecomputeBounds();
			TAABB<T, 3> ExpandedBounds = Leaves[LeafIdx].GetBounds();
			ExpandedBounds.Thicken(FAABBTreeCVars::DynamicTreeBoundingBoxPadding);
			Nodes[BestSibling].ChildrenBounds[0] = ExpandedBounds;
			UpdateAncestorBounds(BestSibling, true);
			FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload);
			PayloadInfo->LeafIdx = LeafIdx;
			PayloadInfo->NodeIdx = BestSibling;
			return;
		}

		int32 NewLeafNode = AllocateLeafNodeAndLeaf(Payload, NewBounds);

		// New tree?
		if (RootNode == INDEX_NONE)
		{
			RootNode = NewLeafNode;
			return;
		}

		// New internal parent node
		int32 oldParent = Nodes[BestSibling].ParentNode;
		int32 newParent = AllocateInternalNode();
		Nodes[newParent].ParentNode = oldParent;
		Nodes[newParent].ChildrenNodes[0] = BestSibling;
		Nodes[newParent].ChildrenNodes[1] = NewLeafNode;
		Nodes[newParent].ChildrenBounds[0] = Nodes[BestSibling].ChildrenBounds[0];
		if (!Nodes[BestSibling].bLeaf)
		{
			Nodes[newParent].ChildrenBounds[0].GrowToInclude(Nodes[BestSibling].ChildrenBounds[1]);
		}
		Nodes[newParent].ChildrenBounds[1] = Nodes[NewLeafNode].ChildrenBounds[0];

		if (oldParent != INDEX_NONE)
		{
			int32 ChildIdx = WhichChildAmI(BestSibling);
			Nodes[oldParent].ChildrenNodes[ChildIdx] = newParent;
		}
		else
		{
			RootNode = newParent;
		}

		Nodes[BestSibling].ParentNode = newParent;
		Nodes[NewLeafNode].ParentNode = newParent;

		UpdateAncestorBounds(newParent, true);

	}	

	void  UpdateAncestorBounds(int32 NodeIdx, bool bDoRotation = false)
	{
		int32 CurrentNodeIdx = NodeIdx;
		int32 ParentNodeIdx = Nodes[NodeIdx].ParentNode;


		// This should not be required
		/*if (bDoRotation && NodeIdx != INDEX_NONE)
		{
			RotateNode(NodeIdx,true); 
		}*/
		
		while (ParentNodeIdx != INDEX_NONE)
		{
			int32 ChildIndex = WhichChildAmI(CurrentNodeIdx);
			Nodes[ParentNodeIdx].ChildrenBounds[ChildIndex] = Nodes[CurrentNodeIdx].ChildrenBounds[0];
			if (!Nodes[CurrentNodeIdx].bLeaf)
			{
				Nodes[ParentNodeIdx].ChildrenBounds[ChildIndex].GrowToInclude(Nodes[CurrentNodeIdx].ChildrenBounds[1]);
			}

			if (bDoRotation)
			{
				RotateNode(ParentNodeIdx);
			}

			CurrentNodeIdx = ParentNodeIdx;
			ParentNodeIdx = Nodes[CurrentNodeIdx].ParentNode;
		}
	}

	void RemoveLeafNode(int32 LeafNodeIdx, const TPayloadType& Payload)
	{
		int32 LeafIdx = Nodes[LeafNodeIdx].ChildrenNodes[0];

		if (Leaves[LeafIdx].GetElementCount() > 1)
		{
			Leaves[LeafIdx].RemoveElement(Payload);
			Leaves[LeafIdx].RecomputeBounds();
			TAABB<T, 3> ExpandedBounds = Leaves[LeafIdx].GetBounds();
			ExpandedBounds.Thicken(FAABBTreeCVars::DynamicTreeBoundingBoxPadding);
			Nodes[LeafNodeIdx].ChildrenBounds[0] = ExpandedBounds;
			UpdateAncestorBounds(LeafNodeIdx);
			return;
		}

		int32 ParentNodeIdx = Nodes[LeafNodeIdx].ParentNode;

		if (ParentNodeIdx != INDEX_NONE)
		{
			int32 GrandParentNodeIdx = Nodes[ParentNodeIdx].ParentNode;
			int32 SiblingNodeLocalIdx = GetSiblingIndex(LeafNodeIdx);
			int32 SiblingNodeIdx = Nodes[ParentNodeIdx].ChildrenNodes[SiblingNodeLocalIdx];

			if (GrandParentNodeIdx != INDEX_NONE)
			{
				int32 ChildLocalIdx = WhichChildAmI(ParentNodeIdx);
				Nodes[GrandParentNodeIdx].ChildrenNodes[ChildLocalIdx] = SiblingNodeIdx;
			}
			else
			{
				RootNode = SiblingNodeIdx;
			}
			Nodes[SiblingNodeIdx].ParentNode = GrandParentNodeIdx;
			UpdateAncestorBounds(SiblingNodeIdx);
			DeAllocateInternalNode(ParentNodeIdx);
		}
		else
		{
			RootNode = INDEX_NONE;
		}
		DeAllocateLeafNode(LeafNodeIdx);
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
					if (bDynamicTree)
					{
						RemoveLeafNode(PayloadInfo->NodeIdx, Payload);
					}
					else
					{
						Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
					}
				}

				PayloadToInfo.Remove(Payload);
				bShouldRebuild = true;
			}
		}
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds, bool bInHasBounds) override
	{
#if !WITH_EDITOR
		//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBTreeUpdateElement)
		//CSV_CUSTOM_STAT(ChaosPhysicsTimers, 1, 1, ECsvCustomStatOp::Accumulate);
		//CSV_CUSTOM_STAT(PhysicsCounters, NumIntoNP, 1, ECsvCustomStatOp::Accumulate);
#endif

		bool bHasBounds = bInHasBounds;
		// If bounds are bad, use global
		if (bDynamicTree && bHasBounds && ValidateBounds(NewBounds) == false)
		{
			bHasBounds = false;
			ensureMsgf(false, TEXT("AABBTree encountered invalid bounds input. Forcing element to global payload. Min: %s Max: %s. If Bounds are valid but large, increase FAABBTreeCVars::MaxNonGlobalElementBoundsExtrema."),
				*NewBounds.Min().ToString(), *NewBounds.Max().ToString());
		}

		if (ensure(bMutable))
		{
			FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload);
			if (PayloadInfo)
			{
				if (PayloadInfo->LeafIdx != INDEX_NONE)
				{
					//If we are still within the same leaf bounds, do nothing, don't detect a change either
					if (bHasBounds) 
					{
						if (bDynamicTree)
						{
							// The leaf node bounds can be larger than the actual leave bound
							const TAABB<T, 3>& LeafNodeBounds = Nodes[PayloadInfo->NodeIdx].ChildrenBounds[0];
							if (LeafNodeBounds.Contains(NewBounds.Min()) && LeafNodeBounds.Contains(NewBounds.Max()))
							{
								// We still need to update the constituent bounds
								Leaves[PayloadInfo->LeafIdx].UpdateElement(Payload, NewBounds, bHasBounds);
								Leaves[PayloadInfo->LeafIdx].RecomputeBounds();
								return;
							}
						}
						else
						{
							const TAABB<T, 3>& LeafBounds = Leaves[PayloadInfo->LeafIdx].GetBounds();
							if (LeafBounds.Contains(NewBounds.Min()) && LeafBounds.Contains(NewBounds.Max()))
							{
								// We still need to update the constituent bounds
								Leaves[PayloadInfo->LeafIdx].UpdateElement(Payload, NewBounds, bHasBounds);
								return;
							}
						}
					}

					// DBVH remove from tree

					if (bDynamicTree)
					{
						RemoveLeafNode(PayloadInfo->NodeIdx, Payload);
						PayloadInfo->LeafIdx = INDEX_NONE;
						PayloadInfo->NodeIdx = INDEX_NONE;
					}
					else
					{
						Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
						PayloadInfo->LeafIdx = INDEX_NONE;
					}
				}
			}
			else
			{
				PayloadInfo = &PayloadToInfo.Add(Payload);
			}

			bShouldRebuild = true;

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
					if (bDynamicTree)
					{
						InsertLeaf(Payload, NewBounds);
					}
					else
					{
						PayloadInfo->DirtyPayloadIdx = DirtyElements.Add(FElement{ Payload, NewBounds });
						if (DirtyElementGridEnabled())
						{
							//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBAddElement)
							PayloadInfo->DirtyGridOverflowIdx = AddDirtyElementToGrid(NewBounds, PayloadInfo->DirtyPayloadIdx);
						}
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
				TAABB<T, 3> GlobalBounds = bTooBig ? NewBounds : TAABB<T, 3>(TVec3<T>(TNumericLimits<T>::Lowest()), TVec3<T>(TNumericLimits<T>::Max()));
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

		if(!bDynamicTree && DirtyElements.Num() > MaxDirtyElements)
		{
			UE_LOG(LogChaos, Verbose, TEXT("Bounding volume exceeded maximum dirty elements (%d dirty of max %d) and is forcing a tree rebuild."), DirtyElements.Num(), MaxDirtyElements);
			ReoptimizeTree();
		}
	}

	int32 NumDirtyElements() const
	{
		return DirtyElements.Num();
	}

	// Some useful statistics
	const AABBTreeStatistics& GetAABBTreeStatistics()
	{
		// Update the stats that needs it first
		TreeStats.StatNumDirtyElements = DirtyElements.Num();
		TreeStats.StatNumGridOverflowElements = DirtyElementsGridOverflow.Num();
		return TreeStats;
	}

	const AABBTreeExpensiveStatistics& GetAABBTreeExpensiveStatistics()
	{
		TreeExpensiveStats.StatMaxDirtyElements = DirtyElements.Num();
		TreeExpensiveStats.StatMaxNumLeaves = Leaves.Num();
		int32 StatMaxLeafSize = 0;
		for(const TLeafType& Leaf : Leaves)
		{
			StatMaxLeafSize = FMath::Max(StatMaxLeafSize, (int32)Leaf.GetElementCount());
		}
		TreeExpensiveStats.StatMaxLeafSize = StatMaxLeafSize;
		TreeExpensiveStats.StatMaxTreeDepth = (Nodes.Num() == 0) ? 0 : GetSubtreeDepth(0);
		TreeExpensiveStats.StatGlobalPayloadsSize = GlobalPayloads.Num();

		return TreeExpensiveStats;
	}

	const int32 GetSubtreeDepth(const int32 NodeIdx)
	{
		const FNode& Node = Nodes[NodeIdx];
		if (Node.bLeaf)
		{
			return 1;
		}
		else
		{
			return FMath::Max(GetSubtreeDepth(Node.ChildrenNodes[0]), GetSubtreeDepth(Node.ChildrenNodes[1])) + 1;
		}
	}

	const TArray<TPayloadBoundsElement<TPayloadType, T>>& GlobalObjects() const
	{
		return GlobalPayloads;
	}


	virtual bool ShouldRebuild() override { return bDynamicTree ? false : bShouldRebuild; }  // Used to find out if something changed since last reset for optimizations
	// Contract: bShouldRebuild can only ever be cleared by calling the ClearShouldRebuild method, it can be set at will though
	virtual void ClearShouldRebuild() override { bShouldRebuild = false; }

	virtual bool IsTreeDynamic() const override { return bDynamicTree; }
	bool SetTreeToDynamic() { bDynamicTree = true; } // Tree cannot be changed back to static for now

	virtual void PrepareCopyTimeSliced(const  ISpatialAcceleration<TPayloadType, T, 3>& InFrom) override
	{
		check(this != &InFrom);
		check(InFrom.GetType() == ESpatialAcceleration::AABBTree);
		const TAABBTree<TPayloadType, TLeafType, bMutable, T>& From = static_cast<const TAABBTree<TPayloadType, TLeafType, bMutable, T>&>(InFrom);

		Reset();

		// Copy all the small objects first

		ISpatialAcceleration<TPayloadType, T, 3>::operator=(From);

		DirtyElementGridCellSize = From.DirtyElementGridCellSize;
		DirtyElementGridCellSizeInv = From.DirtyElementGridCellSizeInv;
		DirtyElementMaxGridCellQueryCount = From.DirtyElementMaxGridCellQueryCount;
		DirtyElementMaxPhysicalSizeInCells = From.DirtyElementMaxPhysicalSizeInCells;
		DirtyElementMaxCellCapacity = From.DirtyElementMaxCellCapacity;

		MaxChildrenInLeaf = From.MaxChildrenInLeaf;
		MaxTreeDepth = From.MaxTreeDepth;
		MaxPayloadBounds = From.MaxPayloadBounds;
		MaxNumToProcess = From.MaxNumToProcess;
		NumProcessedThisSlice = From.NumProcessedThisSlice;
		bShouldRebuild = From.bShouldRebuild;

		RootNode = From.RootNode;
		FirstFreeInternalNode = From.FirstFreeInternalNode;
		FirstFreeLeafNode = From.FirstFreeLeafNode;

		// Reserve sizes for arrays etc

		Nodes.Reserve(From.Nodes.Num());
		Leaves.Reserve(From.Leaves.Num());
		DirtyElements.Reserve(From.DirtyElements.Num());
		CellHashToFlatArray.Reserve(From.CellHashToFlatArray.Num());
		FlattenedCellArrayOfDirtyIndices.Reserve(From.FlattenedCellArrayOfDirtyIndices.Num());
		DirtyElementsGridOverflow.Reserve(From.DirtyElementsGridOverflow.Num());
		GlobalPayloads.Reserve(From.GlobalPayloads.Num());
		PayloadToInfo.Reserve(From.PayloadToInfo.Num());
		OverlappingLeaves.Reserve(From.OverlappingLeaves.Num());
		OverlappingOffsets.Reserve(From.OverlappingOffsets.Num());
		OverlappingPairs.Reserve(From.OverlappingPairs.Num());
		OverlappingCounts.Reserve(From.OverlappingCounts.Num());

		this->SetAsyncTimeSlicingComplete(false);
	}
	
	virtual void ProgressCopyTimeSliced(const  ISpatialAcceleration<TPayloadType, T, 3>& InFrom, int MaximumBytesToCopy) override
	{
		check(this != &InFrom);
		check(InFrom.GetType() == ESpatialAcceleration::AABBTree);
		const TAABBTree<TPayloadType, TLeafType, bMutable, T>& From = static_cast<const TAABBTree<TPayloadType, TLeafType, bMutable, T>&>(InFrom);

		int32 SizeToCopyLeft = MaximumBytesToCopy;
		check(From.CellHashToFlatArray.Num() == 0); // Partial Copy of TMAPs not implemented, and this should be empty for our current use cases

		if (!ContinueTimeSliceCopy(From.Nodes, Nodes, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.Leaves, Leaves, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.DirtyElements, DirtyElements, SizeToCopyLeft))
		{
			return;
		}

		if (!ContinueTimeSliceCopy(From.FlattenedCellArrayOfDirtyIndices, FlattenedCellArrayOfDirtyIndices, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.DirtyElementsGridOverflow, DirtyElementsGridOverflow, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.GlobalPayloads, GlobalPayloads, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.PayloadToInfo, PayloadToInfo, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.OverlappingLeaves, OverlappingLeaves, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.OverlappingOffsets, OverlappingOffsets, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.OverlappingCounts, OverlappingCounts, SizeToCopyLeft))
		{
			return;
		}
		if (!ContinueTimeSliceCopy(From.OverlappingPairs, OverlappingPairs, SizeToCopyLeft))
		{
			return;
		}

		this->SetAsyncTimeSlicingComplete(true);
	}

	// Returns true if bounds appear valid. Returns false if extremely large values, contains NaN, or is empty.
	FORCEINLINE_DEBUGGABLE bool ValidateBounds(const TAABB<T, 3>& Bounds)
	{
		const TVec3<T>& Min = Bounds.Min();
		const TVec3<T>& Max = Bounds.Max();

		for (int32 i = 0; i < 3; ++i)
		{
			const T& MinComponent = Min[i];
			const T& MaxComponent = Max[i];

			// If element is extremely far out on any axis, if past limit return false to make it a global element and prevent huge numbers poisoning splitting algorithm computation.
			if (MinComponent <= -FAABBTreeCVars::MaxNonGlobalElementBoundsExtrema || MaxComponent >= FAABBTreeCVars::MaxNonGlobalElementBoundsExtrema)
			{
				return false;
			}

			// Are we an empty aabb?
			if (MinComponent > MaxComponent)
			{
				return false;
			}

			// Are we NaN/Inf?
			if (!FMath::IsFinite(MinComponent) || !FMath::IsFinite(MaxComponent))
			{
				return false;
			}
		}

		return true;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedAABBTreeFullBounds)
		{
			// Serialize out unused aabb for earlier versions
			TAABB<T, 3> Dummy(TVec3<T>((T)0), TVec3<T>((T)0));
			TBox<T, 3>::SerializeAsAABB(Ar, Dummy);
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
			bShouldRebuild = true;
		}

		// Dynamic trees are not serialized/deserialized for now
		if (Ar.IsLoading())
		{
			bDynamicTree = false;
			RootNode = INDEX_NONE;
			FirstFreeInternalNode = INDEX_NONE;
			FirstFreeLeafNode = INDEX_NONE;
		}
		else
		{
			ensure(bDynamicTree == false);
		}
	}
	
	/** Given a first node and a leaf index find the overlapping leaves and update the node stack 
	 * @param FirstNode First node to be added to the stack
	 * @param LeafIndex Leaf index for which we want to find the overlapping leaves
	 * @param Nodestack Node stack that will be used to traverse the tree and find the overlapping leaves
	 */
	void FindOverlappingLeaf(const int32 FirstNode, const int32 LeafIndex, TArray<int32>& NodeStack) 
	{
		const TAABB<T,3>& LeafBounds = Leaves[LeafIndex].GetBounds();
				
		NodeStack.Reset();
		NodeStack.Add(FirstNode);
		
		int32 NodeIndex = INDEX_NONE;
		while (NodeStack.Num())
		{
			NodeIndex = NodeStack.Pop(false);
			const FNode& Node = Nodes[NodeIndex];
			
			// If a leaf directly test the bounds
			if (Node.bLeaf)
			{
				if (LeafBounds.Intersects(Leaves[Node.ChildrenNodes[0]].GetBounds()))
				{
					OverlappingLeaves.Add(Node.ChildrenNodes[0]);
				}
			}
			else
			{
				// If not loop over all the children nodes to check if they intersect the bounds
				for(int32 ChildIndex = 0; ChildIndex < 2; ++ChildIndex)
				{
					if(LeafBounds.Intersects(Node.ChildrenBounds[ChildIndex]) && Node.ChildrenNodes[ChildIndex] != INDEX_NONE)
					{
						NodeStack.Add(Node.ChildrenNodes[ChildIndex]);
					}
				}
			}
		}
	}
	
	/** Recursively add overlapping leaves given 2 nodes in the tree */
    void AddNodesOverlappingLeaves(const TAABBTreeNode<T>& LeftNode, const TAABB<T, 3>& LeftBounds,
								  const TAABBTreeNode<T>& RightNode, const TAABB<T, 3>& RightBounds, const bool bDirtyFilter)
	{
		// If dirty filter enabled only look for overlapping leaves if one of the 2 nodes are dirty 
		if(!bDirtyFilter || (bDirtyFilter && (LeftNode.bDirtyNode || RightNode.bDirtyNode)))
		{
			if(LeftBounds.Intersects(RightBounds))
			{
				// If left and right are leaves check for intersection
				if(LeftNode.bLeaf && RightNode.bLeaf)
				{
					const int32 LeftLeaf = LeftNode.ChildrenNodes[0];
					const int32 RightLeaf = RightNode.ChildrenNodes[0];

					// Same condition as for the nodes
					if(!bDirtyFilter || (bDirtyFilter && (Leaves[LeftLeaf].IsLeafDirty() || Leaves[RightLeaf].IsLeafDirty())))
					{
						if(Leaves[LeftLeaf].GetBounds().Intersects(Leaves[RightLeaf].GetBounds()))
						{
							OverlappingPairs.Add(FIntVector2(LeftLeaf, RightLeaf));
							++OverlappingCounts[LeftLeaf];
							++OverlappingCounts[RightLeaf];
						}
					}
				}
				// If only left is a leaf continue recursion with the right node children
				else if(LeftNode.bLeaf)
				{
					AddNodesOverlappingLeaves(LeftNode, LeftBounds, Nodes[RightNode.ChildrenNodes[0]], RightNode.ChildrenBounds[0], bDirtyFilter);
					AddNodesOverlappingLeaves(LeftNode, LeftBounds, Nodes[RightNode.ChildrenNodes[1]], RightNode.ChildrenBounds[1], bDirtyFilter);
				}
				// Otherwise continue recursion with the left node children
				else
				{
					AddNodesOverlappingLeaves(Nodes[LeftNode.ChildrenNodes[0]], LeftNode.ChildrenBounds[0], RightNode, RightBounds, bDirtyFilter);
					AddNodesOverlappingLeaves(Nodes[LeftNode.ChildrenNodes[1]], LeftNode.ChildrenBounds[1], RightNode, RightBounds, bDirtyFilter);
				}
			}
		}
	}
	
	/** Recursively add overlapping leaves given a root node in the tree */
	void AddRootOverlappingLeaves(const TAABBTreeNode<T>& TreeNode, const bool bDirtyFilter)
	{
		if(!TreeNode.bLeaf)
		{
			// Find overlapping leaves within the left and right children
			AddRootOverlappingLeaves(Nodes[TreeNode.ChildrenNodes[0]], bDirtyFilter);
			AddRootOverlappingLeaves(Nodes[TreeNode.ChildrenNodes[1]], bDirtyFilter);

			// Then try finding some overlaps in between the 2 children
			AddNodesOverlappingLeaves(Nodes[TreeNode.ChildrenNodes[0]], TreeNode.ChildrenBounds[0],
									  Nodes[TreeNode.ChildrenNodes[1]], TreeNode.ChildrenBounds[1], bDirtyFilter);
		}
	}

	/** Fill the overlapping pairs from the previous persistent and not dirty leaves */
	void FillPersistentOverlappingPairs()
	{
		for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
		{
			int32 NumOverlaps = 0;
			if(!Leaves[LeafIndex].IsLeafDirty())
			{
				for(int32 OverlappingIndex = OverlappingOffsets[LeafIndex]; OverlappingIndex < OverlappingOffsets[LeafIndex+1]; ++OverlappingIndex)
				{
					if(!Leaves[OverlappingLeaves[OverlappingIndex]].IsLeafDirty())
					{
						if(LeafIndex < OverlappingLeaves[OverlappingIndex])
						{
							OverlappingPairs.Add(FIntVector2(LeafIndex, OverlappingLeaves[OverlappingIndex]));
						}
						if(LeafIndex != OverlappingLeaves[OverlappingIndex])
						{
							++NumOverlaps;
						}
					}
				}
			}
			// Make sure the leaf is intersecting itself if several elements per leaf
			OverlappingPairs.Add(FIntVector2(LeafIndex, LeafIndex));
			++NumOverlaps;
			
			OverlappingCounts[LeafIndex] = NumOverlaps;
		}
	}
	/** Propagates the leaves dirty flag up to the root node */
	void PropagateLeavesDirtyFlag()
	{
		for(int32 NodeIndex = 0, NumNodes = Nodes.Num(); NodeIndex < NumNodes; ++NodeIndex)
		{
			if(Nodes[NodeIndex].bLeaf)
			{
				Nodes[NodeIndex].bDirtyNode = Leaves[Nodes[NodeIndex].ChildrenNodes[0]].IsLeafDirty();
				if(Nodes[NodeIndex].bDirtyNode)
				{
					int32 NodeParent = Nodes[NodeIndex].ParentNode;
					while(NodeParent != INDEX_NONE && !Nodes[NodeParent].bDirtyNode)
					{
						Nodes[NodeParent].bDirtyNode = true;
						NodeParent = Nodes[NodeParent].ParentNode;
					}
				}
			}
		}
	}

	/** Simultaneous tree descent to compute the overlapping leaves */
	void ComputeOverlappingCacheFromRoot(const bool bDirtyFilter)
	{
		if(!bDynamicTree || (bDynamicTree && RootNode == INDEX_NONE)) return;
		
		OverlappingOffsets.SetNum(Leaves.Num()+1, false);
		OverlappingCounts.SetNum(Leaves.Num(), false);
		OverlappingPairs.Reset();
		
		if(bDirtyFilter)
		{
			FillPersistentOverlappingPairs();
			PropagateLeavesDirtyFlag();
		}
		else
		{
			for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
			{
				// Make sure the leaf is intersecting itself if several elements per leaf
				OverlappingPairs.Add(FIntVector2(LeafIndex, LeafIndex));
				OverlappingCounts[LeafIndex] = 1;
			}
		}
		AddRootOverlappingLeaves(Nodes[RootNode], bDirtyFilter);

		OverlappingOffsets[0] = 0;
		for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
		{
			OverlappingOffsets[LeafIndex+1] = OverlappingOffsets[LeafIndex] + OverlappingCounts[LeafIndex];
			OverlappingCounts[LeafIndex] = OverlappingOffsets[LeafIndex];
		}
		OverlappingLeaves.SetNum(OverlappingOffsets.Last(), false);
		for(auto& OverlappingPair : OverlappingPairs)
		{
			if(OverlappingPair[0] != OverlappingPair[1])
			{
				OverlappingLeaves[OverlappingCounts[OverlappingPair[0]]++] = OverlappingPair[1];
				OverlappingLeaves[OverlappingCounts[OverlappingPair[1]]++] = OverlappingPair[0];
			}
			else
			{
				OverlappingLeaves[OverlappingCounts[OverlappingPair[0]]++] = OverlappingPair[0];
			}
		}
		if(bDirtyFilter)
		{
			for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
			{
				Leaves[LeafIndex].SetDirtyState(false);
			}
			for(int32 NodeIndex = 0, NumNodes = Nodes.Num(); NodeIndex < NumNodes; ++NodeIndex)
			{
				Nodes[NodeIndex].bDirtyNode = false;
			}
		}
	}

	/** Sequential loop over the leaves to fill the overlapping pairs */
	void ComputeOverlappingCacheFromLeaf()
	{
		OverlappingOffsets.SetNum(Leaves.Num()+1, false);
		OverlappingLeaves.Reset();
		
		if(!bDynamicTree || (bDynamicTree && RootNode == INDEX_NONE)) return;
		
		TArray<int32> NodeStack;
		const int32 FirstNode = bDynamicTree ? RootNode : 0;
		
		for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
		{ 
			OverlappingOffsets[LeafIndex] = OverlappingLeaves.Num();
			FindOverlappingLeaf(FirstNode, LeafIndex, NodeStack);
		}
		OverlappingOffsets.Last() = OverlappingLeaves.Num();
	}

	/** Cache for each leaves all the overlapping leaves*/
	virtual void CacheOverlappingLeaves() override
	{
		// Dev settings to switch easily algorithms
		// Will switch to cvars if the leaf version could be faster 
		const bool bCachingRoot = true;
		const bool bDirtyFilter = false;
		
		if(bCachingRoot)
		{
			ComputeOverlappingCacheFromRoot(bDirtyFilter);
		}
		else
		{
			ComputeOverlappingCacheFromLeaf();
		}
	}

	/** Print the overlapping leaves data structure */
	void PrintOverlappingLeaves()
	{
		UE_LOG(LogChaos, Log, TEXT("Num Leaves = %d"), Leaves.Num());
		for(int32 LeafIndex = 0, NumLeaves = Leaves.Num(); LeafIndex < NumLeaves; ++LeafIndex)
		{
			auto& Leaf = Leaves[LeafIndex];
			UE_LOG(LogChaos, Log, TEXT("Overlapping Count[%d] = %d with bounds = %f %f %f | %f %f %f"), LeafIndex,
				OverlappingOffsets[LeafIndex+1] - OverlappingOffsets[LeafIndex], Leaf.GetBounds().Min()[0],
				Leaf.GetBounds().Min()[1], Leaf.GetBounds().Min()[2], Leaf.GetBounds().Max()[0], Leaf.GetBounds().Max()[1], Leaf.GetBounds().Max()[2]);
			
			for(int32 OverlappingIndex = OverlappingOffsets[LeafIndex]; OverlappingIndex < OverlappingOffsets[LeafIndex+1]; ++OverlappingIndex)
			{
				UE_LOG(LogChaos, Log, TEXT("Overlapping Leaf[%d] = %d"), LeafIndex, OverlappingLeaves[OverlappingIndex]);
			}
		}
	}

private:

	using FElement = TPayloadBoundsElement<TPayloadType, T>;
	using FNode = TAABBTreeNode<T>;

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

		TAABBTree<TPayloadType,TLeafType,bMutable, T> NewTree(AllElements);
		*this = NewTree;
		bShouldRebuild = true; // No changes since last time tree was built
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

	/** Cached version of the overlap function 
	 * @param QueryBounds Bounds we want to query the tree on
	 * @param Visitor Object owning the payload and used in the overlap function to store the result
	 * @param bOverlapResult Result of the overlap function that will be used in the overlap fast
	 * @return Boolean to check if we have used or not the cached overlapping leaves (for static we don't have the cache yet)
	 */
	template <EAABBQueryType Query, typename SQVisitor>
	bool OverlapCached(const FAABB3& QueryBounds, SQVisitor& Visitor, bool& bOverlapResult) const
	{
		bool bCouldUseCache = false;
		bOverlapResult = true;
		// Only the overlap queries could use the caching
		if (Query == EAABBQueryType::Overlap && Visitor.GetQueryPayload())
		{
			// Grab the payload from the visitor (only available on physics thread for now) and retrieve the info
			const TPayloadType& QueryPayload = *static_cast<const TPayloadType*>(Visitor.GetQueryPayload());
			if( const FAABBTreePayloadInfo* QueryInfo =  PayloadToInfo.Find(QueryPayload))
			{
				const int32 LeafIndex = QueryInfo->LeafIdx;
				if(LeafIndex != INDEX_NONE && LeafIndex < (OverlappingOffsets.Num()-1))
				{
					//Once we have the leaf index we can loop over the overlapping leaves
					for(int32 OverlappingIndex = OverlappingOffsets[LeafIndex];
								OverlappingIndex < OverlappingOffsets[LeafIndex+1]; ++OverlappingIndex)
					{
						const TLeafType& OverlappingLeaf = Leaves[OverlappingLeaves[OverlappingIndex]];
						if (OverlappingLeaf.OverlapFast(QueryBounds, Visitor) == false)
						{
							bOverlapResult = false;
							break;
						}
					}
					bCouldUseCache = true;
				}
			}
		}
		return bCouldUseCache;
	}

	template <EAABBQueryType Query, typename TQueryFastData, typename SQVisitor>
	bool QueryImp(const FVec3& RESTRICT Start, TQueryFastData& CurData, const FVec3& QueryHalfExtents, const FAABB3& QueryBounds, SQVisitor& Visitor, const FVec3& Dir, const FVec3& InvDir, const bool bParallel[3]) const
	{
		PHYSICS_CSV_CUSTOM_VERY_EXPENSIVE(PhysicsCounters, MaxDirtyElements, DirtyElements.Num(), ECsvCustomStatOp::Max);
		PHYSICS_CSV_CUSTOM_VERY_EXPENSIVE(PhysicsCounters, MaxNumLeaves, Leaves.Num(), ECsvCustomStatOp::Max);
		PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, QueryImp);
		//QUICK_SCOPE_CYCLE_COUNTER(AABBTreeQueryImp);
#if !WITH_EDITOR
		//CSV_SCOPED_TIMING_STAT(ChaosPhysicsTimers, AABBTreeQuery)
#endif
		FVec3 TmpPosition;
		FReal TOI = 0;
		{
			//QUICK_SCOPE_CYCLE_COUNTER(QueryGlobal);
			PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, QueryImp_Global);
			for(const auto& Elem : GlobalPayloads)
			{
				if (PrePreFilterHelper(Elem.Payload, Visitor))
				{
					continue;
				}

				const FAABB3 InstanceBounds(Elem.Bounds.Min(), Elem.Bounds.Max());
				if(TAABBTreeIntersectionHelper<TQueryFastData,Query>::Intersects(Start, CurData, TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents, Dir, InvDir, bParallel))
				{
					TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload,true, InstanceBounds);
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
				const FAABB3 InstanceBounds(Elem.Bounds.Min(), Elem.Bounds.Max());
				if (PrePreFilterHelper(Elem.Payload, Visitor))
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
			if (DirtyElements.Num() > 0)
			{
				bool bUseGrid = false;

				if (DirtyElementGridEnabled() && CellHashToFlatArray.Num() > 0)
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
					PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, QueryImp_DirtyElementsGrid);
					TArray<DirtyGridHashEntry> HashEntryForOverlappedCells;

					auto AddHashEntry = [&](int32 QueryCellHash)
					{
						const DirtyGridHashEntry* HashEntry = CellHashToFlatArray.Find(QueryCellHash);
						if (HashEntry)
						{
							HashEntryForOverlappedCells.Add(*HashEntry);
						}
						return true;
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
						DoForSweepIntersectCells(QueryHalfExtents, Start, CurData.Dir, CurData.CurrentLength, DirtyElementGridCellSize, DirtyElementGridCellSizeInv,
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

				else
				{
					PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, QueryImp_DirtyElements);
					for (const auto& Elem : DirtyElements)
					{
						if (!IntersectAndVisit(Elem))
						{
							return false;
						}
					}
				}
			}
		}

		struct FNodeQueueEntry
		{
			int32 NodeIdx;
			FReal TOI;
		};

		// Caching is for now only available for dyanmic tree
		if (bDynamicTree && !OverlappingLeaves.IsEmpty())
		{
			// For overlap query and dynamic tree we are using the cached overlapping leaves
			bool bOverlapResult = true;
			if (OverlapCached<Query, SQVisitor>(QueryBounds, Visitor, bOverlapResult))
			{
				return bOverlapResult;
			}
		}
		
		TArray<FNodeQueueEntry> NodeStack;
		if (bDynamicTree)
		{

			if (RootNode != INDEX_NONE)
			{
				NodeStack.Add(FNodeQueueEntry{ RootNode, 0 });
			}
		}
		else if (Nodes.Num())
		{
			NodeStack.Add(FNodeQueueEntry{ 0, 0 });
		}

// Slow debug code
//#if !WITH_EDITOR
//		if (Query == EAABBQueryType::Overlap)
//		{
//			CSV_CUSTOM_STAT(ChaosPhysicsTimers, OverlapCount, 1, ECsvCustomStatOp::Accumulate);
//			CSV_CUSTOM_STAT(ChaosPhysicsTimers, DirtyCount, DirtyElements.Num(), ECsvCustomStatOp::Max);
//		}
//#endif

		while (NodeStack.Num())
		{
			PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, QueryImp_NodeTraverse);

//#if !WITH_EDITOR
//			if (Query == EAABBQueryType::Overlap)
//			{
//				CSV_CUSTOM_STAT(ChaosPhysicsTimers, AABBCheckCount, 1, ECsvCustomStatOp::Accumulate);
//			}
//#endif

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
				PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, NodeTraverse_Leaf);
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
				PHYSICS_CSV_SCOPED_VERY_EXPENSIVE(PhysicsVerbose, NodeTraverse_Branch);
				int32 Idx = 0;
				for (const TAABB<T, 3>& AABB : Node.ChildrenBounds)
				{
					if(TAABBTreeIntersectionHelper<TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, FAABB3(AABB.Min(), AABB.Max()), QueryBounds, QueryHalfExtents, Dir, InvDir, bParallel))
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
		RootNode = INDEX_NONE;
		FirstFreeInternalNode = INDEX_NONE;
		FirstFreeLeafNode = INDEX_NONE;
		DirtyElements.Reset();
		CellHashToFlatArray.Reset(); 
		FlattenedCellArrayOfDirtyIndices.Reset();
		DirtyElementsGridOverflow.Reset();
		TreeStats.Reset();
		TreeExpensiveStats.Reset();
		PayloadToInfo.Reset();
		NumProcessedThisSlice = 0;
		GetCVars();  // Safe to copy CVARS here

		if (bDynamicTree)
		{
			// Todo for now, don't ever return async task
			int32 Idx = 0;
			for (auto& Particle : Particles)
			{
				bool bHasBoundingBox = HasBoundingBox(Particle);
				auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
				TAABB<T, 3> ElemBounds = ComputeWorldSpaceBoundingBox(Particle, false, (T)0);
				
				UpdateElement(Payload, ElemBounds, bHasBoundingBox);
				++Idx;
			}
			this->SetAsyncTimeSlicingComplete(true);
			return;
		}

		WorkSnapshot.Bounds = TAABB<T, 3>::EmptyAABB();

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeTimeSliceSetup);

			// Prepare to find the average and scaled variance of particle centers.
			WorkSnapshot.AverageCenter = FVec3(0);
			WorkSnapshot.ScaledCenterVariance = FVec3(0);

			int32 Idx = 0;

			//TODO: we need a better way to time-slice this case since there can be a huge number of Particles. Can't do it right now without making full copy
			TVec3<T> CenterSum(0);

			for (auto& Particle : Particles)
			{
				bool bHasBoundingBox = HasBoundingBox(Particle);
				auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
				TAABB<T, 3> ElemBounds = ComputeWorldSpaceBoundingBox(Particle, false, (T)0);

				// If bounds are bad, use global so we won't screw up splitting computations.
				if (bHasBoundingBox && ValidateBounds(ElemBounds) == false)
				{
					bHasBoundingBox = false;
					ensureMsgf(false, TEXT("AABBTree encountered invalid bounds input. Forcing element to global payload. Min: %s Max: %s. If Bounds are valid but large, increase FAABBTreeCVars::MaxNonGlobalElementBoundsExtrema."),
						*ElemBounds.Min().ToString(), *ElemBounds.Max().ToString());
				}


				if (bHasBoundingBox)
				{
					if (ElemBounds.Extents().Max() > MaxPayloadBounds)
					{
						bHasBoundingBox = false;
					}
					else
					{
						FReal NumElems = (FReal)(WorkSnapshot.Elems.Add(FElement{ Payload, ElemBounds }) + 1);
						WorkSnapshot.Bounds.GrowToInclude(ElemBounds);

						// Include the current particle in the average and scaled variance of the particle centers using Welford's method.
						TVec3<T> CenterDelta = ElemBounds.Center() - WorkSnapshot.AverageCenter;
						WorkSnapshot.AverageCenter += CenterDelta / (T)NumElems;
						WorkSnapshot.ScaledCenterVariance += (ElemBounds.Center() - WorkSnapshot.AverageCenter) * CenterDelta;
					}
				}
				else
				{
					ElemBounds = TAABB<T, 3>(TVec3<T>(TNumericLimits<T>::Lowest()), TVec3<T>(TNumericLimits<T>::Max()));
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
		TAABB<T, 3> RealBounds;	//Actual bounds as children are added
		int32 WorkSnapshotIdx;	//Idx into work snapshot pool
	};

	struct FWorkSnapshot
	{
		FWorkSnapshot()
			: TimeslicePhase(eTimeSlicePhase::PreFindBestBounds)
		{

		}

		eTimeSlicePhase TimeslicePhase;

		TAABB<T, 3> Bounds;
		TArray<FElement> Elems;

		// The average of the element centers and their variance times the number of elements.
		TVec3<T> AverageCenter;
		TVec3<T> ScaledCenterVariance;

		int32 NodeLevel;
		int32 NewNodeIdx;

		int32 BestBoundsCurIdx;

		FSplitInfo SplitInfos[2];
	};

	void FindBestBounds(const int32 StartElemIdx, const int32 LastElem, FWorkSnapshot& CurrentSnapshot, int32 MaxAxis, const TVec3<T>& SplitCenter)
	{
		const T SplitVal = SplitCenter[MaxAxis];

		// add all elements to one of the two split infos at this level - root level [ not taking into account the max number allowed or anything
		for(int32 ElemIdx = StartElemIdx; ElemIdx < LastElem; ++ElemIdx)
		{
			const FElement& Elem = CurrentSnapshot.Elems[ElemIdx];
			int32 BoxIdx = 0;
			const TVec3<T> ElemCenter = Elem.Bounds.Center();
			
			// This was changed to work around a code generation issue on some platforms, don't change it without testing results of ElemCenter computation.
			// 
			// NOTE: This needs review.  TVec3<T>::operator[] should now cope with the strict aliasing violation which caused the original codegen breakage.
			T CenterVal = ElemCenter[0];
			if (MaxAxis == 1)
			{
				CenterVal = ElemCenter[1];
			}
			else if(MaxAxis == 2)
			{
				CenterVal = ElemCenter[2];
			}
		
			const int32 MinBoxIdx = CenterVal <= SplitVal ? 0 : 1;
			
			FSplitInfo& SplitInfo = CurrentSnapshot.SplitInfos[MinBoxIdx];
			FWorkSnapshot& WorkSnapshot = WorkPool[SplitInfo.WorkSnapshotIdx];
			T NumElems = (T)(WorkSnapshot.Elems.Add(Elem) + 1);
			SplitInfo.RealBounds.GrowToInclude(Elem.Bounds);

			// Include the current particle in the average and scaled variance of the particle centers using Welford's method.
			TVec3<T> CenterDelta = ElemCenter - WorkSnapshot.AverageCenter;
			WorkSnapshot.AverageCenter += CenterDelta / NumElems;
			WorkSnapshot.ScaledCenterVariance += (ElemCenter - WorkSnapshot.AverageCenter) * CenterDelta;
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
			auto& TreeExpensiveStatsRef = TreeExpensiveStats;
			auto MakeLeaf = [NewNodeIdx, &PayloadToInfoRef, &WorkPoolRef, CurIdx, &LeavesRef, &NodesRef, &TreeExpensiveStatsRef]()
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
				//Add two children, remember this invalidates any pointers to current snapshot
				const int32 FirstChildIdx = GetNewWorkSnapshot();
				const int32 SecondChildIdx = GetNewWorkSnapshot();

				//mark idx of children into the work pool
				WorkPool[CurIdx].SplitInfos[0].WorkSnapshotIdx = FirstChildIdx;
				WorkPool[CurIdx].SplitInfos[1].WorkSnapshotIdx = SecondChildIdx;

				for (FSplitInfo& SplitInfo : WorkPool[CurIdx].SplitInfos)
				{
					SplitInfo.RealBounds = TAABB<T, 3>::EmptyAABB();
				}

				WorkPool[CurIdx].BestBoundsCurIdx = 0;
				WorkPool[CurIdx].TimeslicePhase = eTimeSlicePhase::DuringFindBestBounds;
				const int32 ExpectedNumPerChild = WorkPool[CurIdx].Elems.Num() / 2;
				{
					WorkPool[FirstChildIdx].Elems.Reserve(ExpectedNumPerChild);
					WorkPool[SecondChildIdx].Elems.Reserve(ExpectedNumPerChild);

					// Initialize the the two info's element center average and scaled variance.
					WorkPool[FirstChildIdx].AverageCenter = TVec3<T>(0);
					WorkPool[FirstChildIdx].ScaledCenterVariance = TVec3<T>(0);
					WorkPool[SecondChildIdx].AverageCenter = TVec3<T>(0);
					WorkPool[SecondChildIdx].ScaledCenterVariance = TVec3<T>(0);
				}
			}

			if (WorkPool[CurIdx].TimeslicePhase == eTimeSlicePhase::DuringFindBestBounds)
			{
				const int32 NumWeCanProcess = MaxNumToProcess - NumProcessedThisSlice;
				const int32 LastIdxToProcess = WeAreTimeslicing ? FMath::Min(WorkPool[CurIdx].BestBoundsCurIdx + NumWeCanProcess, WorkPool[CurIdx].Elems.Num()) : WorkPool[CurIdx].Elems.Num();

				// Determine the axis to split the AABB on based on the SplitOnVarianceAxis console variable. If it is not 1, simply use the largest axis
				// of the work snapshot bounds; otherwise, select the axis with the greatest center variance. Note that the variance times the number of
				// elements is actually used but since all that is needed is the axis with the greatest variance the scale factor is irrelevant.
				const int32 MaxAxis = (FAABBTreeCVars::SplitOnVarianceAxis != 1) ? WorkPool[CurIdx].Bounds.LargestAxis() :
					(WorkPool[CurIdx].ScaledCenterVariance[0] > WorkPool[CurIdx].ScaledCenterVariance[1] ?
						(WorkPool[CurIdx].ScaledCenterVariance[0] > WorkPool[CurIdx].ScaledCenterVariance[2] ? 0 : 2) :
						(WorkPool[CurIdx].ScaledCenterVariance[1] > WorkPool[CurIdx].ScaledCenterVariance[2] ? 1 : 2));

				// Find the point where the AABB will be split based on the SplitAtAverageCenter console variable. If it is not 1, just use the center
				// of the AABB; otherwise, use the average of the element centers.
				const TVec3<T>& Center = (FAABBTreeCVars::SplitAtAverageCenter != 1) ? WorkPool[CurIdx].Bounds.Center() : WorkPool[CurIdx].AverageCenter;

				FindBestBounds(WorkPool[CurIdx].BestBoundsCurIdx, LastIdxToProcess, WorkPool[CurIdx], MaxAxis, Center);
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
			const void* GetSimData() const { return nullptr; }

			/** Return a pointer to the payload on which we are querying the acceleration structure */
			const void* GetQueryPayload() const 
			{ 
				return nullptr; 
			}

			bool ShouldIgnore(const TSpatialVisitorData<TPayloadType>& Instance) const { return false; }
			TArray<TPayloadType>& CollectedResults;
		};

		TArray<TPayloadType> Results;
		FSimpleVisitor Collector(Results);
		Overlap(Intersection, Collector);

		return Results;
	}

	// Set InOutMaxSize to less than 0 for unlimited
	// 
	template<typename ContainerType>
	static void AddToContainerHelper(const ContainerType& ContainerFrom, ContainerType& ContainerTo, int32 Index)
	{
		ContainerTo.Add(ContainerFrom[Index]);
	}

	template<>
	static void AddToContainerHelper(const TArrayAsMap<TPayloadType, FAABBTreePayloadInfo>& ContainerFrom, TArrayAsMap<TPayloadType, FAABBTreePayloadInfo>& ContainerTo, int32 Index)
	{
		ContainerTo.AddFrom(ContainerFrom, Index);
	}

	template<typename ContainerType>
	static int32 ContainerElementSizeHelper(const ContainerType& Container, int32 Index)
	{
		return sizeof(typename ContainerType::ElementType);
	}

	template<>
	static int32 ContainerElementSizeHelper(const TArray<TAABBTreeLeafArray<TPayloadType, true>>& Container, int32 Index)
	{
		return sizeof(typename TArray<TAABBTreeLeafArray<TPayloadType, true>>::ElementType) + sizeof(typename decltype(Container[Index].Elems)::ElementType) * Container[Index].GetElementCount();
	}

	template<>
	static int32 ContainerElementSizeHelper(const TArray<TAABBTreeLeafArray<TPayloadType, false>>& Container, int32 Index)
	{
		return sizeof(typename TArray<TAABBTreeLeafArray<TPayloadType, false>>::ElementType) + sizeof(typename decltype(Container[Index].Elems)::ElementType) * Container[Index].GetElementCount();
	}

	template<typename ContainerType>
	static bool ContinueTimeSliceCopy(const ContainerType& ContainerFrom, ContainerType& ContainerTo, int32& InOutMaxSize)
	{
		int32 SizeCopied = 0;

		for (int32 Index = ContainerTo.Num(); Index < ContainerFrom.Num() && (InOutMaxSize < 0 || SizeCopied < InOutMaxSize); Index++)
		{
			AddToContainerHelper(ContainerFrom, ContainerTo, Index);
			SizeCopied += ContainerElementSizeHelper(ContainerFrom, Index);
		}

		// Update the maximum size left
		if (InOutMaxSize > 0)
		{
			if (SizeCopied > InOutMaxSize)
			{
				InOutMaxSize = 0;
			}
			else
			{
				InOutMaxSize -= SizeCopied;
			}
		}

		bool Done = ContainerTo.Num() == ContainerFrom.Num();
		return Done;
	}


#if !UE_BUILD_SHIPPING
	virtual void DebugDraw(ISpacialDebugDrawInterface<T>* InInterface) const override
	{
		if (InInterface)
		{
			if (Nodes.Num() > 0)
			{
				Nodes[0].DebugDraw(*InInterface, Nodes, { 1.f, 1.f, 1.f }, 5.f);
			}
			for (const TLeafType& Leaf : Leaves)
			{
				Leaf.DebugDrawLeaf(*InInterface, FLinearColor::MakeRandomColor(), 10.f);
			}
		}
	}
#endif


	TAABBTree(const TAABBTree<TPayloadType, TLeafType, bMutable, T>& Other)
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, Nodes(Other.Nodes)
		, Leaves(Other.Leaves)
		, DirtyElements(Other.DirtyElements)
		, bDynamicTree(Other.bDynamicTree)
		, RootNode(Other.RootNode)
		, FirstFreeInternalNode(Other.FirstFreeInternalNode)
		, FirstFreeLeafNode(Other.FirstFreeLeafNode)
		, CellHashToFlatArray(Other.CellHashToFlatArray)
		, FlattenedCellArrayOfDirtyIndices(Other.FlattenedCellArrayOfDirtyIndices)
		, DirtyElementsGridOverflow(Other.DirtyElementsGridOverflow)
		, DirtyElementGridCellSize(Other.DirtyElementGridCellSize)
		, DirtyElementGridCellSizeInv(Other.DirtyElementGridCellSizeInv)
		, DirtyElementMaxGridCellQueryCount(Other.DirtyElementMaxGridCellQueryCount)
		, DirtyElementMaxPhysicalSizeInCells(Other.DirtyElementMaxPhysicalSizeInCells)
		, DirtyElementMaxCellCapacity(Other.DirtyElementMaxCellCapacity)
		, TreeStats(Other.TreeStats)
		, TreeExpensiveStats(Other.TreeExpensiveStats)
		, GlobalPayloads(Other.GlobalPayloads)
		, PayloadToInfo(Other.PayloadToInfo)
		, MaxChildrenInLeaf(Other.MaxChildrenInLeaf)
		, MaxTreeDepth(Other.MaxTreeDepth)
		, MaxPayloadBounds(Other.MaxPayloadBounds)
		, MaxNumToProcess(Other.MaxNumToProcess)
		, NumProcessedThisSlice(Other.NumProcessedThisSlice)
		, bShouldRebuild(Other.bShouldRebuild)
		, OverlappingLeaves(Other.OverlappingLeaves)
		, OverlappingOffsets(Other.OverlappingOffsets)
		, OverlappingPairs(Other.OverlappingPairs)
		, OverlappingCounts(Other.OverlappingCounts)
	{

		ensure(bDynamicTree == Other.IsTreeDynamic());

	}

	virtual ISpatialAcceleration<TPayloadType, T, 3>& operator=(const ISpatialAcceleration<TPayloadType, T, 3>& Other) override
	{
		
		check(Other.GetType() == ESpatialAcceleration::AABBTree);
		return operator=(static_cast<const TAABBTree<TPayloadType, TLeafType, bMutable, T>&>(Other));
	}

	TAABBTree<TPayloadType,TLeafType, bMutable, T>& operator=(const TAABBTree<TPayloadType,TLeafType,bMutable, T>& Rhs)
	{
		ISpatialAcceleration<TPayloadType, T, 3>::operator=(Rhs);
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
			bDynamicTree = Rhs.bDynamicTree;
			RootNode = Rhs.RootNode;
			FirstFreeInternalNode = Rhs.FirstFreeInternalNode;
			FirstFreeLeafNode = Rhs.FirstFreeLeafNode;
			
			CellHashToFlatArray = Rhs.CellHashToFlatArray;
			FlattenedCellArrayOfDirtyIndices = Rhs.FlattenedCellArrayOfDirtyIndices;
			DirtyElementsGridOverflow = Rhs.DirtyElementsGridOverflow;
			TreeStats = Rhs.TreeStats;
			TreeExpensiveStats = Rhs.TreeExpensiveStats;
			
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
			bShouldRebuild = Rhs.bShouldRebuild;
		}

		return *this;
	}

	TArray<FNode> Nodes;
	TArray<TLeafType> Leaves;
	TArray<FElement> DirtyElements;

	// DynamicTree members
	bool bDynamicTree = false;
	int32 RootNode = INDEX_NONE;
	// Free lists (indices are in Nodes array)
	int32 FirstFreeInternalNode = INDEX_NONE;
	int32 FirstFreeLeafNode = INDEX_NONE;

	// Data needed for DirtyElement2DAccelerationGrid
	TMap<int32, DirtyGridHashEntry> CellHashToFlatArray; // Index, size into flat grid structure (FlattenedCellArrayOfDirtyIndices)
	TArray<int32> FlattenedCellArrayOfDirtyIndices; // Array of indices into dirty Elements array, indices are always sorted within a 2D cell
	TArray<int32> DirtyElementsGridOverflow; // Array of indices of DirtyElements that is not in the grid for some reason
	// Copy of CVARS
	T DirtyElementGridCellSize;
	T DirtyElementGridCellSizeInv;
	int32 DirtyElementMaxGridCellQueryCount;
	int32 DirtyElementMaxPhysicalSizeInCells;
	int32 DirtyElementMaxCellCapacity;
	// Some useful statistics
	AABBTreeStatistics TreeStats;
	AABBTreeExpensiveStatistics TreeExpensiveStats;

	TArray<FElement> GlobalPayloads;
	TArrayAsMap<TPayloadType, FAABBTreePayloadInfo> PayloadToInfo;

	int32 MaxChildrenInLeaf;
	int32 MaxTreeDepth;
	T MaxPayloadBounds;
	int32 MaxNumToProcess;

	int32 NumProcessedThisSlice;
	TArray<int32> WorkStack;
	TArray<int32> WorkPoolFreeList;
	TArray<FWorkSnapshot> WorkPool;

	bool bShouldRebuild;  // Contract: this can only ever be cleared by calling the ClearShouldRebuild method

	/** Flat array of overlapping leaves.  */
	TArray<int32> OverlappingLeaves;

	/** Offset for each leaf in the overlapping leaves (OverlappingOffsets[LeafIndex]/OverlappingOffsets[LeafIndex+1] will be start/end indices of the leaf index in the overallping leaves array )*/
	TArray<int32> OverlappingOffsets;

	/** List of leaves intersecting pairs that will be used to build the flat arrays (offsets,leaves) */
	TArray<FIntVector2> OverlappingPairs;

	/** Number of overlapping leaves per leaf */
	TArray<int32> OverlappingCounts;
};

template<typename TPayloadType, typename TLeafType, bool bMutable, typename T>
FArchive& operator<<(FChaosArchive& Ar, TAABBTree<TPayloadType, TLeafType, bMutable, T>& AABBTree)
{
	AABBTree.Serialize(Ar);
	return Ar;
}


}
