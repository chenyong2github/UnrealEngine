// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/Box.h"
#include "GeometryParticlesfwd.h"
#include <tuple>
#include "PBDCollisionConstraintImp.h"

namespace Chaos
{

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAccelerationCollection : public ISpatialAcceleration<TPayloadType, T, d>
{
public:
	ISpatialAccelerationCollection() : ISpatialAcceleration<TPayloadType, T, d>(StaticType) {}
	static constexpr ESpatialAcceleration StaticType = ESpatialAcceleration::Collection;
	virtual FSpatialAccelerationIdx AddSubstructure(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Substructure, int32 Bucket) = 0;
	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> RemoveSubstructure(FSpatialAccelerationIdx Idx) = 0;
	virtual ISpatialAcceleration<TPayloadType, T, d>* GetSubstructure(FSpatialAccelerationIdx Idx) = 0;

	/** This is kind of a hack to avoid virtuals. We simply route calls into templated functions */
	virtual void PBDComputeConstraintsLowLevel_GatherStats(TPBDCollisionConstraint<T, d>& CollisionConstraint, T Dt) const = 0;
	virtual void PBDComputeConstraintsLowLevel(TPBDCollisionConstraint<T, d>& CollisionConstraint, T Dt) const = 0;
	virtual TArray<FSpatialAccelerationIdx> GetAllSpatialIndices() const = 0;
};

template <typename TSpatialAcceleration>
class CHAOS_API TSpatialAccelerationCollectionBucket
{
public:
	using AccelerationType = TSpatialAcceleration;
	using TPayloadType = typename AccelerationType::PayloadType;
	using T = typename AccelerationType::TType;
	static constexpr int d = AccelerationType::D;

	TSpatialAccelerationCollectionBucket() = default;

	TSpatialAccelerationCollectionBucket<TSpatialAcceleration> Copy() const
	{
		return TSpatialAccelerationCollectionBucket<TSpatialAcceleration>(*this);
	}

	uint16 AddSubstructure(TUniquePtr<TSpatialAcceleration>&& Substructure)
	{
		uint16 NewIdx = NewSlot();
		Accelerations[NewIdx] = MoveTemp(Substructure);
		
		return NewIdx;
	}
	
	TUniquePtr <ISpatialAcceleration<TPayloadType, T, d>> RemoveSubstructure(uint16 Idx)
	{
		return ReleaseSlot(Idx);
	}

	TSpatialAcceleration* GetSubstructure(int32 Idx)
	{
		return Accelerations[Idx].Get();
	}

	template <typename SQVisitor>
	void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, SQVisitor& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Raycast(Start, Dir, OriginalLength, Visitor);
			}
		}
	}

	template <typename SQVisitor>
	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, const TVector<T,3>& QueryHalfExtents, SQVisitor& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Sweep(Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
			}
		}
	}

	template <typename SQVisitor>
	void Overlap(const TBox<T,d>& QueryBounds, SQVisitor& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Overlap(QueryBounds, Visitor);
			}
		}
	}

	void GlobalObjects(TArray<TPayloadBoundsElement<TPayloadType, T>>& ObjList) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				ObjList.Append(Acceleration->GlobalObjects());
			}
		}
	}

	void GetAllSpatialIndices(TArray<FSpatialAccelerationIdx>& Indices, uint16 BucketIdx) const
	{
		Indices.Reserve(Indices.Num() + Accelerations.Num());
		uint16 InnerIdx = 0;
		for (const auto& Acceleration : Accelerations)
		{
			Indices.Add(FSpatialAccelerationIdx{ BucketIdx, InnerIdx++ });
		}
	}

	void RemoveElement(const TPayloadType& Payload, int32 Idx)
	{
		Accelerations[Idx]->RemoveElement(Payload);
	}

	void UpdateElement(const TPayloadType& Payload, const TBox<T, d>& NewBounds, bool bHasBounds, int32 Idx)
	{
		Accelerations[Idx]->UpdateElement(Payload, NewBounds, bHasBounds);
	}

	explicit TSpatialAccelerationCollectionBucket(const TSpatialAccelerationCollectionBucket<TSpatialAcceleration>& Other)
		: FreeIndices(Other.FreeIndices)
	{
		Accelerations.Reserve(Other.Accelerations.Num());
		for (const auto& OtherAccel : Other.Accelerations)
		{
			if (OtherAccel)
			{
				Accelerations.Add(AsUniqueSpatialAccelerationChecked<TSpatialAcceleration>(OtherAccel->Copy()));
			}
			else
			{
				Accelerations.Add(nullptr);
			}
		}
	}

