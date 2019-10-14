// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Templates/Models.h"

namespace Chaos
{

enum class EAABBQueryType
{
	Raycast,
	Sweep,
	Overlap
};

template <typename T, EAABBQueryType Query>
struct TAABBTreeIntersectionHelper
{
	static bool Intersects(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel,
		T& CurrentLength, T& InvCurrentLength, T& TOI, TVector<T, 3>& OutPosition,
		const TBox<T, 3>& Bounds, const TBox<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents);
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, EAABBQueryType::Raycast>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel,
		T& CurrentLength, T& InvCurrentLength, T& TOI, TVector<T, 3>& OutPosition,
		const TBox<T, 3>& Bounds, const TBox<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
	{
		return TBox<T, 3>::RaycastFast(Bounds.Min(), Bounds.Max(), Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, EAABBQueryType::Sweep>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel,
		T& CurrentLength, T& InvCurrentLength, T& TOI, TVector<T, 3>& OutPosition,
		const TBox<T, 3>& Bounds, const TBox<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
	{
		return TBox<T, 3>::RaycastFast(Bounds.Min() - QueryHalfExtents, Bounds.Max() + QueryHalfExtents, Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TOI, OutPosition);
	}
};

template <typename T>
struct TAABBTreeIntersectionHelper<T, EAABBQueryType::Overlap>
{
	FORCEINLINE_DEBUGGABLE static bool Intersects(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel,
		T& CurrentLength, T& InvCurrentLength, T& TOI, TVector<T, 3>& OutPosition,
		const TBox<T, 3>& Bounds, const TBox<T, 3>& QueryBounds, const TVector<T, 3>& QueryHalfExtents)
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

	template <typename TSQVisitor>
	bool RaycastFast(const TVector<T,3>& Start, const TVector<T,3>& Dir, const TVector<T,3>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, TSQVisitor& Visitor) const
	{
		return RaycastSweepImp</*bSweep=*/false>(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, TVector<T,3>(), Visitor);
	}

	template <typename TSQVisitor>
	bool SweepFast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, const TVector<T,3>& QueryHalfExtents, TSQVisitor& Visitor) const
	{
		return RaycastSweepImp</*bSweep=*/true>(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, QueryHalfExtents, Visitor);
	}

	template <typename TSQVisitor>
	bool OverlapFast(const TBox<T, 3>& QueryBounds, TSQVisitor& Visitor) const
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

	template <bool bSweep, typename TSQVisitor>
	bool RaycastSweepImp(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T, 3>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, const TVector<T, 3>& QueryHalfExtents, TSQVisitor& Visitor) const
	{
		TVector<T, 3> TmpPosition;
		T TOI;
		for (const auto& Elem : Elems)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TAABBTreeIntersectionHelper<T, bSweep ? EAABBQueryType::Sweep : EAABBQueryType::Raycast>::Intersects(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength,
				TOI, TmpPosition, InstanceBounds, TBox<T,3>(), QueryHalfExtents))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				const bool bContinue = (bSweep && Visitor.VisitSweep(VisitData, CurrentLength)) || (!bSweep &&  Visitor.VisitRaycast(VisitData, CurrentLength));
				if (!bContinue)
				{
					return false;
				}
				InvCurrentLength = 1 / CurrentLength;
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
		ChildrenBounds[0] = TBox<T, 3>();
		ChildrenBounds[1] = TBox<T, 3>();
	}
	TBox<T, 3> ChildrenBounds[2];
	int32 ChildrenNodes[2];
	bool bLeaf;

