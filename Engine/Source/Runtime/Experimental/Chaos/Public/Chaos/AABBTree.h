// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/AABB.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Templates/Models.h"
#include "Chaos/BoundingVolume.h"

struct FAABBTreeCVars
{
	static int32 UpdateDirtyElementPayloadData;
	static FAutoConsoleVariableRef CVarUpdateDirtyElementPayloadData;
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

template <typename T, typename TQueryFastData, EAABBQueryType Query>
struct TAABBTreeIntersectionHelper
{
	static bool Intersects(const TVec3<T>& Start, TQueryFastData& QueryFastData, T& TOI, TVec3<T>& OutPosition,
		const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVec3<T>& QueryHalfExtents)
	{
		check(false);
		return true;
	}
};

template<>
struct TAABBTreeIntersectionHelper<FReal, FQueryFastData, EAABBQueryType::Raycast>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const FVec3& Start, FQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const TAABB<FReal, 3>& Bounds, const TAABB<FReal, 3>& QueryBounds, const FVec3& QueryHalfExtents)
	{
		return Bounds.RaycastFast(Start, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, FQueryFastData, EAABBQueryType::Sweep>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVec3<T>& Start, FQueryFastData& QueryFastData,
		T& TOI, TVec3<T>& OutPosition, const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVec3<T>& QueryHalfExtents)
	{
		TAABB<T, 3> SweepBounds(Bounds.Min() - QueryHalfExtents, Bounds.Max() + QueryHalfExtents);
		return SweepBounds.RaycastFast(Start, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, FQueryFastDataVoid, EAABBQueryType::Overlap>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVec3<T>& Start, FQueryFastDataVoid& QueryFastData, T& TOI, TVec3<T>& OutPosition,
		const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVec3<T>& QueryHalfExtents)
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

template <typename TPayloadType, typename T, bool bComputeBounds = true>
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

	template <typename TSQVisitor, typename TQueryFastData>
	bool RaycastFast(const TVec3<T>& Start, TQueryFastData& QueryFastData, TSQVisitor& Visitor) const
	{
		return RaycastSweepImp</*bSweep=*/false>(Start, QueryFastData, TVec3<T>(), Visitor);
	}

	template <typename TSQVisitor, typename TQueryFastData>
	bool SweepFast(const TVec3<T>& Start, TQueryFastData& QueryFastData, const TVec3<T>& QueryHalfExtents, TSQVisitor& Visitor) const
	{
		return RaycastSweepImp</*bSweep=*/true>(Start, QueryFastData, QueryHalfExtents, Visitor);
	}

	template <typename TSQVisitor>
	bool OverlapFast(const TAABB<T, 3>& QueryBounds, TSQVisitor& Visitor) const
	{
		for (const auto& Elem : Elems)
		{
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
	bool RaycastSweepImp(const TVec3<T>& Start, TQueryFastData& QueryFastData, const TVec3<T>& QueryHalfExtents, TSQVisitor& Visitor) const
	{
		TVec3<T> TmpPosition;
		T TOI;
		for (const auto& Elem : Elems)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TAABBTreeIntersectionHelper<T, TQueryFastData, bSweep ? EAABBQueryType::Sweep : EAABBQueryType::Raycast>::Intersects(Start, QueryFastData, TOI, TmpPosition, InstanceBounds, TAABB<T,3>(), QueryHalfExtents))
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

	void UpdateElement(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds, bool bHasBounds)
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

	const TAABB<T, 3>& GetBounds() const
	{
		return Bounds;
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Elems;
	}

	TArray<TPayloadBoundsElement<TPayloadType, T>> Elems;
	TAABB<T,3> Bounds;
};

template <typename TPayloadType, typename T, bool bComputeBounds>
FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeLeafArray<TPayloadType, T, bComputeBounds>& LeafArray)
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
	int32 ChildrenNodes[2] = { 0, 0 };
	bool bLeaf = false;

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
	}
};

template <typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeNode<T>& Node)
{
	Node.Serialize(Ar);
	return Ar;
}

struct FAABBTreePayloadInfo
{
	int32 GlobalPayloadIdx;
	int32 DirtyPayloadIdx;
	int32 LeafIdx;