private:
	uint16 NewSlot()
	{
		if (FreeIndices.Num())
		{
			return FreeIndices.Pop(false);
		}
		else if (Accelerations.Num() < FSpatialAccelerationIdx::MaxBucketEntries)
		{
			return Accelerations.Add(nullptr);
		}
		check(false);	//too many substructures in a single bucket
		return 0;
	}

	TUniquePtr <ISpatialAcceleration<TPayloadType, T, d>> ReleaseSlot(uint16 Index)
	{
		TUniquePtr <ISpatialAcceleration<TPayloadType, T, d>> Result = MoveTemp(Accelerations[Index]);
		Accelerations[Index] = nullptr;
		if (Index + 1 == Accelerations.Num())
		{
			Accelerations.Pop(false);
		}
		else
		{
			FreeIndices.Add(Index);
		}
		return Result;
	}

	TSpatialAccelerationCollectionBucket& operator=(const TSpatialAccelerationCollectionBucket<TSpatialAcceleration>& Other)
	{
		if (&Other != this)
		{
			FreeIndices = Other.FreeIndices;
			Accelerations.Reset(Other.Accelerations.Num());

			for (const auto& OtherAccel : Other.Accelerations)
			{
				if (OtherAccel)
				{
					Accelerations.Add(AsUniqueSpatialAccelerationChecked<TSpatialAcceleration>(OtherAccel->Copy()));
				}
				else
				{
					Accelerations.Add(nullptr);
				}
			}
		}

		return *this;
	}

	TArray<TUniquePtr<TSpatialAcceleration>> Accelerations;
	TArray<uint16> FreeIndices;
};

template <typename... TRemaining>
struct CHAOS_API TSpatialBucketTuple
{
};

template <typename TAcceleration, typename... TRemaining>
struct CHAOS_API TSpatialBucketTuple<TAcceleration, TRemaining...>
{
	using FirstType = TAcceleration;
	TSpatialAccelerationCollectionBucket<TAcceleration> First;
	TSpatialBucketTuple<TRemaining...> Remaining;

	TSpatialBucketTuple() = default;

	explicit TSpatialBucketTuple(const TSpatialBucketTuple<TAcceleration, TRemaining...>& Other)
		: First(Other.First)
		, Remaining(Other.Remaining)
	{
	}
};

template<int Idx, typename ... Rest>
struct CHAOS_API TSpatialBucketTupleGetter
{

};

template<int Idx, typename First, typename... Rest>
struct CHAOS_API TSpatialBucketTupleGetter<Idx, First, Rest...>
{
	static auto& Get(TSpatialBucketTuple<First, Rest...>& Buckets) { return TSpatialBucketTupleGetter<Idx - 1, Rest...>::Get(Buckets.Remaining); }
	static const auto& Get(const TSpatialBucketTuple<First, Rest...>& Buckets) { return TSpatialBucketTupleGetter<Idx - 1, Rest...>::Get(Buckets.Remaining); }
};

template<typename First, typename... Rest>
struct CHAOS_API TSpatialBucketTupleGetter<0, First, Rest...>
{
	static auto& Get(TSpatialBucketTuple<First, Rest...>& Buckets) { return Buckets.First; }
	static const auto& Get(const TSpatialBucketTuple<First, Rest...>& Buckets) { return Buckets.First; }
};

template <int Idx, typename First, typename... Rest>
auto& GetBucket(TSpatialBucketTuple<First, Rest...>& Buckets)
{
	return TSpatialBucketTupleGetter<Idx, First, Rest...>::Get(Buckets);
}

template <int Idx, typename First, typename... Rest>
const auto& GetBucket(const TSpatialBucketTuple<First, Rest...>& Buckets)
{
	return TSpatialBucketTupleGetter<Idx, First, Rest...>::Get(Buckets);
}

template <int BucketIdx, int NumBuckets, typename Tuple, typename TPayloadType, typename T, int d>
struct TSpatialAccelerationCollectionHelper
{
	template <typename SQVisitor>
	static void Raycast(const Tuple& Buckets, const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, SQVisitor& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Raycast(Start, Dir, OriginalLength, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper<NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Raycast(Buckets, Start, Dir, OriginalLength, Visitor);
		}
	}

	template <typename SQVisitor>
	static void Sweep(const Tuple& Buckets, const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, const TVector<T, d> QueryHalfExtents, SQVisitor& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Sweep(Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Sweep(Buckets, Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
		}
	}

	template <typename SQVisitor>
	static void Overlap(const Tuple& Buckets, const TBox<T, d> QueryBounds, SQVisitor& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Overlap(QueryBounds, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Overlap(Buckets, QueryBounds, Visitor);
		}
	}

	static void GlobalObjects(const Tuple& Buckets, TArray<TPayloadBoundsElement<TPayloadType, T>>& ObjList)
	{
		GetBucket<BucketIdx>(Buckets).GlobalObjects(ObjList);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::GlobalObjects(Buckets, ObjList);
		}
	}

