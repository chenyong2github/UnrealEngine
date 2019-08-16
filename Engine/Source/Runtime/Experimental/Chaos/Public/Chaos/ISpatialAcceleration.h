// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Chaos/Vector.h"

namespace Chaos
{

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
	virtual bool Overlap(const TPayloadType Instance) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a raycast
		@Instance - the instance we are potentially intersecting with a raycast
		@CurLength - the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Raycast(const TPayloadType Instance, T& CurLength) = 0;

	/** Called whenever an instance in the acceleration structure may intersect with a sweep
		@Instance - the instance we are potentially intersecting with a sweep
		@CurLength - the length all future intersection tests will use. A blocking intersection should update this
		Returns true to continue iterating through the acceleration structure
	*/
	virtual bool Sweep(const TPayloadType Instance, T& CurLength) = 0;
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

template <typename TPayloadType, typename T, int d>
class CHAOS_API ISpatialAcceleration
{
public:
	virtual ~ISpatialAcceleration() = default;

	virtual TArray<TPayloadType> FindAllIntersections(const TBox<T, d>& Box) const = 0;
	virtual TArray<TPayloadType> FindAllIntersections(const TSpatialRay<T,d>& Ray) const = 0;
	virtual TArray<TPayloadType> FindAllIntersections(const TVector<T, d>& Point) const = 0;
	virtual TArray<TPayloadType> FindAllIntersections(const TGeometryParticles<T, d>& InParticles, const int32 i) const = 0;

	virtual void Raycast(const TVector<T, d>& Start, const TVector<T, d>& Dir, const T OriginalLength, ISpatialVisitor<TPayloadType, T>& Visitor) const {}
	virtual void Sweep(const TVector<T, d>& Start, const TVector<T, d>& Dir, T OriginalLength, const TVector<T, d> QueryHalfExtents, ISpatialVisitor<TPayloadType, T>& Visitor, const TVector<T, d>& Scale = TVector<T, d>(1)) const {}
	virtual void Overlap(const TBox<T, d>& QueryBounds, ISpatialVisitor<TPayloadType, T>& Visitor, const TVector<T, d>& Scale = TVector<T, d>(1)) const {}

#if !UE_BUILD_SHIPPING
	virtual void DebugDraw(ISpacialDebugDrawInterface<T>* InInterface) const {}
	virtual void DumpStats() const {}
#endif

};

/** Helper class used to bridge virtual to template implementation of acceleration structures */
template <typename TPayloadType, typename T>
class TSpatialVisitor
{
public:
	TSpatialVisitor(ISpatialVisitor<TPayloadType, T>& InVisitor)
		: Visitor(InVisitor) {}
	FORCEINLINE bool VisitOverlap(const TPayloadType Instance)
	{
		return Visitor.Overlap(Instance);
	}

	FORCEINLINE bool VisitRaycast(const TPayloadType Instance, T& CurLength)
	{
		return Visitor.Raycast(Instance, CurLength);
	}

	FORCEINLINE bool VisitSweep(const TPayloadType Instance, T& CurLength)
	{
		return Visitor.Sweep(Instance, CurLength);
	}

private:
	ISpatialVisitor<TPayloadType, T>& Visitor;
};

}
