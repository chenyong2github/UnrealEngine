// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Vector.h"
#include "Chaos/Box.h"
#include "GeometryParticlesfwd.h"
#include <tuple>

namespace Chaos
{

template <typename T, int d>
class TBox;

template <typename T, int d>
class TGeometryParticle;

template <typename T, int d>
class TSpatialRay
{
public:
	TSpatialRay()
		: Start((T)0)
		, End((T)0)
	{}

	TSpatialRay(const TVector<T, d>& InStart, const TVector<T, d> InEnd)
		: Start(InStart)
		, End(InEnd)
	{}

	TVector<T, d> Start;
	TVector<T, d> End;
};

/** A struct which is passed to spatial acceleration visitors whenever there are potential hits.
	In production, this class will only contain a payload.
*/
template <typename TPayloadType>
struct CHAOS_API TSpatialVisitorData
{
	TPayloadType Payload;
	TSpatialVisitorData(const TPayloadType& InPayload, const bool bInHasBounds = false, const TBox<float, 3>& InBounds = TBox<float, 3>::ZeroBox())
		: Payload(InPayload)
#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
		, bHasBounds(bInHasBounds)
		, Bounds(InBounds)
	{ }
	bool bHasBounds;
	TBox<float, 3> Bounds;
#else
	{ }
#endif
};

/** Visitor base class used to iterate through spatial acceleration structures.
	This class is responsible for gathering any information it wants (for example narrow phase query results).
	This class determines whether the acceleration structure should continue to iterate through potential instances
*/
template <typename TPayloadType, typename T>
class CHAOS_API ISpatialVisitor
{
public:
	virtual ~ISpatialVisitor() = default;

	/** Called whenever an instance in the acceleration structure may overlap
		@Instance - the instance we are potentially overlapping
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Overlap(const TSpatialVisitorData<TPayloadType>& Instance) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a raycast
		@Instance - the instance we are potentially intersecting with a raycast
		@CurLength - the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Raycast(const TSpatialVisitorData<TPayloadType>& Instance, T& CurLength) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a sweep
		@Instance - the instance we are potentially intersecting with a sweep
		@CurLength - the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Sweep(const TSpatialVisitorData<TPayloadType>& Instance, T& CurLength) = 0;
};

enum class ESpatialAccelerationType
{
	Grid,
	BVH
};

/**
 * Can be implemented by external, non-chaos systems to collect / render
 * debug information from spacial structures. When passed to the debug
 * methods on ISpatialAcceleration the methods will be called out by
 * the spacial structure if implemented for the external system to handle
 * the actual drawing.
 */
template <typename T>
class ISpacialDebugDrawInterface
{
public:
	
	virtual ~ISpacialDebugDrawInterface() = default;

	virtual void Box(const TBox<T, 3>& InBox, const TVector<T, 3>& InLinearColor, float InThickness) = 0;
	virtual void Line(const TVector<T, 3>& InBegin, const TVector<T, 3>& InEnd, const TVector<T, 3>& InLinearColor, float InThickness)  = 0;

};

enum ESpatialAcceleration
{
	BoundingVolume,
	AABBTree,
	AABBTreeBV,
	Unknown
};


template <typename TPayloadType, typename T>
struct TPayloadBoundsElement
{
	TPayloadType Payload;
	TBox<T, 3> Bounds;

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Payload;
		Ar << Bounds;
	}

	template <typename TPayloadType2>
	TPayloadType2 GetPayload(int32 Idx) const { return Payload; }

	bool HasBoundingBox() const { return true; }