	static void GetAllSpatialIndices(const Tuple& Buckets, TArray<FSpatialAccelerationIdx>& Indices)
	{
		GetBucket<BucketIdx>(Buckets).GetAllSpatialIndices(Indices, BucketIdx);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::GetAllSpatialIndices(Buckets, Indices);
		}
	}
};

template <bool bGatherStats, typename SpatialAccelerationCollection, typename T, int d>
typename TEnableIf<TIsSame<typename SpatialAccelerationCollection::TPayloadType, TAccelerationStructureHandle<T,d>>::Value, void>::Type PBDComputeConstraintsLowLevel_Helper(TPBDCollisionConstraint<T, d>& CollisionConstraint, const SpatialAccelerationCollection& Accel, T Dt)
{
	CollisionConstraint.template ComputeConstraintsHelperLowLevel<bGatherStats>(Accel, Dt);
}

template <bool bGatherStats, typename SpatialAccelerationCollection, typename T, int d>
typename TEnableIf<!TIsSame<typename SpatialAccelerationCollection::TPayloadType, TAccelerationStructureHandle<T, d>>::Value, void>::Type PBDComputeConstraintsLowLevel_Helper(TPBDCollisionConstraint<T, d>& CollisionConstraint, const SpatialAccelerationCollection& Accel, T Dt)
{
}

