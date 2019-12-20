// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/AABB.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Templates/Models.h"
#include "Chaos/BoundingVolume.h"

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
	static bool Intersects(const TVector<T, 3>& Start, TQueryFastData& QueryFastData, T& TOI, TVector<T, 3>& OutPosition,
		const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
	{
		check(false);
		return true;
	}
};

template<>
struct TAABBTreeIntersectionHelper<FReal, FQueryFastData, EAABBQueryType::Raycast>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const FVec3& Start, FQueryFastData& QueryFastData, FReal& TOI, FVec3& OutPosition,
		const TAABB<FReal, 3>& Bounds, const TAABB<FReal, 3>& QueryBounds, const TVector<FReal, 3>& QueryHalfExtents)
	{
		return Bounds.RaycastFast(Start, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, FQueryFastData, EAABBQueryType::Sweep>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVector<T, 3>& Start, FQueryFastData& QueryFastData,
		T& TOI, TVector<T, 3>& OutPosition, const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
	{
		TAABB<T, 3> SweepBounds(Bounds.Min() - QueryHalfExtents, Bounds.Max() + QueryHalfExtents);
		return SweepBounds.RaycastFast(Start, QueryFastData.Dir, QueryFastData.InvDir, QueryFastData.bParallel, QueryFastData.CurrentLength, QueryFastData.InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, FQueryFastDataVoid, EAABBQueryType::Overlap>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVector<T, 3>& Start, FQueryFastDataVoid& QueryFastData, T& TOI, TVector<T, 3>& OutPosition,
		const TAABB<T, 3>& Bounds, const TAABB<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
	{
		return QueryBounds.Intersects(Bounds);
	}
};

template <typename TPayloadType, typename T>
struct TAABBTreeLeafArray
{
	TAABBTreeLeafArray() {}
	//todo: avoid copy?
	TAABBTreeLeafArray(const TArray<TPayloadBoundsElement<TPayloadType, T>>& InElems)
		: Elems(InElems)
	{
	}

	template <typename TSQVisitor, typename TQueryFastData>
	bool RaycastFast(const TVector<T,3>& Start, TQueryFastData& QueryFastData, TSQVisitor& Visitor) const
	{
		return RaycastSweepImp</*bSweep=*/false>(Start, QueryFastData, TVector<T,3>(), Visitor);
	}

	template <typename TSQVisitor, typename TQueryFastData>
	bool SweepFast(const TVector<T, 3>& Start, TQueryFastData& QueryFastData, const TVector<T,3>& QueryHalfExtents, TSQVisitor& Visitor) const
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
	bool RaycastSweepImp(const TVector<T, 3>& Start, TQueryFastData& QueryFastData, const TVector<T, 3>& QueryHalfExtents, TSQVisitor& Visitor) const
	{
		TVector<T, 3> TmpPosition;
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

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Elems;
	}

	TArray<TPayloadBoundsElement<TPayloadType, T>> Elems;
};

template <typename TPayloadType, typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TAABBTreeLeafArray<TPayloadType, T>& LeafArray)
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
	int32 ChildrenNodes[2];
	bool bLeaf;

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
		if (!TimeSliceWorkToComplete.IsEmpty())
		{
			FWorkSnapshot Store;
			TimeSliceWorkToComplete.Dequeue(Store);
			NumProcessedThisSlice = 0;
			if (Store.TimeslicePhase == eTimeSlicePhase::PHASE1)
			{
				SplitNode(Store.Bounds, Store.Elems, Store.NodeLevel, Store.NewNodeIdx);
			}
			else
			{
				check(Store.TimeslicePhase == eTimeSlicePhase::PHASE2);
				SplitNode(Store.Bounds, Store.RealBounds, Store.Elems, Store.SplitBoundsSize2, Store.NewNodeIdx, Store.NodeLevel, Store.BoxIdx);
			}
		}

		// are done now?
		if (TimeSliceWorkToComplete.IsEmpty())
		{
			this->SetAsyncTimeSlicingComplete(true);
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

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>>(new TAABBTree<TPayloadType, TLeafType, T, bMutable>(*this));
	}

	virtual void Raycast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Raycast(Start, Dir, Length, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Raycast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Raycast>(Start, QueryFastData, TVector<T,3>(), TAABB<T,3>(), Visitor);
	}

	template <typename SQVisitor>
	bool RaycastFast(const TVector<T, 3>& Start, FQueryFastData& CurData, SQVisitor& Visitor) const
	{
		return QueryImp<EAABBQueryType::Raycast>(Start, CurData, TVector<T, 3>(), TAABB<T, 3>(), Visitor);
	}

	void Sweep(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, const TVector<T, 3> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Sweep(Start, Dir, Length, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Sweep(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, const TVector<T, 3> QueryHalfExtents, SQVisitor& Visitor) const
	{
		FQueryFastData QueryFastData(Dir, Length);
		QueryImp<EAABBQueryType::Sweep>(Start, QueryFastData, QueryHalfExtents, TAABB<T, 3>(), Visitor);
	}

	template <typename SQVisitor>
	bool SweepFast(const TVector<T, 3>& Start, FQueryFastData& CurData, const TVector<T, 3> QueryHalfExtents, SQVisitor& Visitor) const
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
		return QueryImp<EAABBQueryType::Overlap>(TVector<T, 3>(), VoidData, TVector<T, 3>(), QueryBounds, Visitor);
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
				TAABB<T, 3> GlobalBounds = bTooBig ? NewBounds : TAABB<T, 3>(TVector<T, 3>(TNumericLimits<T>::Lowest()), TVector<T, 3>(TNumericLimits<T>::Max()));
				if (PayloadInfo->GlobalPayloadIdx == INDEX_NONE)
				{
					PayloadInfo->GlobalPayloadIdx = GlobalPayloads.Add(FElement{ Payload, GlobalBounds });
				}
				else
				{
					GlobalPayloads[PayloadInfo->GlobalPayloadIdx].Bounds = GlobalBounds;
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
	}

	const TArray<TPayloadBoundsElement<TPayloadType, T>>& GlobalObjects() const
	{
		return GlobalPayloads;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

		TBox<FReal, 3>::SerializeAsAABB(Ar, FullBounds);
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
			TArray<TPayloadType> Payloads;
			if (!Ar.IsLoading())
			{
				PayloadToInfo.GenerateKeyArray(Payloads);
			}
			Ar << Payloads;

			for (auto Payload : Payloads)
			{
				auto& Info = PayloadToInfo.FindOrAdd(Payload);
				Ar << Info;
			}
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

	template <EAABBQueryType Query, typename TQueryFastData, typename SQVisitor>
	bool QueryImp(const TVector<T, 3>& Start, TQueryFastData& CurData, const TVector<T, 3> QueryHalfExtents, const TAABB<T,3>& QueryBounds, SQVisitor& Visitor) const
	{
		TVector<T, 3> TmpPosition;
		T TOI = 0;

		for (const auto& Elem : GlobalPayloads)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TAABBTreeIntersectionHelper<T, TQueryFastData, Query>::Intersects(Start, CurData, TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true);
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

		if (bMutable)
		{
			for (const auto& Elem : DirtyElements)
			{
				const auto& InstanceBounds = Elem.Bounds;
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

	template <typename TParticles>
	void GenerateTree(const TParticles& Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_AABBTreeGenerateTree);
		this->SetAsyncTimeSlicingComplete(false);

		TArray<FElement> ElemsWithBounds;
		ElemsWithBounds.Reserve(Particles.Num());

		GlobalPayloads.Reset();
		Leaves.Reset();
		Nodes.Reset();
		DirtyElements.Reset();
		PayloadToInfo.Reset();
		NumProcessedThisSlice = 0;

		FullBounds = TAABB<T, 3>::EmptyAABB();

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeTimeSliceSetup);

			int32 Idx = 0;

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
						ElemsWithBounds.Add(FElement{ Payload, ElemBounds });
						FullBounds.GrowToInclude(ElemBounds);
					}
				}
				else
				{
					ElemBounds = TAABB<T, 3>(TVector<T, 3>(TNumericLimits<T>::Lowest()), TVector<T, 3>(TNumericLimits<T>::Max()));
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

		{
			SCOPE_CYCLE_COUNTER(STAT_AABBTreeInitialTimeSlice);

			SplitNode(FullBounds, ElemsWithBounds, 0, 0, true);

			if (TimeSliceWorkToComplete.IsEmpty())
			{
				this->SetAsyncTimeSlicingComplete(true);
			}
		}

	}

	void SplitNode(TAABB<T, 3>& SplitBounds, TAABB<T, 3>& RealBounds, TArray<FElement>& Children, T& SplitBoundsSize2, int NewNodeIdx, int NodeLevel, int BoxIdx)
	{
		//SCOPE_CYCLE_COUNTER(STAT_AABBTreeChildrenPhase);

		check (Children.Num() > 0)
		{
			Nodes[NewNodeIdx].bLeaf = false;

			// increment this early so the amount of recursion work left on the stack is smaller
			NumProcessedThisSlice += Children.Num();

			const int32 ChildIdx = Nodes.Num();
			Nodes[NewNodeIdx].ChildrenBounds[BoxIdx] = RealBounds;
			Nodes[NewNodeIdx].ChildrenNodes[BoxIdx] = ChildIdx;

			SplitNode(RealBounds, Children, NodeLevel + 1, ChildIdx);
		}

	}

	void SplitNode(const TAABB<T, 3>& Bounds, const TArray<FElement>& Elems, int32 NodeLevel, int32 NewNodeIdx, bool Initial=false)
	{
		// create the actual node space but might no be filled in (YET) due to time slicing exit
		if (NewNodeIdx >= Nodes.Num())
		{
		 	Nodes.AddDefaulted();	//todo: remove TAABB
		}

		bool WeAreTimeslicing = (MaxNumToProcess > 0);

		if (WeAreTimeslicing && (NumProcessedThisSlice >= MaxNumToProcess))
		{
			// done enough work, capture stack
			FWorkSnapshot Store;
			Store.TimeslicePhase = eTimeSlicePhase::PHASE1;
			Store.Bounds = Bounds;
			Store.Elems = Elems;			
			Store.NodeLevel = NodeLevel;
			Store.NewNodeIdx = NewNodeIdx;

			TimeSliceWorkToComplete.Enqueue(Store);

			return; // done enough
		}

		auto& PayloadToInfoRef = PayloadToInfo;
		auto& LeavesRef = Leaves;
		auto& NodesRef = Nodes;
		auto MakeLeaf = [NewNodeIdx, &PayloadToInfoRef, &Elems, &LeavesRef, &NodesRef]()
		{
			if (bMutable)
			{
				for (const FElement& Elem : Elems)
				{
					PayloadToInfoRef.Add(Elem.Payload, FAABBTreePayloadInfo{ INDEX_NONE, INDEX_NONE, LeavesRef.Num() });
				}
			}

			NodesRef[NewNodeIdx].bLeaf = true;
			NodesRef[NewNodeIdx].ChildrenNodes[0] = LeavesRef.Add(TLeafType{ Elems }); //todo: avoid copy?

		};

		if (Elems.Num() <= MaxChildrenInLeaf || NodeLevel >= MaxTreeDepth)
		{
			MakeLeaf();
			return; // keep working
		}

		const TVector<T, 3> Extents = Bounds.Extents();
		const int32 MaxAxis = Bounds.LargestAxis();
		bool ChildrenInBothHalves = false;

		FSplitInfo SplitInfos[2];
		SplitInfos[0].SplitBounds = TAABB<T, 3>(Bounds.Min(), Bounds.Min());
		SplitInfos[1].SplitBounds = TAABB<T, 3>(Bounds.Max(), Bounds.Max());

		const TVector<T, 3> Center = Bounds.Center();
		for (FSplitInfo& SplitInfo : SplitInfos)
		{
			SplitInfo.RealBounds = TAABB<T, 3>::EmptyAABB();

			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				TVector<T, 3> NewPt0 = Center;
				TVector<T, 3> NewPt1 = Center;
				if (Axis != MaxAxis)
				{
					NewPt0[Axis] = Bounds.Min()[Axis];
					NewPt1[Axis] = Bounds.Max()[Axis];
					SplitInfo.SplitBounds.GrowToInclude(NewPt0);
					SplitInfo.SplitBounds.GrowToInclude(NewPt1);
				}
			}

			SplitInfo.SplitBoundsSize2 = SplitInfo.SplitBounds.Extents().SizeSquared();
		}
		
		{
			//SCOPE_CYCLE_COUNTER(STAT_AABBTreeGrowPhase);

			// add all elements to one of the two split infos at this level - root level [ not taking into account the max number allowed or anything
			for (const FElement& Elem : Elems)
			{
				int32 MinBoxIdx = INDEX_NONE;
				T MinDelta2 = TNumericLimits<T>::Max();
				int32 BoxIdx = 0;
				for (const FSplitInfo& SplitInfo : SplitInfos)
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
					SplitInfos[MinBoxIdx].Children.Add(Elem);
					SplitInfos[MinBoxIdx].RealBounds.GrowToInclude(Elem.Bounds);
				}
			}

			NumProcessedThisSlice += Elems.Num();

			ChildrenInBothHalves = SplitInfos[0].Children.Num() && SplitInfos[1].Children.Num();

			if (ChildrenInBothHalves && WeAreTimeslicing && (NumProcessedThisSlice >= MaxNumToProcess))
			{
				for (int32 BoxIdx = 0; BoxIdx < 2; ++BoxIdx)
				{
					// done enough work, capture stack
					FWorkSnapshot Store;
					Store.TimeslicePhase = eTimeSlicePhase::PHASE2;
					Store.NodeLevel = NodeLevel;
					Store.NewNodeIdx = NewNodeIdx;
					Store.BoxIdx = BoxIdx;
					Store.Bounds = MoveTemp(SplitInfos[BoxIdx].SplitBounds);
					Store.Elems = MoveTemp(SplitInfos[BoxIdx].Children);
					Store.RealBounds = MoveTemp(SplitInfos[BoxIdx].RealBounds);
					Store.SplitBoundsSize2 = SplitInfos[BoxIdx].SplitBoundsSize2;
					TimeSliceWorkToComplete.Enqueue(Store);
				}
				return; // done enough
			}	
		}

		{			
			//SCOPE_CYCLE_COUNTER(STAT_AABBTreeChildrenPhase);

			// if children in both halves, recurse a level down
			if (ChildrenInBothHalves)
			{
				Nodes[NewNodeIdx].bLeaf = false;

				// increment this early so the amount of recursion work left on the stack is smaller
				NumProcessedThisSlice += SplitInfos[0].Children.Num() + SplitInfos[1].Children.Num();

				for (int32 BoxIdx = 0; BoxIdx < 2; ++BoxIdx)
				{
					const int32 ChildIdx = Nodes.Num();
					Nodes[NewNodeIdx].ChildrenBounds[BoxIdx] = SplitInfos[BoxIdx].RealBounds;
					Nodes[NewNodeIdx].ChildrenNodes[BoxIdx] = ChildIdx;

					SplitNode(SplitInfos[BoxIdx].RealBounds, SplitInfos[BoxIdx].Children, NodeLevel + 1, ChildIdx);
				}
			}
			else
			{
				//couldn't split so just make a leaf - THIS COULD CONTAIN MORE THAN MaxChildrenInLeaf!!!
				MakeLeaf();
			}
		}

		return; // keep working
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
			TArray<TPayloadType>& CollectedResults;
		};

		TArray<TPayloadType> Results;
		FSimpleVisitor Collector(Results);
		Overlap(Intersection, Collector);

		return Results;
	}


	TAABBTree(const TAABBTree<TPayloadType, TLeafType, T, bMutable>& Other)
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, FullBounds(Other.FullBounds)
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

	struct FSplitInfo
	{
		TAABB<T, 3> SplitBounds;	//Even split of parent bounds
		TAABB<T, 3> RealBounds;	//Actual bounds as children are added
		TArray<FElement> Children;
		T SplitBoundsSize2;
	};

	TAABB<T, 3> FullBounds;
	TArray<FNode> Nodes;
	TArray<TLeafType> Leaves;
	TArray<FElement> DirtyElements;
	TArray<FElement> GlobalPayloads;
	TMap<TPayloadType, FAABBTreePayloadInfo> PayloadToInfo;
	int32 MaxChildrenInLeaf;
	int32 MaxTreeDepth;
	T MaxPayloadBounds;
	int32 MaxNumToProcess;
	
	enum eTimeSlicePhase
	{
		PHASE1,
		PHASE2
	};

	struct FWorkSnapshot
	{
		eTimeSlicePhase TimeslicePhase;

		// used for phase 1 & 2
		TAABB<T, 3> Bounds;
		TArray<FElement> Elems;

		int32 NodeLevel;
		int32 NewNodeIdx;
	
		// phase 2 data only
		TAABB<T, 3> RealBounds;
		int8 BoxIdx;
		T SplitBoundsSize2;
	};

	int32 NumProcessedThisSlice;
	TQueue<FWorkSnapshot> TimeSliceWorkToComplete;
};

template<typename TPayloadType, typename TLeafType, class T, bool bMutable>
FArchive& operator<<(FChaosArchive& Ar, TAABBTree<TPayloadType, TLeafType, T, bMutable>& AABBTree)
{
	AABBTree.Serialize(Ar);
	return Ar;
}


}