	const TBox<T, 3>& BoundingBox() const { return Bounds; }
};

template <typename TPayloadType, typename T>
FChaosArchive& operator<<(FChaosArchive& Ar, TPayloadBoundsElement<TPayloadType, T>& PayloadElement)
{
	PayloadElement.Serialize(Ar);
	return Ar;
}

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAcceleration
{
public:

	ISpatialAcceleration(ESpatialAcceleration InType = ESpatialAcceleration::Unknown)
		: Type(InType)
	{
	}
	virtual ~ISpatialAcceleration() = default;

	virtual TArray<TPayloadType> FindAllIntersections(const TBox<T, d>& Box) const { check(false); return TArray<TPayloadType>(); }

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false); }
	virtual void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false); }
	virtual void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const { check(false); }

	virtual void RemoveElement(const TPayloadType& Payload)
	{
		check(false);	//not implemented
	}

	virtual void UpdateElement(const TPayloadType& Payload, const TBox<T, d>& NewBounds, bool bHasBounds)
	{
		check(false);
	}

	virtual void RemoveElementFrom(const TPayloadType& Payload, FSpatialAccelerationIdx Idx)
	{
		RemoveElement(Payload);
	}

	virtual void UpdateElementIn(const TPayloadType& Payload, const TBox<T, d>& NewBounds, bool bHasBounds, FSpatialAccelerationIdx Idx)
	{
		UpdateElement(Payload, NewBounds, bHasBounds);
	}

	virtual TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>> Copy() const
	{
		check(false);	//not implemented
		return nullptr;
	}

	/** Begin Root-level API. This is kind of a hack to avoid virtuals. We simply route calls into templated functions */
	virtual void PBDComputeConstraintsHelper() const
	{
		check(false);	//not implemented
	}

#if !UE_BUILD_SHIPPING
	virtual void DebugDraw(ISpacialDebugDrawInterface<T>* InInterface) const {}
	virtual void DumpStats() const {}
#endif

	static ISpatialAcceleration<TPayloadType, T, d>* SerializationFactory(FChaosArchive& Ar, ISpatialAcceleration<TPayloadType, T, d>* Accel);
	virtual void Serialize(FChaosArchive& Ar)
	{
		check(false);
	}

	template <typename TConcrete>
	TConcrete* As()
	{
		return Type == TConcrete::StaticType ? static_cast<TConcrete*>(this) : nullptr;
	}

	template <typename TConcrete>
	const TConcrete* As() const
	{
		return Type == TConcrete::StaticType ? static_cast<const TConcrete*>(this) : nullptr;
	}

	template <typename TConcrete>
	TConcrete& AsChecked()
	{
		check(Type == TConcrete::StaticType); 
		return static_cast<TConcrete&>(*this);
	}

	template <typename TConcrete>
	const TConcrete& AsChecked() const
	{
		check(Type == TConcrete::StaticType);
		return static_cast<const TConcrete&>(*this);
	}


	template <typename TDerived>
	static TUniquePtr<TDerived> AsUnique(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Base)
	{
		if (TDerived* Derived = Base->template As<TDerived>())
		{
			Base.Release();
			return TUniquePtr<TDerived>(Derived);
		}
		return nullptr;
	}

	template <typename TDerived>
	static TUniquePtr<TDerived> AsUniqueChecked(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Base)
	{
		TDerived& Derived = Base->template AsChecked<TDerived>();
		Base.Release();
		return TUniquePtr<TDerived>(&Derived);
	}

private:
	ESpatialAcceleration Type;
};

/** Helper class used to bridge virtual to template implementation of acceleration structures */
template <typename TPayloadType, typename T>
class TSpatialVisitor
{
public:
	TSpatialVisitor(ISpatialVisitor<TPayloadType, T>& InVisitor)
		: Visitor(InVisitor) {}
	FORCEINLINE bool VisitOverlap(const TSpatialVisitorData<TPayloadType>& Instance)
	{
		return Visitor.Overlap(Instance);
	}

	FORCEINLINE bool VisitRaycast(const TSpatialVisitorData<TPayloadType>& Instance, T& CurLength)
	{
		return Visitor.Raycast(Instance, CurLength);
	}