	FAABBTreePayloadInfo(int32 InGlobalPayloadIdx = INDEX_NONE, int32 InDirtyIdx = INDEX_NONE, int32 InLeafIdx = INDEX_NONE)
		: GlobalPayloadIdx(InGlobalPayloadIdx)
		, DirtyPayloadIdx(InDirtyIdx)
		, LeafIdx(InLeafIdx)
	{}

	void Serialize(FArchive& Ar)
	{
		Ar << GlobalPayloadIdx;
		Ar << DirtyPayloadIdx;
		Ar << LeafIdx;
	}
};

inline FArchive& operator<<(FArchive& Ar, FAABBTreePayloadInfo& PayloadInfo)
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

template <typename TPayloadType, typename TLeafType, typename T, bool bMutable = true>
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
	static constexpr ESpatialAcceleration StaticType = TIsSame<TAABBTreeLeafArray<TPayloadType, T>, TLeafType>::Value ? ESpatialAcceleration::AABBTree : 
		(TIsSame<TBoundingVolume<TPayloadType, T, 3>, TLeafType>::Value ? ESpatialAcceleration::AABBTreeBV : ESpatialAcceleration::Unknown);
	TAABBTree()
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, MaxChildrenInLeaf(DefaultMaxChildrenInLeaf)
		, MaxTreeDepth(DefaultMaxTreeDepth)
		, MaxPayloadBounds(DefaultMaxPayloadBounds)
		, MaxNumToProcess(DefaultMaxNumToProcess)
	{
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
	TAABBTree(const TParticles& Particles, int32 InMaxChildrenInLeaf = DefaultMaxChildrenInLeaf, int32 InMaxTreeDepth = DefaultMaxTreeDepth, T InMaxPayloadBounds = DefaultMaxPayloadBounds, int32 InMaxNumToProcess = DefaultMaxNumToProcess )
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
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

	virtual TArray<TPayloadType> FindAllIntersections(const TAABB<T, 3>& Box) const override { return FindAllIntersectionsImp(Box); }

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

	void CopyFrom(const TAABBTree<TPayloadType, TLeafType, T, bMutable>& Other)
	{
		(*this) = Other;
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>>(new TAABBTree<TPayloadType, TLeafType, T, bMutable>(*this));
	}

	virtual void Raycast(const TVec3<T>& Start, const TVec3<T>& Dir, const T Length, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Raycast(Start, Dir, Length, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Raycast(const TVec3<T>& Start, const TVec3<T>& Dir, const T Length, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Raycast>(Start, QueryFastData, TVec3<T>(), TAABB<T,3>(), Visitor);
	}

	template <typename SQVisitor>
	bool RaycastFast(const TVec3<T>& Start, FQueryFastData& CurData, SQVisitor& Visitor) const
	{
		return QueryImp<EAABBQueryType::Raycast>(Start, CurData, TVec3<T>(), TAABB<T, 3>(), Visitor);
	}

	void Sweep(const TVec3<T>& Start, const TVec3<T>& Dir, const T Length, const TVec3<T> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Sweep(Start, Dir, Length, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Sweep(const TVec3<T>& Start, const TVec3<T>& Dir, const T Length, const TVec3<T> QueryHalfExtents, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Sweep>(Start, QueryFastData, QueryHalfExtents, TAABB<T, 3>(), Visitor);
	}

	template <typename SQVisitor>
	bool SweepFast(const TVec3<T>& Start, FQueryFastData& CurData, const TVec3<T> QueryHalfExtents, SQVisitor& Visitor) const
	{
		return QueryImp<EAABBQueryType::Sweep>(Start,CurData, QueryHalfExtents, TAABB<T, 3>(), Visitor);
	}

	void Overlap(const TAABB<T, 3>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Overlap(QueryBounds, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Overlap(const TAABB<T,3>& QueryBounds, SQVisitor& Visitor) const
	{
		OverlapFast(QueryBounds, Visitor);
	}

	template <typename SQVisitor>
	bool OverlapFast(const TAABB<T, 3>& QueryBounds, SQVisitor& Visitor) const
	{
		//dummy variables to reuse templated path
		FQueryFastDataVoid VoidData;
		return QueryImp<EAABBQueryType::Overlap>(TVec3<T>(), VoidData, TVec3<T>(), QueryBounds, Visitor);
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
					if (PayloadInfo->DirtyPayloadIdx + 1 < DirtyElements.Num())
					{
						auto LastDirtyPayload = DirtyElements.Last().Payload;
						PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = PayloadInfo->DirtyPayloadIdx;
					}
					DirtyElements.RemoveAtSwap(PayloadInfo->DirtyPayloadIdx);
				}
				else if (ensure(PayloadInfo->LeafIdx != INDEX_NONE))
				{
					Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
				}

				PayloadToInfo.Remove(Payload);
			}
		}
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TAABB<T, 3>& NewBounds, bool bHasBounds) override
	{
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
						const TAABB<T,3>& LeafBounds = Leaves[PayloadInfo->LeafIdx].GetBounds();
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
				}
				else
				{
					DirtyElements[PayloadInfo->DirtyPayloadIdx].Bounds = NewBounds;
					UpdateElementHelper(DirtyElements[PayloadInfo->DirtyPayloadIdx].Payload, Payload);
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
					if (PayloadInfo->DirtyPayloadIdx + 1 < DirtyElements.Num())
					{
						auto LastDirtyPayload = DirtyElements.Last().Payload;
						PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = PayloadInfo->DirtyPayloadIdx;
					}
					DirtyElements.RemoveAtSwap(PayloadInfo->DirtyPayloadIdx);

					PayloadInfo->DirtyPayloadIdx = INDEX_NONE;
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

	const TArray<TPayloadBoundsElement<TPayloadType, T>>& GlobalObjects() const
	{
		return GlobalPayloads;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::RemovedAABBTreeFullBounds)
		{
			// Serialize out unused aabb for earlier versions
			TAABB<T, 3> Dummy(TVec3<T>((T)0), TVec3<T>((T)0));
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
	}

private:

	using FElement = TPayloadBoundsElement<TPayloadType, T>;
	using FNode = TAABBTreeNode<T>;

	void ReoptimizeTree()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TAABBTree::ReoptimizeTree);
		TArray<FElement> AllElements;

		SIZE_T ReserveCount = DirtyElements.Num() + GlobalPayloads.Num();
		for (const auto& Leaf : Leaves)
		{
			ReserveCount += Leaf.GetReserveCount();
		}

		AllElements.Reserve(ReserveCount);

		AllElements.Append(DirtyElements);
		AllElements.Append(GlobalPayloads);

		for (auto& Leaf : Leaves)
		{
			Leaf.GatherElements(AllElements);
		}

		TAABBTree<TPayloadType,TLeafType,T,bMutable> NewTree(AllElements);
		*this = NewTree;
	}

	template <EAABBQueryType Query, typename TQueryFastData, typename SQVisitor>
	bool QueryImp(const TVec3<T>& Start, TQueryFastData& CurData, const TVec3<T> QueryHalfExtents, const TAABB<T,3>& QueryBounds, SQVisitor& Visitor) const
	{
		//QUICK_SCOPE_CYCLE_COUNTER(AABBTreeQueryImp);
		TVec3<T> TmpPosition;
		T TOI = 0;
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
				if(TAABBTreeIntersectionHelper<T,TQueryFastData,Query>::Intersects(Start,CurData,TOI,TmpPosition,InstanceBounds,QueryBounds,QueryHalfExtents))
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
		{
			//QUICK_SCOPE_CYCLE_COUNTER(QueryDirty);

			for (const auto& Elem : DirtyElements)
			{
				const auto& InstanceBounds = Elem.Bounds;
				if (PrePreFilterHelper(Elem.Payload, QueryData))
				{
					continue;
				}

				if (TAABBTreeIntersectionHelper<T, TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents))
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
			}

		}

		struct FNodeQueueEntry
		{
			int32 NodeIdx;
			T TOI;
		};

		TArray<FNodeQueueEntry> NodeStack;
		NodeStack.Add(FNodeQueueEntry{ 0, 0 });
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
					if (Leaf.SweepFast(Start, CurData, QueryHalfExtents, Visitor) == false)
					{
						return false;
					}
				}
				else if (Leaf.RaycastFast(Start, CurData, Visitor) == false)
				{
					return false;
				}
			}
			else
			{
				int32 Idx = 0;
				for (const TAABB<T, 3>& AABB : Node.ChildrenBounds)
				{
					if(TAABBTreeIntersectionHelper<T, TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, AABB, QueryBounds, QueryHalfExtents))
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
		PayloadToInfo.Reset();
		NumProcessedThisSlice = 0;

		WorkSnapshot.Bounds = TAABB<T, 3>::EmptyAABB();

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeTimeSliceSetup);

			int32 Idx = 0;

			//TODO: we need a better way to time-slice this case since there can be a huge number of Particles. Can't do it right now without making full copy
			for (auto& Particle : Particles)
			{
				bool bHasBoundingBox = HasBoundingBox(Particle);
				auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
				TAABB<T, 3> ElemBounds = ComputeWorldSpaceBoundingBox(Particle, false, (T)0);

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
						PayloadToInfo.Add(Payload, FAABBTreePayloadInfo{ GlobalPayloads.Num(), INDEX_NONE, INDEX_NONE });
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
		TAABB<T, 3> SplitBounds;	//Even split of parent bounds
		TAABB<T, 3> RealBounds;	//Actual bounds as children are added
		int32 WorkSnapshotIdx;	//Idx into work snapshot pool
		T SplitBoundsSize2;
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
			T MinDelta2 = TNumericLimits<T>::Max();
			int32 BoxIdx = 0;
			for (const FSplitInfo& SplitInfo : CurrentSnapshot.SplitInfos)
			{
				TAABB<T, 3> NewBox = SplitInfo.SplitBounds;
				NewBox.GrowToInclude(Elem.Bounds);
				const T Delta2 = NewBox.Extents().SizeSquared() - SplitInfo.SplitBoundsSize2;
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
				const TVec3<T> Extents = WorkPool[CurIdx].Bounds.Extents();
				const int32 MaxAxis = WorkPool[CurIdx].Bounds.LargestAxis();

				//Add two children, remember this invalidates any pointers to current snapshot
				const int32 FirstChildIdx = GetNewWorkSnapshot();
				const int32 SecondChildIdx = GetNewWorkSnapshot();

				//mark idx of children into the work pool
				WorkPool[CurIdx].SplitInfos[0].WorkSnapshotIdx = FirstChildIdx;
				WorkPool[CurIdx].SplitInfos[1].WorkSnapshotIdx = SecondChildIdx;

				//these are the hypothetical bounds for the perfect 50/50 split
				WorkPool[CurIdx].SplitInfos[0].SplitBounds = TAABB<T, 3>(WorkPool[CurIdx].Bounds.Min(), WorkPool[CurIdx].Bounds.Min());
				WorkPool[CurIdx].SplitInfos[1].SplitBounds = TAABB<T, 3>(WorkPool[CurIdx].Bounds.Max(), WorkPool[CurIdx].Bounds.Max());

				const TVec3<T> Center = WorkPool[CurIdx].Bounds.Center();
				for (FSplitInfo& SplitInfo : WorkPool[CurIdx].SplitInfos)
				{
					SplitInfo.RealBounds = TAABB<T, 3>::EmptyAABB();

					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						TVec3<T> NewPt0 = Center;
						TVec3<T> NewPt1 = Center;
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

	TArray<TPayloadType> FindAllIntersectionsImp(const TAABB<T, 3>& Intersection) const
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


	TAABBTree(const TAABBTree<TPayloadType, TLeafType, T, bMutable>& Other)
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, Nodes(Other.Nodes)
		, Leaves(Other.Leaves)
		, DirtyElements(Other.DirtyElements)
		, GlobalPayloads(Other.GlobalPayloads)
		, PayloadToInfo(Other.PayloadToInfo)
		, MaxChildrenInLeaf(Other.MaxChildrenInLeaf)
		, MaxTreeDepth(Other.MaxTreeDepth)
		, MaxPayloadBounds(Other.MaxPayloadBounds)
		, MaxNumToProcess(Other.MaxNumToProcess)
		, NumProcessedThisSlice(Other.NumProcessedThisSlice)
	{

	}

	TAABBTree<TPayloadType,TLeafType,T,bMutable>& operator=(const TAABBTree<TPayloadType,TLeafType,T,bMutable>& Rhs)
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
};

template<typename TPayloadType, typename TLeafType, class T, bool bMutable>
FArchive& operator<<(FChaosArchive& Ar, TAABBTree<TPayloadType, TLeafType, T, bMutable>& AABBTree)
{
	AABBTree.Serialize(Ar);
	return Ar;
}


}