	void Serialize(FChaosArchive& Ar)
	{
		for (auto& Bounds : ChildrenBounds)
		{
			Ar << Bounds;
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

template <typename TPayloadType, typename TLeafType, typename T>
class TAABBTree final : public ISpatialAcceleration<TPayloadType, T, 3>
{
public:
	using PayloadType = TPayloadType;
	static constexpr int D = 3;
	using TType = T;
	static constexpr T DefaultMaxPayloadBounds = 100000;
	static constexpr int32 DefaultMaxChildrenInLeaf = 12;
	static constexpr int32 DefaultMaxTreeDepth = 16;
	static constexpr ESpatialAcceleration StaticType = TIsSame<TAABBTreeLeafArray<TPayloadType, T>, TLeafType>::Value ? ESpatialAcceleration::AABBTree : 
		(TIsSame<TBoundingVolume<TPayloadType, T, 3>, TLeafType>::Value ? ESpatialAcceleration::AABBTreeBV : ESpatialAcceleration::Unknown);
	TAABBTree()
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, MaxChildrenInLeaf(DefaultMaxChildrenInLeaf)
		, MaxTreeDepth(DefaultMaxTreeDepth)
		, MaxPayloadBounds(DefaultMaxPayloadBounds)
	{
	}

	template <typename TParticles>
	TAABBTree(const TParticles& Particles, int32 InMaxChildrenInLeaf = 12, int32 InMaxTreeDepth = 16, T InMaxPayloadBounds = DefaultMaxPayloadBounds)
		: ISpatialAcceleration<TPayloadType, T, 3>(StaticType)
		, MaxChildrenInLeaf(InMaxChildrenInLeaf)
		, MaxTreeDepth(InMaxTreeDepth)
		, MaxPayloadBounds(InMaxPayloadBounds)
	{
		GenerateTree(Particles);
	}

	virtual ~TAABBTree() {}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>> Copy() const override
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, 3>>(new TAABBTree<TPayloadType, TLeafType, T>(*this));
	}

	virtual void Raycast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Raycast(Start, Dir, Length, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Raycast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, SQVisitor& Visitor) const
	{
		ensure(Length);
		T CurLength = Length;
		bool bParallel[3];
		TVector<T, 3> InvDir;

		T InvLength = 1 / Length;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		QueryImp<EAABBQueryType::Raycast>(Start, Dir, InvDir, bParallel, CurLength, InvLength, TVector<T,3>(), TBox<T,3>(), Visitor);
	}

	template <typename SQVisitor>
	bool RaycastFast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T,3>& InvDir, const bool* bParallel, T& CurLength, T& InvLength, SQVisitor& Visitor) const
	{
		return QueryImp<EAABBQueryType::Raycast>(Start, Dir, InvDir, bParallel, CurLength, InvLength, TVector<T, 3>(), TBox<T, 3>(), Visitor);
	}

	void Sweep(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, const TVector<T, 3> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Sweep(Start, Dir, Length, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Sweep(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const T Length, const TVector<T, 3> QueryHalfExtents, SQVisitor& Visitor) const
	{
		bool bParallel[3];
		TVector<T, 3> InvDir;

		ensure(Length);
		T CurLength = Length;
		T InvLength = 1 / Length;
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			bParallel[Axis] = Dir[Axis] == 0;
			InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
		}

		QueryImp<EAABBQueryType::Sweep>(Start, Dir, InvDir, bParallel, CurLength, InvLength, QueryHalfExtents, TBox<T, 3>(), Visitor);
	}

	template <typename SQVisitor>
	bool SweepFast(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T,3>& InvDir, const bool* bParallel, T& CurLength, T& InvLength, const TVector<T, 3> QueryHalfExtents, SQVisitor& Visitor) const
	{
		return QueryImp<EAABBQueryType::Sweep>(Start, Dir, InvDir, bParallel, CurLength, InvLength, QueryHalfExtents, TBox<T, 3>(), Visitor);
	}

	void Overlap(const TBox<T, 3>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const override
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Overlap(QueryBounds, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Overlap(const TBox<T,3>& QueryBounds, SQVisitor& Visitor) const
	{
		OverlapFast(QueryBounds, Visitor);
	}

	template <typename SQVisitor>
	bool OverlapFast(const TBox<T, 3>& QueryBounds, SQVisitor& Visitor) const
	{
		//dummy variables to reuse templated path
		T Length;
		T InvLength;
		TVector<T, 3> InvDir;
		bool bParallel;
		return QueryImp<EAABBQueryType::Overlap>(TVector<T, 3>(), TVector<T, 3>(), InvDir, &bParallel, Length, InvLength, TVector<T, 3>(), QueryBounds, Visitor);
	}

	virtual void RemoveElement(const TPayloadType& Payload)
	{
		if (FAABBTreePayloadInfo* PayloadInfo = PayloadToInfo.Find(Payload))
		{
			if (PayloadInfo->GlobalPayloadIdx != INDEX_NONE)
			{
				ensure(PayloadInfo->DirtyPayloadIdx == INDEX_NONE);
				ensure(PayloadInfo->LeafIdx == INDEX_NONE);
				if(PayloadInfo->GlobalPayloadIdx + 1 < GlobalPayloads.Num())
				{
					auto LastGlobalPayload = GlobalPayloads.Last().Payload;
					PayloadToInfo.FindChecked(LastGlobalPayload).GlobalPayloadIdx = PayloadInfo->GlobalPayloadIdx;
				}
				GlobalPayloads.RemoveAtSwap(PayloadInfo->GlobalPayloadIdx);
			}
			else if(PayloadInfo->DirtyPayloadIdx != INDEX_NONE)
			{
				if(PayloadInfo->DirtyPayloadIdx + 1 < DirtyElements.Num())
				{
					auto LastDirtyPayload = DirtyElements.Last().Payload;
					PayloadToInfo.FindChecked(LastDirtyPayload).DirtyPayloadIdx = PayloadInfo->DirtyPayloadIdx;
				}
				DirtyElements.RemoveAtSwap(PayloadInfo->DirtyPayloadIdx);
			}
			else if(ensure(PayloadInfo->LeafIdx != INDEX_NONE))
			{
				Leaves[PayloadInfo->LeafIdx].RemoveElement(Payload);
			}

			PayloadToInfo.Remove(Payload);
		}
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TBox<T, 3>& NewBounds, bool bHasBounds) override
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
			PayloadInfo->GlobalPayloadIdx = INDEX_NONE;
		}
		else
		{
			TBox<T, 3> GlobalBounds = bTooBig ? NewBounds : TBox<T, 3>(TVector<T, 3>(TNumericLimits<T>::Lowest()), TVector<T, 3>(TNumericLimits<T>::Max()));
			if (PayloadInfo->GlobalPayloadIdx == INDEX_NONE)
			{
				PayloadInfo->GlobalPayloadIdx = GlobalPayloads.Add(FElement{ Payload, GlobalBounds });
			}
			else
			{
				GlobalPayloads[PayloadInfo->GlobalPayloadIdx].Bounds = GlobalBounds;
			}
			PayloadInfo->DirtyPayloadIdx = INDEX_NONE;
		}
	}

	const TArray<TPayloadBoundsElement<TPayloadType, T>>& GlobalObjects() const
	{
		return GlobalPayloads;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		Ar << FullBounds;
		Ar << Nodes;
		Ar << Leaves;
		Ar << DirtyElements;
		Ar << GlobalPayloads;

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

		Ar << MaxChildrenInLeaf;
		Ar << MaxTreeDepth;
		Ar << MaxPayloadBounds;
	}

private:

	using FElement = TPayloadBoundsElement<TPayloadType, T>;
	using FNode = TAABBTreeNode<T>;

	template <EAABBQueryType Query, typename SQVisitor>
	bool QueryImp(const TVector<T, 3>& Start, const TVector<T, 3>& Dir, const TVector<T,3>& InvDir, const bool* bParallel, T& CurrentLength, T& InvCurrentLength, const TVector<T, 3> QueryHalfExtents, const TBox<T,3>& QueryBounds, SQVisitor& Visitor) const
	{
		TVector<T, 3> TmpPosition;
		T TOI = 0;

		for (const auto& Elem : GlobalPayloads)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if (TAABBTreeIntersectionHelper<T, Query>::Intersects(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength,
				TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true);
				bool bContinue;
				if (Query == EAABBQueryType::Overlap)
				{
					bContinue = Visitor.VisitOverlap(VisitData);
				}
				else
				{
					bContinue = Query == EAABBQueryType::Sweep ? Visitor.VisitSweep(VisitData, CurrentLength) : Visitor.VisitRaycast(VisitData, CurrentLength);
				}

				if (!bContinue)
				{
					return false;
				}

				if (Query != EAABBQueryType::Overlap)
				{
					InvCurrentLength = 1 / CurrentLength;
				}
			}
		}

		for (const auto& Elem : DirtyElements)
		{
			const auto& InstanceBounds = Elem.Bounds;
			if(TAABBTreeIntersectionHelper<T, Query>::Intersects(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength,
				TOI, TmpPosition, InstanceBounds, QueryBounds, QueryHalfExtents))
			{
				TSpatialVisitorData<TPayloadType> VisitData(Elem.Payload, true, InstanceBounds);
				
				bool bContinue;
				if (Query == EAABBQueryType::Overlap)
				{
					bContinue = Visitor.VisitOverlap(VisitData);
				}
				else
				{
					bContinue = Query == EAABBQueryType::Sweep ? Visitor.VisitSweep(VisitData, CurrentLength) : Visitor.VisitRaycast(VisitData, CurrentLength);
				}
				
				if (!bContinue)
				{
					return false;
				}

				if (Query != EAABBQueryType::Overlap)
				{
					InvCurrentLength = 1 / CurrentLength;
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
				if (NodeEntry.TOI > CurrentLength)
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
					if (Leaf.SweepFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, QueryHalfExtents, Visitor) == false)
					{
						return false;
					}
				}
				else if (Leaf.RaycastFast(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength, Visitor) == false)
				{
					return false;
				}
			}
			else
			{
				int32 Idx = 0;
				for (const TBox<T, 3>& AABB : Node.ChildrenBounds)
				{
					if(TAABBTreeIntersectionHelper<T, Query>::Intersects(Start, Dir, InvDir, bParallel, CurrentLength, InvCurrentLength,
						TOI, TmpPosition, AABB, QueryBounds, QueryHalfExtents))
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
		TArray<FElement> ElemsWithBounds;

		ElemsWithBounds.Reserve(Particles.Num());

		GlobalPayloads.Reset();
		Leaves.Reset();
		Nodes.Reset();
		DirtyElements.Reset();
		PayloadToInfo.Reset();

		int32 Idx = 0;
		FullBounds = TBox<T, 3>::EmptyBox();
		for (auto& Particle : Particles)
		{
			bool bHasBoundingBox = HasBoundingBox(Particle);
			auto Payload = Particle.template GetPayload<TPayloadType>(Idx);
			TBox<T, 3> ElemBounds = ComputeWorldSpaceBoundingBox(Particle, false, (T)0);

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
				ElemBounds = TBox<T,3>(TVector<T, 3>(TNumericLimits<T>::Lowest()), TVector<T, 3>(TNumericLimits<T>::Max()));
			}
			
			if (!bHasBoundingBox)
			{
				PayloadToInfo.Add(Payload, FAABBTreePayloadInfo{ GlobalPayloads.Num(), INDEX_NONE, INDEX_NONE });
				GlobalPayloads.Add(FElement{ Payload, ElemBounds });
			}

			++Idx;
			//todo: payload info
		}

		SplitNode(FullBounds, ElemsWithBounds, 0);
	}

	int32 SplitNode(const TBox<T, 3>& Bounds, const TArray<FElement>& Elems, int32 NodeLevel)
	{
		const int32 NewNodeIdx = Nodes.Num();
		Nodes.AddDefaulted();	//todo: remove TBox

		auto& PayloadToInfoRef = PayloadToInfo;
		auto& LeavesRef = Leaves;
		auto& NodesRef = Nodes;
		auto MakeLeaf = [NewNodeIdx, &PayloadToInfoRef, &Elems, &LeavesRef, &NodesRef]()
		{
			for (const FElement& Elem : Elems)
			{
				PayloadToInfoRef.Add(Elem.Payload, FAABBTreePayloadInfo{ INDEX_NONE, INDEX_NONE, LeavesRef.Num() });
			}

			NodesRef[NewNodeIdx].bLeaf = true;
			NodesRef[NewNodeIdx].ChildrenNodes[0] = LeavesRef.Add(TLeafType{ Elems }); //todo: avoid copy?

		};

		if (Elems.Num() <= MaxChildrenInLeaf || NodeLevel >= MaxTreeDepth)
		{
			MakeLeaf();
			return NewNodeIdx;
		}

		const TVector<T, 3> Extents = Bounds.Extents();
		const int32 MaxAxis = Bounds.LargestAxis();

		struct FSplitInfo
		{
			TBox<T, 3> SplitBounds;	//Even split of parent bounds
			TBox<T, 3> RealBounds;	//Actual bounds as children are added
			TArray<FElement> Children;
			T SplitBoundsSize2;
		};

		FSplitInfo SplitInfos[2];
		SplitInfos[0].SplitBounds = TBox<T, 3>(Bounds.Min(), Bounds.Min());
		SplitInfos[1].SplitBounds = TBox<T, 3>(Bounds.Max(), Bounds.Max());
		
		const TVector<T, 3> Center = Bounds.Center();
		for (FSplitInfo& SplitInfo : SplitInfos)
		{
			SplitInfo.RealBounds = TBox<T, 3>::EmptyBox();

			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				TVector<T,3> NewPt0 = Center;
				TVector<T,3> NewPt1 = Center;
				if(Axis != MaxAxis)
				{
					NewPt0[Axis] = Bounds.Min()[Axis];
					NewPt1[Axis] = Bounds.Max()[Axis];
					SplitInfo.SplitBounds.GrowToInclude(NewPt0);
					SplitInfo.SplitBounds.GrowToInclude(NewPt1);
				}
			}

			SplitInfo.SplitBoundsSize2 = SplitInfo.SplitBounds.Extents().SizeSquared();
		}
		
		for (const FElement& Elem : Elems)
		{
			int32 MinBoxIdx = INDEX_NONE;
			T MinDelta2 = TNumericLimits<T>::Max();
			int32 BoxIdx = 0;
			for (const FSplitInfo& SplitInfo : SplitInfos)
			{
				TBox<T, 3> NewBox = SplitInfo.SplitBounds;
				NewBox.GrowToInclude(Elem.Bounds);
				const T Delta2 = NewBox.Extents().SizeSquared() - SplitInfo.SplitBoundsSize2;
				if (Delta2 < MinDelta2)
				{
					MinDelta2 = Delta2;
					MinBoxIdx = BoxIdx;
				}
				++BoxIdx;
			}
			
			SplitInfos[MinBoxIdx].Children.Add(Elem);
			SplitInfos[MinBoxIdx].RealBounds.GrowToInclude(Elem.Bounds);
		}

		if (SplitInfos[0].Children.Num() && SplitInfos[1].Children.Num())
		{
			Nodes[NewNodeIdx].bLeaf = false;
			for (int32 BoxIdx = 0; BoxIdx < 2; ++BoxIdx)
			{
				const int32 ChildIdx = SplitNode(SplitInfos[BoxIdx].RealBounds, SplitInfos[BoxIdx].Children, NodeLevel + 1);
				Nodes[NewNodeIdx].ChildrenBounds[BoxIdx] = SplitInfos[BoxIdx].RealBounds;
				Nodes[NewNodeIdx].ChildrenNodes[BoxIdx] = ChildIdx;
			}
		}
		else
		{
			//couldn't split so just make a leaf
			MakeLeaf();
		}

		return NewNodeIdx;
	}
	
	TAABBTree(const TAABBTree<TPayloadType, TLeafType, T>& Other)
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
	{

	}

	TBox<T, 3> FullBounds;
	TArray<FNode> Nodes;
	TArray<TLeafType> Leaves;
	TArray<FElement> DirtyElements;
	TArray<FElement> GlobalPayloads;
	TMap<TPayloadType, FAABBTreePayloadInfo> PayloadToInfo;
	int32 MaxChildrenInLeaf;
	int32 MaxTreeDepth;
	T MaxPayloadBounds;
};

template<typename TPayloadType, typename TLeafType, class T>
FArchive& operator<<(FChaosArchive& Ar, TAABBTree<TPayloadType, TLeafType, T>& AABBTree)
{
	check(false);
	//AABBTree.Serialize(Ar);
	return Ar;
}


}