	FORCEINLINE bool VisitSweep(const TSpatialVisitorData<TPayloadType>& Instance, T& CurLength)
	{
		return Visitor.Sweep(Instance, CurLength);
	}

private:
	ISpatialVisitor<TPayloadType, T>& Visitor;
};

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAccelerationCollection : public ISpatialAcceleration<TPayloadType, T, d>
{
public:
	virtual FSpatialAccelerationIdx AddSubstructure(TUniquePtr<ISpatialAcceleration<TPayloadType, T, d>>&& Substructure, int32 Bucket) = 0;
	virtual void RemoveSubstructure(FSpatialAccelerationIdx Idx) = 0;
	virtual ISpatialAcceleration<TPayloadType, T, d>* GetSubstructure(FSpatialAccelerationIdx Idx) = 0;
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

	//question: should this be private and make higher up code more explicit? kind of annoying because of tuple
	TSpatialAccelerationCollectionBucket(const TSpatialAccelerationCollectionBucket<TSpatialAcceleration>& Other)
		: FreeIndices(Other.FreeIndices)
	{
		Accelerations.Reserve(Other.Accelerations.Num());
		for (const auto& OtherAccel : Other.Accelerations)
		{
			if (OtherAccel)
			{
				Accelerations.Add(TSpatialAcceleration::template AsUniqueChecked<TSpatialAcceleration>(OtherAccel->Copy()));
			}
			else
			{
				Accelerations.Add(nullptr);
			}
		}
	}

	uint16 AddSubstructure(TUniquePtr<TSpatialAcceleration>&& Substructure)
	{
		uint16 NewIdx = NewSlot();
		Accelerations[NewIdx] = MoveTemp(Substructure);
		
		return NewIdx;
	}
	
	void RemoveSubstructure(uint16 Idx)
	{
		ReleaseSlot(Idx);
	}

	TSpatialAcceleration* GetSubstructure(int32 Idx)
	{
		return Accelerations[Idx].Get();
	}

	void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Raycast(Start, Dir, OriginalLength, Visitor);
			}
		}
	}

	void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, const TVector<T,3>& QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Sweep(Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
			}
		}
	}

	void Overlap(const TBox<T,d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		for (const auto& Acceleration : Accelerations)
		{
			if (Acceleration)
			{
				Acceleration->Overlap(QueryBounds, Visitor);
			}
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

	void ReleaseSlot(uint16 Index)
	{
		Accelerations[Index] = nullptr;
		FreeIndices.Add(Index);
		if (Index + 1 == Accelerations.Num())
		{
			Accelerations.Pop(false);
		}
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
	static void Raycast(const Tuple& Buckets, const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Raycast(Start, Dir, OriginalLength, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper<NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Raycast(Buckets, Start, Dir, OriginalLength, Visitor);
		}
	}

	static void Sweep(const Tuple& Buckets, const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Sweep(Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Sweep(Buckets, Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
		}
	}

	static void Overlap(const Tuple& Buckets, const TBox<T, d> QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor)
	{
		GetBucket<BucketIdx>(Buckets).Overlap(QueryBounds, Visitor);
		constexpr int NextBucket = BucketIdx + 1;
		if (NextBucket < NumBuckets)
		{
			TSpatialAccelerationCollectionHelper < NextBucket < NumBuckets ? NextBucket : 0, NumBuckets, Tuple, TPayloadType, T, d>::Overlap(Buckets, QueryBounds, Visitor);
		}
	}
};

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
		case 0: Result.InnerIdx = GetBucket<0>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<0>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 1: Result.InnerIdx = GetBucket<ClampedIdx(1)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(1)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 2: Result.InnerIdx = GetBucket<ClampedIdx(2)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(2)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 3: Result.InnerIdx = GetBucket<ClampedIdx(3)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(3)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 4: Result.InnerIdx = GetBucket<ClampedIdx(4)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(4)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 5: Result.InnerIdx = GetBucket<ClampedIdx(5)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(5)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 6: Result.InnerIdx = GetBucket<ClampedIdx(6)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(6)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		case 7: Result.InnerIdx = GetBucket<ClampedIdx(7)>(Buckets).AddSubstructure(ISpatialAcceleration<TPayloadType, T, d>::template AsUniqueChecked<typename std::remove_reference<decltype(GetBucket<ClampedIdx(7)>(Buckets))>::type::AccelerationType>(MoveTemp(Substructure))); break;
		default: check(false);
		}
		return Result;
	}

	virtual void RemoveSubstructure(FSpatialAccelerationIdx Idx) override
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
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Raycast(Buckets, Start, Dir, OriginalLength, Visitor);
	}

	virtual void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Sweep(Buckets, Start, Dir, OriginalLength, QueryHalfExtents, Visitor);
	}
	virtual void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor) const
	{
		TSpatialAccelerationCollectionHelper<0, NumBuckets, decltype(Buckets), TPayloadType, T, d>::Overlap(Buckets, QueryBounds, Visitor);
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
