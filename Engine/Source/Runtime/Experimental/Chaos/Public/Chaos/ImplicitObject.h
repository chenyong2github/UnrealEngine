// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Pair.h"
#include "Chaos/Serializable.h"
#include "Chaos/Core.h"

#include <functional>

#ifndef TRACK_CHAOS_GEOMETRY
#define TRACK_CHAOS_GEOMETRY !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

namespace Chaos
{
template<class T, int d>
class TAABB;
template<class T>
class TCylinder;
template<class T, int d>
class TSphere;
template<class T, int d>
class TPlane;
template<class T, int d>
class TParticles;
template<class T, int d>
class TBVHParticles;
class FImplicitObject;

namespace ImplicitObjectType
{
	enum
	{
		//Note: add entries in order to avoid serialization issues (but before IsInstanced)
		Sphere = 0,
		Box,
		Plane,
		Capsule,
		Transformed,
		Union,
		LevelSet,
		Unknown,
		Convex,
		TaperedCylinder,
		Cylinder,
		TriangleMesh,
		HeightField,
		DEPRECATED_Scaled,	//needed for serialization of existing data
		Triangle,
		UnionClustered,

		//Add entries above this line for serialization
		IsInstanced = 1 << 6,
		IsScaled = 1 << 7
	};
}

using EImplicitObjectType = uint8;	//see ImplicitObjectType

FORCEINLINE bool IsInstanced(EImplicitObjectType Type)
{
	return (Type & ImplicitObjectType::IsInstanced) != 0;
}

FORCEINLINE bool IsScaled(EImplicitObjectType Type)
{
	return (Type & ImplicitObjectType::IsScaled) != 0;
}

FORCEINLINE EImplicitObjectType GetInnerType(EImplicitObjectType Type)
{
	return Type & (~(ImplicitObjectType::IsScaled | ImplicitObjectType::IsInstanced));
}


namespace EImplicitObject
{
	enum Flags
	{
		IsConvex = 1,
		HasBoundingBox = 1 << 1,
	};

	const int32 FiniteConvex = IsConvex | HasBoundingBox;
}



template<class T, int d, bool bSerializable>
struct TImplicitObjectPtrStorage
{
};

template<class T, int d>
struct TImplicitObjectPtrStorage<T, d, false>
{
	using PtrType = FImplicitObject*;

	static PtrType Convert(const TUniquePtr<FImplicitObject>& Object)
	{
		return Object.Get();
	}
};

template<class T, int d>
struct TImplicitObjectPtrStorage<T, d, true>
{
	using PtrType = TSerializablePtr<FImplicitObject>;

	static PtrType Convert(const TUniquePtr<FImplicitObject>& Object)
	{
		return MakeSerializable(Object);
	}
};

class CHAOS_API FImplicitObject
{
public:
	using TType = FReal;
	static constexpr int D = 3;
	static FImplicitObject* SerializationFactory(FChaosArchive& Ar, FImplicitObject* Obj);

	FImplicitObject(int32 Flags, EImplicitObjectType InType = ImplicitObjectType::Unknown);
	FImplicitObject(const FImplicitObject&) = delete;
	FImplicitObject(FImplicitObject&&) = delete;
	virtual ~FImplicitObject();

	template<class T_DERIVED>
	T_DERIVED* GetObject()
	{
		if (T_DERIVED::StaticType() == Type)
		{
			return static_cast<T_DERIVED*>(this);
		}
		return nullptr;
	}

	template<class T_DERIVED>
	const T_DERIVED* GetObject() const
	{
		if (T_DERIVED::StaticType() == Type)
		{
			return static_cast<const T_DERIVED*>(this);
		}
		return nullptr;
	}

	template<class T_DERIVED>
	const T_DERIVED& GetObjectChecked() const
	{
		check(T_DERIVED::StaticType() == Type);
		return static_cast<const T_DERIVED&>(*this);
	}

	template<class T_DERIVED>
	T_DERIVED& GetObjectChecked()
	{
		check(T_DERIVED::StaticType() == Type);
		return static_cast<const T_DERIVED&>(*this);
	}

	EImplicitObjectType GetType(bool bGetTrueType = false) const;

	virtual bool IsValidGeometry() const;

	virtual TUniquePtr<FImplicitObject> Copy() const;

	//This is strictly used for optimization purposes
	bool IsUnderlyingUnion() const;

	// Explicitly non-virtual.  Must cast to derived types to target their implementation.
	FReal SignedDistance(const FVec3& x) const;

	// Explicitly non-virtual.  Must cast to derived types to target their implementation.
	FVec3 Normal(const FVec3& x) const;
	virtual FReal PhiWithNormal(const FVec3& x, FVec3& Normal) const = 0;
	virtual const class TAABB<FReal, 3>& BoundingBox() const;
	bool HasBoundingBox() const { return bHasBoundingBox; }
	bool IsConvex() const { return bIsConvex; }
	void SetDoCollide(const bool Collide ) { bDoCollide = Collide; }
	bool GetDoCollide() const { return bDoCollide; }
	void SetConvex(const bool Convex = true) { bIsConvex = Convex; }
	
#if TRACK_CHAOS_GEOMETRY
	//Turn on memory tracking. Must pass object itself as a serializable ptr so we can save it out
	void Track(TSerializablePtr<FImplicitObject> This, const FString& DebugInfo);
#endif

	virtual bool IsPerformanceWarning() const { return false; }
	virtual FString PerformanceWarningAndSimplifaction() 
	{
		return FString::Printf(TEXT("ImplicitObject - No Performance String"));
	};