template <typename ... TSpatialAccelerationBuckets>
class CHAOS_API TSpatialAccelerationCollection : public
	ISpatialAccelerationCollection<typename std::tuple_element<0, std::tuple< TSpatialAccelerationBuckets...>>::type::PayloadType,
	typename std::tuple_element<0, std::tuple< TSpatialAccelerationBuckets...>>::type::TType,
	std::tuple_element<0, std::tuple< TSpatialAccelerationBuckets...>>::type::D>
{
public:
	using FirstAccelerationType = typename std::tuple_element<0, std::tuple< TSpatialAccelerationBuckets...>>::type;
	using TPayloadType = typename FirstAccelerationType::PayloadType;
	using T = typename FirstAccelerationType::TType;
	static constexpr int d = FirstAccelerationType::D;
	TSpatialAccelerationCollection() = default;

	virtual FSpatialAccelerationIdx AddSubstructure(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Substructure, int32 BucketIdx) override
	{
		check(BucketIdx < NumBuckets);
		FSpatialAccelerationIdx Result;
		Result.Bucket = BucketIdx;
		
		switch (BucketIdx)
		{
		case 0: Result.InnerIdx = GetBucket<0>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<0>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 1: Result.InnerIdx = GetBucket<ClampedIdx(1)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(1)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 2: Result.InnerIdx = GetBucket<ClampedIdx(2)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(2)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 3: Result.InnerIdx = GetBucket<ClampedIdx(3)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(3)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 4: Result.InnerIdx = GetBucket<ClampedIdx(4)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(4)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 5: Result.InnerIdx = GetBucket<ClampedIdx(5)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(5)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 6: Result.InnerIdx = GetBucket<ClampedIdx(6)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(6)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 7: Result.InnerIdx = GetBucket<ClampedIdx(7)>(Buckets).AddSubstructure(AsUniqueSpatialAccelerationChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(7)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		default: check(false);
		}
		return Result;
	}

	virtual TUniquePtr <ISpatialAcceleration<TPayloadType, T, d>> RemoveSubstructure(FSpatialAccelerationIdx Idx) override
	{
		check(Idx.Bucket < NumBuckets);
		switch (Idx.Bucket)
		{
		case 0: return GetBucket<0>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 1: return GetBucket<ClampedIdx(1)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 2: return GetBucket<ClampedIdx(2)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 3: return GetBucket<ClampedIdx(3)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 4: return GetBucket<ClampedIdx(4)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 5: return GetBucket<ClampedIdx(5)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 6: return GetBucket<ClampedIdx(6)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		case 7: return GetBucket<ClampedIdx(7)>(Buckets).RemoveSubstructure(Idx.InnerIdx);
		default: check(false);
		}
		return nullptr;
	}

	virtual ISpatialAcceleration<TPayloadType, T, d>* GetSubstructure(FSpatialAccelerationIdx Idx) override
	{
		check (Idx.Bucket < NumBuckets);
		switch (Idx.Bucket)
		{
		case 0: return GetBucket<0>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 1: return GetBucket<ClampedIdx(1)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 2: return GetBucket<ClampedIdx(2)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 3: return GetBucket<ClampedIdx(3)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 4: return GetBucket<ClampedIdx(4)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 5: return GetBucket<ClampedIdx(5)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 6: return GetBucket<ClampedIdx(6)>(Buckets).GetSubstructure(Idx.InnerIdx);
		case 7: return GetBucket<ClampedIdx(7)>(Buckets).GetSubstructure(Idx.InnerIdx);
		default: check(false);
		}
		return nullptr;
	}

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Raycast(Buckets, Start, Dir, OriginalLength, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, SQVisitor& Visitor) const
	{
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Raycast(Buckets, Start, Dir, OriginalLength, Visitor);
	}

	virtual void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Sweep(Buckets, Start, Dir, OriginalLength, QueryHalfExtents, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, SQVisitor& Visitor) const
	{
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Sweep(Buckets, Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
	}

	virtual void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		TSpatialVisitor<TPayloadType, T> ProxyVisitor(Visitor);
		Overlap(QueryBounds, ProxyVisitor);
	}

	template <typename SQVisitor>
	void Overlap(const TBox<T, 3>& QueryBounds, SQVisitor& Visitor) const
	{
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Overlap(Buckets, QueryBounds, Visitor);
	}

	TArray<TPayloadBoundsElement<TPayloadType, T>> GlobalObjects() const
	{
		TArray<TPayloadBoundsElement<TPayloadType, T>> ObjList;
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::GlobalObjects(Buckets, ObjList);
		return ObjList;
	}

	virtual TArray<FSpatialAccelerationIdx> GetAllSpatialIndices() const override
	{
		TArray<FSpatialAccelerationIdx> Indices;
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::GetAllSpatialIndices(Buckets, Indices);
		return Indices;
	}

	virtual void RemoveElementFrom(const TPayloadType& Payload, FSpatialAccelerationIdx SpatialIdx) override
	{
		check(SpatialIdx.Bucket < NumBuckets);
		switch (SpatialIdx.Bucket)
		{
		case 0: return GetBucket<0>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 1: return GetBucket<ClampedIdx(1)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 2: return GetBucket<ClampedIdx(2)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 3: return GetBucket<ClampedIdx(3)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 4: return GetBucket<ClampedIdx(4)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 5: return GetBucket<ClampedIdx(5)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 6: return GetBucket<ClampedIdx(6)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		case 7: return GetBucket<ClampedIdx(7)>(Buckets).RemoveElement(Payload, SpatialIdx.InnerIdx);
		default: check(false);
		}
	}

	virtual void UpdateElementIn(const TPayloadType& Payload, const TBox<T, d>& NewBounds, bool bHasBounds, FSpatialAccelerationIdx SpatialIdx)
	{
		check(SpatialIdx.Bucket < NumBuckets);
		switch (SpatialIdx.Bucket)
		{
		case 0: return GetBucket<0>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 1: return GetBucket<ClampedIdx(1)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 2: return GetBucket<ClampedIdx(2)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 3: return GetBucket<ClampedIdx(3)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 4: return GetBucket<ClampedIdx(4)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 5: return GetBucket<ClampedIdx(5)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 6: return GetBucket<ClampedIdx(6)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		case 7: return GetBucket<ClampedIdx(7)>(Buckets).UpdateElement(Payload, NewBounds, bHasBounds, SpatialIdx.InnerIdx);
		default: check(false);
		}
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> Copy() const
	{
		return TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>(new TSpatialAccelerationCollection<TSpatialAccelerationBuckets...>(*this));
	}

	virtual void PBDComputeConstraintsLowLevel_GatherStats(TPBDCollisionConstraint<T, d>& CollisionConstraint, T Dt) const override
	{
		PBDComputeConstraintsLowLevel_Helper<true>(CollisionConstraint, *this, Dt);
	}

	virtual void PBDComputeConstraintsLowLevel(TPBDCollisionConstraint<T,d>& CollisionConstraint, T Dt) const override
	{
		PBDComputeConstraintsLowLevel_Helper<false>(CollisionConstraint, *this, Dt);
	}

private:

	TSpatialAccelerationCollection(const TSpatialAccelerationCollection<TSpatialAccelerationBuckets...>& Other) = default;
	
	static constexpr uint32 ClampedIdx(uint32 Idx)
	{
		return Idx < NumBuckets ? Idx : 0;
	}

	TSpatialBucketTuple< TSpatialAccelerationBuckets...> Buckets;
	static constexpr uint32 NumBuckets = sizeof...(TSpatialAccelerationBuckets);
	static_assert(NumBuckets < 8, "8 max buckets supported");
};


}