	Pair<FVec3, bool> FindDeepestIntersection(const FImplicitObject* Other, const TBVHParticles<FReal, 3>* Particles, const FMatrix33& OtherToLocalTransform, const FReal Thickness) const;
	Pair<FVec3, bool> FindDeepestIntersection(const FImplicitObject* Other, const TParticles<FReal, 3>* Particles, const FMatrix33& OtherToLocalTransform, const FReal Thickness) const;
	Pair<FVec3, bool> FindClosestIntersection(const FVec3& StartPoint, const FVec3& EndPoint, const FReal Thickness) const;

	//This gives derived types a way to avoid calling PhiWithNormal todo: this api is confusing
	virtual bool Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const
	{
		OutFaceIndex = INDEX_NONE;
		const FVec3 EndPoint = StartPoint + Dir * Length;
		Pair<FVec3, bool> Result = FindClosestIntersection(StartPoint, EndPoint, Thickness);
		if (Result.Second)
		{
			OutPosition = Result.First;
			OutNormal = Normal(Result.First);
			OutTime = Length > 0 ? (OutPosition - StartPoint).Size() : 0.f;
			return true;
		}
		return false;
	}

	/** Returns the most opposing face.
		@param Position - local position to search around (for example an edge of a convex hull)
		@param UnitDir - the direction we want to oppose (for example a ray moving into the edge of a convex hull would get the face with the most negative dot(FaceNormal, UnitDir)
		@param HintFaceIndex - for certain geometry we can use this to accelerate the search.
		@return Index of the most opposing face
	*/
	virtual int32 FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const
	{
		//Many objects have no concept of a face
		return INDEX_NONE;
	}


	/** Finds the first intersecting face at given position
	@param Position - local position to search around (for example a point on the surface of a convex hull)
	@param FaceIndices - Vertices that lie on the face plane.
	@param SearchDistance - distance to surface [def:0.01]
	*/
	virtual int32 FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist = 0.01) const
	{
		//Many objects have no concept of a face
		return INDEX_NONE;
	}


	/** Given a normal and a face index, compute the most opposing normal associated with the underlying geometry features.
		For example a sphere swept against a box may not give a normal associated with one of the box faces. This function will return a normal associated with one of the faces.
		@param DenormDir - the direction we want to oppose
		@param FaceIndex - the face index associated with the geometry (for example if we hit a specific face of a convex hull)
		@param OriginalNormal - the original normal given by something like a sphere sweep
		@return The most opposing normal associated with the underlying geometry's feature (like a face)
	*/
	virtual FVec3 FindGeometryOpposingNormal(const FVec3& DenormDir, int32 FaceIndex, const FVec3& OriginalNormal) const
	{
		//Many objects have no concept of a face
		return OriginalNormal;
	}

	//This gives derived types a way to do an overlap check without calling PhiWithNormal todo: this api is confusing
	virtual bool Overlap(const FVec3& Point, const FReal Thickness) const
	{
		return SignedDistance(Point) <= Thickness;
	}

	virtual void AccumulateAllImplicitObjects(TArray<Pair<const FImplicitObject*, FRigidTransform3>>& Out, const FRigidTransform3& ParentTM) const
	{
		Out.Add(MakePair(this, ParentTM));
	}

	virtual void AccumulateAllSerializableImplicitObjects(TArray<Pair<TSerializablePtr<FImplicitObject>, FRigidTransform3>>& Out, const FRigidTransform3& ParentTM, TSerializablePtr<FImplicitObject> This) const
	{
		Out.Add(MakePair(This, ParentTM));
	}

	virtual void FindAllIntersectingObjects(TArray < Pair<const FImplicitObject*, FRigidTransform3>>& Out, const TAABB<FReal, 3>& LocalBounds) const;

	virtual FString ToString() const
	{
		return FString::Printf(TEXT("ImplicitObject bIsConvex:%d, bIgnoreAnalyticCollision:%d, bHasBoundingBox:%d"), bIsConvex, bDoCollide, bHasBoundingBox);
	}

	void SerializeImp(FArchive& Ar);

	constexpr static EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::Unknown;
	}
	
	virtual void Serialize(FArchive& Ar)
	{
		check(false);	//Aggregate implicits require FChaosArchive - check false by default
	}

	virtual void Serialize(FChaosArchive& Ar);
	
	static FArchive& SerializeLegacyHelper(FArchive& Ar, TUniquePtr<FImplicitObject>& Value);

	virtual uint32 GetTypeHash() const = 0;

	virtual FName GetTypeName() const { return GetTypeName(GetType()); }

	static const FName GetTypeName(const EImplicitObjectType InType);

	virtual uint16 GetMaterialIndex(uint32 HintIndex) const { return 0; }

protected:
	EImplicitObjectType Type;
	bool bIsConvex;
	bool bDoCollide;
	bool bHasBoundingBox;

#if TRACK_CHAOS_GEOMETRY
	bool bIsTracked;
#endif

private:
	virtual Pair<FVec3, bool> FindClosestIntersectionImp(const FVec3& StartPoint, const FVec3& EndPoint, const FReal Thickness) const;
};

FORCEINLINE FChaosArchive& operator<<(FChaosArchive& Ar, FImplicitObject& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

FORCEINLINE FArchive& operator<<(FArchive& Ar, FImplicitObject& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

typedef FImplicitObject FImplicitObject3;
}